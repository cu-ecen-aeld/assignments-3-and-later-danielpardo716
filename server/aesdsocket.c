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
#include <time.h>

#define BUFFER_MAX_LENGTH   256
#define LISTEN_BACKLOG      10
#define SERVER_PORT         "9000"
#define FILE_PATH           "/var/tmp/aesdsocketdata"

static FILE* file_ptr = NULL;
static int socket_fd = -1;

// Linked list for threads
struct thread_node {
    pthread_t thread_id;
    bool completed;
    int client_fd;
    char* client_ip;
    SLIST_ENTRY(thread_node) entries;
};
SLIST_HEAD(thread_list, thread_node);
static struct thread_list head = SLIST_HEAD_INITIALIZER(head);
static pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;
static timer_t timer_id;

static void cleanup_thread(struct thread_node* node)
{
    pthread_join(node->thread_id, NULL);
    SLIST_REMOVE(&head, node, thread_node, entries);
    shutdown(node->client_fd, SHUT_RDWR);
    close(node->client_fd);
    syslog(LOG_INFO, "Closed connection from %s", node->client_ip);
    free(node->client_ip);
    free(node);
}

static void remove_all_threads()
{
    struct thread_node *cur, *next;
    for (cur = SLIST_FIRST(&head); cur != NULL; cur = next)
    {
        next = SLIST_NEXT(cur, entries);
        cleanup_thread(cur);
    }
}

static void cleanup_and_exit(int exit_code)
{
    if (socket_fd != -1)
    {
        shutdown(socket_fd, SHUT_RDWR);
        close(socket_fd);
    }
    if (file_ptr != NULL)
    {
        fclose(file_ptr);
    }
    remove_all_threads();
    timer_delete(timer_id);
    pthread_mutex_destroy(&file_mutex);
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

static void init_signal_handler()
{
    struct sigaction signal = { .sa_handler = signal_handler };
    if (sigaction(SIGINT, &signal, NULL) != 0)
    {
        syslog(LOG_ERR, "Unable to register SIGINT: %s", strerror(errno));
        cleanup_and_exit(-1);
    }
    if (sigaction(SIGTERM, &signal, NULL) != 0)
    {
        syslog(LOG_ERR, "Unable to register SIGTERM: %s", strerror(errno));
        cleanup_and_exit(-1);
    }
}

static void init_socket()
{
    // Get address (port 9000)
    struct addrinfo hints = {
        .ai_family = AF_UNSPEC,
        .ai_socktype = SOCK_STREAM,
        .ai_flags = AI_PASSIVE,
    };
    struct addrinfo* server_addr;
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

static void file_append(const char* data, size_t length)
{
    pthread_mutex_lock(&file_mutex);
    if ((file_ptr = fopen(FILE_PATH, "a")) == NULL)
    {
        syslog(LOG_ERR, "Unable to open "FILE_PATH" , %s", strerror(errno));
        cleanup_and_exit(-1);
    }
    if (fwrite(data, 1, length, file_ptr) != length)
    {
        syslog(LOG_ERR, "Failed to write to file: %s", strerror(errno));
        cleanup_and_exit(-1);
    }
    fflush(file_ptr);
    fclose(file_ptr);
    file_ptr = NULL;
    pthread_mutex_unlock(&file_mutex);
}

static long file_read(char** buffer)
{
    pthread_mutex_lock(&file_mutex);
    if ((file_ptr = fopen(FILE_PATH, "r")) == NULL)
    {
        syslog(LOG_ERR, "Unable to open "FILE_PATH" , %s", strerror(errno));
        cleanup_and_exit(-1);
    }

    // Determine file size
    fseek(file_ptr, 0, SEEK_END);
    long file_size = ftell(file_ptr);
    rewind(file_ptr);

    // Allocate buffer for file contents
    *buffer = (char*)malloc(file_size + 1);
    if (*buffer == NULL)
    {
        syslog(LOG_ERR, "Failed to allocate memory for file read buffer: %s", strerror(errno));
        cleanup_and_exit(-1);
    }

    // Read file contents into buffer
    if (fread(*buffer, 1, file_size, file_ptr) < file_size)
    {
        syslog(LOG_ERR, "Failed to read from file: %s", strerror(errno));
        free(*buffer);
        cleanup_and_exit(-1);
    }

    fclose(file_ptr);
    file_ptr = NULL;
    pthread_mutex_unlock(&file_mutex);
    return file_size;
}

static void* process_data(void* arg)
{
    struct thread_node* node = (struct thread_node*)arg;

    char recv_buffer[BUFFER_MAX_LENGTH];
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

    while ((bytes_received = recv(node->client_fd, recv_buffer, sizeof(recv_buffer), 0)) > 0)
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
            line_buffer[line_len] = recv_buffer[i];
            ++line_len;

            if (recv_buffer[i] == '\n')
            {
                // If newline received, append to file
                file_append(line_buffer, line_len);

                // Read file and send to client
                char* read_buffer = NULL;
                long buffer_size = file_read(&read_buffer);
                if (send(node->client_fd, read_buffer, buffer_size, 0) != buffer_size)
                {
                    syslog(LOG_ERR, "Failed to send any bytes: %s", strerror(errno));
                    cleanup_and_exit(-1);
                }
                free(read_buffer);
                goto process_data_end;
            }
        }
    }

process_data_end:
    free(line_buffer);
    node->completed = true;
    return NULL;
}

