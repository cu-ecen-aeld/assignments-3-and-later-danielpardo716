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
#include <netdb.h>
#include <arpa/inet.h>

#define BUFFER_MAX_LENGTH   1024
#define FILE_PATH           "/var/tmp/aesdsocketdata"

static FILE* file_ptr = NULL;
static int socket_fd = -1;
static int client_fd = -1;

static void cleanup_and_exit(int exit_code)
{
    if (socket_fd != -1)
    {
        shutdown(socket_fd, SHUT_RDWR);
        close(socket_fd);
    }
    if (client_fd != -1)
    {
        shutdown(client_fd, SHUT_RDWR);
        close(client_fd);
    }
    if (file_ptr != NULL)
    {
        fclose(file_ptr);
    }
    if (remove(FILE_PATH) < 0)
    {
        syslog(LOG_ERR, "Failed to remove "FILE_PATH": errno %d", errno);
        exit(-1);
    }
    
    closelog();
    exit(exit_code);
}

static void signal_handler(int signal_number)
{
    if ((signal_number == SIGINT) || (signal_number == SIGTERM))
    {
        syslog(LOG_INFO, "Caught signal %d, exiting", signal_number);
        cleanup_and_exit(0);
    }
}

static void start_daemon()
{
    int pid = fork();
    if (pid < 0)
    {
        syslog(LOG_ERR, "Failed to create child process: errno %d", errno);
        cleanup_and_exit(-1);
    }
    else if (pid == 0)
    {
        // Exit parent process
        cleanup_and_exit(0);
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
        cleanup_and_exit(-1);
    }
    dup2(fd, STDIN_FILENO);
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    close(fd);
}

static void receive_data()
{
    char recv_buffer[BUFFER_MAX_LENGTH];
    char line_buffer[BUFFER_MAX_LENGTH];
    size_t line_len = 0;

    ssize_t bytes_received = 0;
    while ((bytes_received = recv(client_fd, recv_buffer, sizeof(recv_buffer), 0)) > 0)
    {   
        // Scan for newlines and append to file
        for (int i = 0; i < bytes_received; ++i)
        {
            if (recv_buffer[i] == '\n')
            {
                if (fwrite(line_buffer, 1, line_len, file_ptr) != line_len)
                {
                    syslog(LOG_ERR, "Failed to write to file: errno %d", errno);
                    cleanup_and_exit(-1);
                }
                fflush(file_ptr);
            }
            else
            {
                if (line_len < (BUFFER_MAX_LENGTH - 1))
                {
                    line_buffer[line_len] = recv_buffer[i];
                    ++line_len;
                }
            }
        }
    }

    if (bytes_received < 0)
    {
        syslog(LOG_ERR, "Failed to receive any bytes: errno %d", errno);
        cleanup_and_exit(-1);
    }
}

static void send_data()
{
    // Send file to client line-by-line
    char* send_line;
    ssize_t len = 0;
    size_t buffer_size = BUFFER_MAX_LENGTH;
    while ((len = getline(&send_line, &buffer_size, file_ptr)) != -1)
    {
        ssize_t sent = 0;
        while (sent < len)
        {
            ssize_t s = send(client_fd, (send_line + sent), (len - sent), 0);
            if (s < 0)
            {
                syslog(LOG_ERR, "Failed to send any bytes: errno %d", errno);
            }
            sent += s;
        }
    }

    free(send_line);
}

int main(int argc, char** argv)
{
    // Declare variables
    struct sigaction signal;
    struct addrinfo* server_addr;
    struct addrinfo hints;
    struct sockaddr_storage client_addr;
    socklen_t client_addr_size = sizeof(struct sockaddr);
    char client_ip[INET_ADDRSTRLEN];

    // Open logger
    openlog("aesdsocket", 0, LOG_USER);

    // Setup signal handler
    memset(&signal, 0, sizeof(struct sigaction));
    signal.sa_handler = signal_handler;
    if (sigaction(SIGINT, &signal, NULL) != 0)
    {
        syslog(LOG_ERR, "Unable to register SIGINT: errno %d", errno);
        cleanup_and_exit(-1);
    }
    if (sigaction(SIGTERM, &signal, NULL) != 0)
    {
        syslog(LOG_ERR, "Unable to register SIGTERM: errno %d", errno);
        cleanup_and_exit(-1);
    }

    // Get address (port 9000)
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    if (getaddrinfo(NULL, "9000", &hints, &server_addr) != 0)
    {
        syslog(LOG_ERR, "Unable to get addr info: errno %d", errno);
        freeaddrinfo(server_addr);
        cleanup_and_exit(-1);
    }

    // Create IPv4 TCP/IP socket
    socket_fd = socket(server_addr->ai_family, server_addr->ai_socktype, server_addr->ai_protocol);
    if (socket_fd < 0)
    {
        syslog(LOG_ERR, "Unable to create socket: errno %d", errno);
        cleanup_and_exit(-1);
    }

    // Set socket to be reusable
    int optval = 0;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)))
    {
        syslog(LOG_ERR, "Unable to set socket options: errno %d", errno);
        freeaddrinfo(server_addr);
        cleanup_and_exit(-1);
    }

    // Bind address to socket
    if (bind(socket_fd, server_addr->ai_addr, server_addr->ai_addrlen) == -1)
    {
        syslog(LOG_ERR, "Unable to bind socket: errno %d", errno);
        freeaddrinfo(server_addr);
        cleanup_and_exit(-1);
    }

    // Free server_addr since we are done with it
    freeaddrinfo(server_addr);

    // Handle -d argument to run as daemon
    if ((argc == 1) && (strcmp(argv[1], "-d")))
    {
        start_daemon();
    }

    while (true)
    {
        // Listen for connections
        if (listen(socket_fd, 10) < 0)
        {
            syslog(LOG_ERR, "Unable to listen for connection: errno %d", errno);
            cleanup_and_exit(-1);
        }
    
        // Accept connection
        client_fd = accept(socket_fd, (struct sockaddr*)&client_addr, &client_addr_size);
        if (client_fd < 0)
        {
            syslog(LOG_ERR, "Unable to accept connection: errno %d", errno);
            cleanup_and_exit(-1);
        }

        // Log client IP address
        inet_ntop(client_addr.ss_family, &((struct sockaddr_in*)&client_addr)->sin_addr, client_ip, sizeof(client_ip));
        syslog(LOG_INFO, "Accepted connection from %s", client_ip);

        // Open file for recv/send messages
        file_ptr = fopen(FILE_PATH, "ar");
        if (file_ptr == NULL)
        {
            syslog(LOG_ERR, "Unable to open "FILE_PATH" , errno %d", errno);
            cleanup_and_exit(-1);
        }

        // Receive and send data
        receive_data(file_ptr, client_fd);
        send_data(file_ptr, client_fd);
    
        // Close connection and log close message
        fclose(file_ptr);
        close(client_fd);
        syslog(LOG_INFO, "Closed connection.");// from %s", client_addr.sa_data);
    }

    return 0;
}
