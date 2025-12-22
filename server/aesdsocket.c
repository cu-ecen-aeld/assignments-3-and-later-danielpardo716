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
#include <sys/queue.h>
#include <pthread.h>

#define BUFFER_MAX_LENGTH   256
#define LISTEN_BACKLOG      10
#define SERVER_PORT         "9000"
#define FILE_PATH           "/var/tmp/aesdsocketdata"

static FILE* file_ptr = NULL;
static int socket_fd = -1;
static int client_fd = -1;

// Linked list for threads
struct thread_node {
    pthread_t thread_id;
    bool completed;
    SLIST_ENTRY(thread_node) entries;
};
SLIST_HEAD(thread_list, thread_node);
static struct thread_list head = SLIST_HEAD_INITIALIZER(head);
static pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;

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

    remove(FILE_PATH);
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

static void init_signal_handler(struct sigaction* signal_ptr)
{
    memset(signal_ptr, 0, sizeof(struct sigaction));
    signal_ptr->sa_handler = signal_handler;
    if (sigaction(SIGINT, signal_ptr, NULL) != 0)
    {
        syslog(LOG_ERR, "Unable to register SIGINT: %s", strerror(errno));
        cleanup_and_exit(-1);
    }
    if (sigaction(SIGTERM, signal_ptr, NULL) != 0)
    {
        syslog(LOG_ERR, "Unable to register SIGTERM: %s", strerror(errno));
        cleanup_and_exit(-1);
    }
}

static void init_socket()
{
    struct addrinfo* server_addr;
    struct addrinfo hints;

    // Get address (port 9000)
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    if (getaddrinfo(NULL, SERVER_PORT, &hints, &server_addr) < 0)
    {
        syslog(LOG_ERR, "Unable to get addr info: %s", strerror(errno));
        freeaddrinfo(server_addr);
        cleanup_and_exit(-1);
    }

    // Create IPv4 TCP/IP socket
    if ((socket_fd = socket(server_addr->ai_family, server_addr->ai_socktype, server_addr->ai_protocol)) < 0)
    {
        syslog(LOG_ERR, "Unable to create socket: %s", strerror(errno));
        cleanup_and_exit(-1);
    }

    // Set socket to be reusable
    int optval = 1;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0)
    {
        syslog(LOG_ERR, "Unable to set socket options: %s", strerror(errno));
        freeaddrinfo(server_addr);
        cleanup_and_exit(-1);
    }

    // Bind address to socket
    if (bind(socket_fd, server_addr->ai_addr, server_addr->ai_addrlen) < 0)
    {
        syslog(LOG_ERR, "Unable to bind socket: %s", strerror(errno));
        freeaddrinfo(server_addr);
        cleanup_and_exit(-1);
    }

    // Free server_addr since we are done with it
    freeaddrinfo(server_addr);
}

void* process_data(void* param)
{
    struct thread_node* node = (struct thread_node*)param;
    pthread_mutex_lock(&file_mutex);

    // Open file to append to
    if ((file_ptr = fopen(FILE_PATH, "a+")) == NULL)
    {
        syslog(LOG_ERR, "Unable to open "FILE_PATH" , %s", strerror(errno));
        cleanup_and_exit(-1);
    }

    char buffer[BUFFER_MAX_LENGTH];
    char* line_buffer = NULL;
    size_t line_capacity = 256;
    size_t line_len = 0;
    ssize_t bytes_received = 0;
    
    // Allocate initial line buffer
    line_buffer = (char*)malloc(line_capacity);
    if (line_buffer == NULL)
    {
        syslog(LOG_ERR, "Failed to allocate memory for line buffer: %s", strerror(errno));
        cleanup_and_exit(-1);
    }

    while ((bytes_received = recv(client_fd, buffer, sizeof(buffer), 0)) > 0)
    {   
        // Check for receiving errors
        if (bytes_received < 0)
        {
            syslog(LOG_ERR, "Failed to receive any bytes: %s", strerror(errno));
            free(line_buffer);
            cleanup_and_exit(-1);
        }

        // Scan for newlines and append to file
        for (int i = 0; i < bytes_received; ++i)
        {
            // Grow line buffer if needed
            if (line_len >= (line_capacity - 1))
            {
                line_capacity *= 2;
                char* tmp = (char*)realloc(line_buffer, line_capacity);
                if (tmp == NULL)
                {
                    syslog(LOG_ERR, "Failed to reallocate memory for line buffer: %s", strerror(errno));
                    free(line_buffer);
                    cleanup_and_exit(-1);
                }
                line_buffer = tmp;
            }

            // Copy bytes to file line buffer
            line_buffer[line_len] = buffer[i];
            ++line_len;

            if (buffer[i] == '\n')
            {
                // If newline received, append to file
                if (fwrite(line_buffer, 1, line_len, file_ptr) != line_len)
                {
                    syslog(LOG_ERR, "Failed to write to file: %s", strerror(errno));
                    free(line_buffer);
                    cleanup_and_exit(-1);
                }
                fflush(file_ptr);

                // Read file and send to client
                rewind(file_ptr);
                while (fgets(buffer, sizeof(buffer), file_ptr))
                {
                    size_t len = strlen(buffer);
                    ssize_t bytes_sent = 0;                    
                    while (bytes_sent < (ssize_t)len)
                    {
                        ssize_t n = send(client_fd, (buffer + bytes_sent), (len - bytes_sent), 0);
                        if (n <= 0)
                        {
                            syslog(LOG_ERR, "Failed to send any bytes: %s", strerror(errno));
                            free(line_buffer);
                            cleanup_and_exit(-1);
                        }
                        bytes_sent += n;
                    }
                }
                syslog(LOG_INFO, "Done sending file");
                fclose(file_ptr);
                file_ptr = NULL;
                free(line_buffer);
                node->completed = true;
                pthread_mutex_unlock(&file_mutex);
                return NULL;
            }
        }
    }
    return NULL;
}