static void spawn_thread(int client_fd, char* client_ip)
{
    // Allocate and init node
    struct thread_node* node = malloc(sizeof(struct thread_node));
    if (node == NULL)
    {
        syslog(LOG_ERR, "Failed to allocate memory for thread node: %s", strerror(errno));
        cleanup_and_exit(-1);
    }
    node->completed = false;
    node->client_fd = client_fd;
    node->client_ip = strdup(client_ip);

    // Create thread
    int ret = 0;
    if ((ret = pthread_create(&(node->thread_id), NULL, process_data, node)) != 0)
    {
        syslog(LOG_ERR, "Unable to create thread: %s", strerror(ret));
        cleanup_and_exit(-1);
    }

    // Add to linked list
    SLIST_INSERT_HEAD(&head, node, entries);

    // Log client IP address
    syslog(LOG_INFO, "Spawned thread %lu.", node->thread_id);
    syslog(LOG_INFO, "Accepted connection from %s", client_ip);
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
            cleanup_thread(cur);
        }
    }
}

static void timer_handler(union sigval signal_value)
{
    (void)signal_value;  // Unused parameter

    // Get timestamp
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    char time_buffer[64];
    strftime(time_buffer, sizeof(time_buffer), "timestamp: %a, %d %b %Y %T %z:\n", tm_info);

    // Append timestamp to file
    file_append(time_buffer, strlen(time_buffer));
}

void init_timer()
{
    // Create timer
    struct sigevent sev = {
        .sigev_notify = SIGEV_THREAD,
        .sigev_notify_function = timer_handler,
    };
    if (timer_create(CLOCK_MONOTONIC, &sev, &timer_id) != 0)
    {
        syslog(LOG_ERR, "Unable to create timer: %s", strerror(errno));
        cleanup_and_exit(-1);
    }

    // Set timer period
    struct itimerspec its = {
        .it_interval.tv_sec = 10,
        .it_value.tv_sec = 10,
    };
    if (timer_settime(timer_id, 0, &its, NULL) != 0)
    {
        syslog(LOG_ERR, "Unable to set timer: %s", strerror(errno));
        cleanup_and_exit(-1);
    }
}

int main(int argc, char* argv[])
{
    // Open logger
    openlog(NULL, 0, LOG_USER);

    // Setup signal handler
    init_signal_handler();

    // Setup socket
    init_socket();
    
    // Handle -d argument to run as daemon
    if ((argc == 2) && (strcmp(argv[1], "-d") == 0) && (daemon(0, 0) < 0))
    {
        syslog(LOG_ERR, "Failed to create daemon: %s", strerror(errno));
    }

    // Setup timer
    init_timer();
    
    // Listen for connections
    if (listen(socket_fd, LISTEN_BACKLOG) < 0)
    {
        syslog(LOG_ERR, "Unable to listen for connection: %s", strerror(errno));
        cleanup_and_exit(-1);
    }

    while (true)
    {
        // Accept connection
        int client_fd;
        struct sockaddr_storage client_addr;
        socklen_t client_addr_size = sizeof(client_addr);
        if ((client_fd = accept(socket_fd, (struct sockaddr*)&client_addr, &client_addr_size)) < 0)
        {
            syslog(LOG_ERR, "Unable to accept connection: %s", strerror(errno));
            cleanup_and_exit(-1);
        }
        
        // Add new thread to the linked list
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(client_addr.ss_family, &((struct sockaddr_in*)&client_addr)->sin_addr, client_ip, sizeof(client_ip));
        spawn_thread(client_fd, client_ip);

        // Check if any threads have completed
        cleanup_completed_threads();
    }

    return 0;
}
