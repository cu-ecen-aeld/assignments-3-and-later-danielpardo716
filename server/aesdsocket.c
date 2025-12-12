#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>

#define BUFFER_MAX_LENGTH 1024

static bool caught_signal = false;
static void signal_handler(int signal_number)
{
    if ((signal_number == SIGINT) || (signal_number == SIGTERM))
    {
        syslog(LOG_INFO, "Caught signal, exiting");
        caught_signal = true;
    }
}

static void start_daemon()
{
    int pid = fork();
    if (pid < 0)
    {
        syslog(LOG_ERR, "Failed to create child process: errno %d", errno);
        exit(-1);
    }
    else if (pid == 0)
    {
        // Exit parent process
        // freeaddrinfo(servinfo);
        exit(0);
    }
    
    // Create a new session (ensure no controlling tty)
    if (setsid() < 0)
    {
        syslog(LOG_ERR, "Failed to create new session: errno %d", errno);
    };

    // Change working dir
    chdir("/");

    // Close any open file descriptors
    for (int fd = sysconf(_SC_OPEN_MAX); fd >= 0; --fd)
    {
        close(fd);
    }
    
    // Redirect stdin, stdout, and stderr to /dev/null
    int fd = open("/dev/null", O_RDWR);
    if (fd < 0)
    {
        syslog(LOG_ERR, "Unable to open /dev/null: errno %d", errno);
        exit(-1);
    }
    dup2(fd, STDIN_FILENO);
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    close(fd);
}

int main(int argc, char** argv)
{
    // Open logger
    openlog(NULL, 0, LOG_USER);

    // Setup signal handler
    struct sigaction signal;
    memset(&signal, 0, sizeof(struct sigaction));
    signal.sa_handler = signal_handler;
    if (sigaction(SIGINT, &signal, NULL) != 0)
    {
        syslog(LOG_ERR, "Unable to register SIGINT: errno %d", errno);
        exit(-1);
    }
    if (sigaction(SIGTERM, &signal, NULL) != 0)
    {
        syslog(LOG_ERR, "Unable to register SIGTERM: errno %d", errno);
        exit(-1);
    }

    // Create IPv4 TCP/IP socket
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1)
    {
        syslog(LOG_ERR, "Unable to create socket: errno %d", errno);
        exit(-1);
    }

    // Get address (port 9000)
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    struct addrinfo* servinfo;
    if (getaddrinfo(NULL, "9000", &hints, &servinfo) != 0)
    {
        syslog(LOG_ERR, "Unable to get addr info: errno %d", errno);
        exit(-1);
    }

    // Bind address to socket
    // TODO: use SO_REUSEADDR
    if (bind(sockfd, servinfo->ai_addr, servinfo->ai_addrlen) == -1)
    {
        syslog(LOG_ERR, "Unable to bind socket: errno %d", errno);
        exit(-1);
    }

    // Handle -d argument to run as daemon
    if ((argc == 1) && (strncmp(argv[1], "-d", 2)))
    {
        start_daemon();
    }

    FILE* file_ptr = NULL;
    while (true)
    {
        // Listen for connections
        if (listen(sockfd, 1) < 0)
        {
            syslog(LOG_ERR, "Unable to listen for connection: errno %d", errno);
            exit(-1);
        }
    
        // Accept connection
        int acceptedfd = accept(sockfd, servinfo->ai_addr, &(servinfo->ai_addrlen));
        if (acceptedfd < 0)
        {
            syslog(LOG_ERR, "Unable to accept connection: errno %d", errno);
            exit(-1);
        }
        else
        {
            // TODO: get connected IP
            int clientaddr = 0;
            syslog(LOG_INFO, "Accepted connection from %d", clientaddr);
        }

        // Open file for recv/send messages
        file_ptr = fopen("/var/tmp/aesdsocketdata", "ar");
        if (file_ptr == NULL)
        {
            syslog(LOG_ERR, "Unable to open /var/tmp/aesdsocketdata, errno %d", errno);
            exit(-1);
        }

        // Receive data
        char buffer[BUFFER_MAX_LENGTH];
        int recvbytes = recv(acceptedfd, buffer, sizeof(buffer), 0);
        if (recvbytes < 0)
        {
            syslog(LOG_ERR, "Failed to receive any bytes: errno %d", errno);
            exit(-1);
        }

        // Scan for newline
        for (int i = 0; i < recvbytes; ++i)
        {
            if (buffer[i] == '\n')
            {
                // Append data to file
                // TODO: write appropriate length
                if (fwrite(buffer, 1, sizeof(buffer), file_ptr) != sizeof(buffer))
                {
                    syslog(LOG_ERR, "Failed to write to file: errno %d", errno);
                    exit(-1);
                }
                fflush(file_ptr);
            }
        }

        // Send /var/tmp/aesdsocketdata to client line-by-line
        memset(buffer, 0, sizeof(buffer));
        while (getline(buffer, sizeof(buffer), file_ptr) != -1)
        {
            send(acceptedfd, buffer, sizeof(buffer), 0);
        }
    
        // Close connection and log close message
        fclose(file_ptr);
        close(acceptedfd);
        syslog(LOG_INFO, "Closed connection from %d", clientaddr);

        // Check if signal was received
        if (caught_signal) break;
    }

    // Complete any open connections, close sockets, delete /var/tmp/aesdsocketdata
    close(sockfd);
    freeaddrinfo(servinfo);
    fclose(file_ptr);
    if (remove("/var/tmp/aesdsocketdata") < 0)
    {
        syslog(LOG_ERR, "Failed to remove /var/tmp/aesdsocketdata: errno %d", errno);
        exit(-1);
    }
    
    return 0;
}