static void spawn_thread()
{
    // Allocate and init node
    struct thread_node* node = malloc(sizeof(struct thread_node));
    if (node == NULL)
    {
        syslog(LOG_ERR, "Failed to allocate memory for thread node: %s", strerror(errno));
        cleanup_and_exit(-1);
    }
    node->completed = false;

    // Create thread
    int ret = 0;
    if ((ret = pthread_create(&(node->thread_id), NULL, process_data, node)) != 0)
    {
        syslog(LOG_ERR, "Unable to create thread: %s", strerror(ret));
        cleanup_and_exit(-1);
    }

    // Add to linked list
    SLIST_INSERT_HEAD(&head, node, entries);
}

void cleanup_completed_threads()
{
    // Safely iterate through linked list
    struct thread_node *cur, *next;
    for (cur = SLIST_FIRST(&head); cur != NULL; cur = next)
    {
        next = SLIST_NEXT(cur, entries);
        if (cur->completed)
        {
            // If thread is finished, cleanup resources
            int ret = 0;
            if ((ret = pthread_join(cur->thread_id, NULL) != 0))
            {
                syslog(LOG_ERR, "Unable to join thread: %s", strerror(ret));
                cleanup_and_exit(-1);
            }
            SLIST_REMOVE(&head, cur, thread_node, entries);
            free(cur);
        }
    }
}

int main(int argc, char* argv[])
{
    // Open logger
    openlog(NULL, 0, LOG_USER);

    // Setup signal handler
    struct sigaction signal;
    init_signal_handler(&signal);

    // Setup socket
    init_socket();
    
    // Handle -d argument to run as daemon
    if ((argc == 2) && (strcmp(argv[1], "-d") == 0) && (daemon(0, 0) < 0))
    {
        syslog(LOG_ERR, "Failed to create daemon: %s", strerror(errno));
    }
    
    // Listen for connections
    if (listen(socket_fd, LISTEN_BACKLOG) < 0)
    {
        syslog(LOG_ERR, "Unable to listen for connection: %s", strerror(errno));
        cleanup_and_exit(-1);
    }

    while (true)
    {
        // Accept connection
        struct sockaddr_storage client_addr;
        socklen_t client_addr_size = sizeof(client_addr);
        char client_ip[INET_ADDRSTRLEN];
        if ((client_fd = accept(socket_fd, (struct sockaddr*)&client_addr, &client_addr_size)) < 0)
        {
            syslog(LOG_ERR, "Unable to accept connection: %s", strerror(errno));
            cleanup_and_exit(-1);
        }

        // Log client IP address
        inet_ntop(client_addr.ss_family, &((struct sockaddr_in*)&client_addr)->sin_addr, client_ip, sizeof(client_ip));
        syslog(LOG_INFO, "Accepted connection from %s", client_ip);

        // Add new thread to the linked list
        spawn_thread();

        // Check if any threads have completed
        cleanup_completed_threads();
    
        // Close connection and log close message
        close(client_fd);
        syslog(LOG_INFO, "Closed connection from %s", client_ip);
    }

    return 0;
}
