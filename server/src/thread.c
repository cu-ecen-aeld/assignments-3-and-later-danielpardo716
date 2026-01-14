#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/queue.h>

#include "thread.h"
#include "file.h"
#include "cleanup.h"

#define BUFFER_MAX_LENGTH   256

// Linked list for threads
SLIST_HEAD(thread_list, thread_node);
static struct thread_list head = SLIST_HEAD_INITIALIZER(head);

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
        cleanup_and_exit(EXIT_FAILURE);
    }

    while ((bytes_received = recv(node->client_fd, recv_buffer, sizeof(recv_buffer), 0)) > 0)
    {   
        // Check for receiving errors
        if (bytes_received < 0)
        {
            syslog(LOG_ERR, "Failed to receive any bytes: %s", strerror(errno));
            free(line_buffer);
            cleanup_and_exit(EXIT_FAILURE);
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
                    cleanup_and_exit(EXIT_FAILURE);
                }
                line_buffer = tmp;
            }

            // Copy bytes to file line buffer
            line_buffer[line_len] = recv_buffer[i];
            ++line_len;

            if (recv_buffer[i] == '\n')
            {
                // Check for ioctl command
                if (strncmp(line_buffer, FILE_IOCTL_SEEKTO_CMD, FILE_IOCTL_SEEKTO_CMD_LEN) == 0)
                {
                    uint32_t write_cmd = 0;
                    uint32_t write_cmd_offset = 0;
                    if (sscanf(line_buffer + FILE_IOCTL_SEEKTO_CMD_LEN, "%d,%d", &write_cmd, &write_cmd_offset) != 2)
                    {
                        syslog(LOG_ERR, "Failed to parse ioctl command from client: %s. Received %s", strerror(errno), line_buffer);
                        free(line_buffer);
                        cleanup_and_exit(EXIT_FAILURE);
                    }
                    if (file_ioctl(write_cmd, write_cmd_offset) != 0)
                    {
                        syslog(LOG_ERR, "Failed to perform ioctl command from client: %s", strerror(errno));
                        free(line_buffer);
                        cleanup_and_exit(EXIT_FAILURE);
                    }
                    goto process_data_end;
                }
                else
                {
                    // If newline received, append to file
                    file_append(line_buffer, line_len);
    
                    // Read file and send to client
                    char* read_buffer = NULL;
                    long buffer_size = file_read(&read_buffer);
                    if (send(node->client_fd, read_buffer, buffer_size, 0) != buffer_size)
                    {
                        syslog(LOG_ERR, "Failed to send any bytes: %s", strerror(errno));
                        free(line_buffer);
                        if (read_buffer != NULL) free(read_buffer);
                        cleanup_and_exit(EXIT_FAILURE);
                    }
                    free(read_buffer);
                    goto process_data_end;
                }
            }
        }
    }

process_data_end:
    free(line_buffer);
    node->completed = true;
    return NULL;
}

void thread_spawn(int client_fd, char* client_ip)
{
    // Allocate and init node
    struct thread_node* node = malloc(sizeof(struct thread_node));
    if (node == NULL)
    {
        syslog(LOG_ERR, "Failed to allocate memory for thread node: %s", strerror(errno));
        cleanup_and_exit(EXIT_FAILURE);
    }
    node->completed = false;
    node->client_fd = client_fd;
    node->client_ip = strdup(client_ip);

    // Create thread
    int ret = 0;
    if ((ret = pthread_create(&(node->thread_id), NULL, process_data, node)) != 0)
    {
        syslog(LOG_ERR, "Unable to create thread: %s", strerror(ret));
        cleanup_and_exit(EXIT_FAILURE);
    }

    // Add to linked list
    SLIST_INSERT_HEAD(&head, node, entries);

    // Log client IP address
    syslog(LOG_INFO, "Spawned thread %lu.", node->thread_id);
    syslog(LOG_INFO, "Accepted connection from %s", client_ip);
}

static void thread_cleanup(struct thread_node* node)
{
    pthread_join(node->thread_id, NULL);
    SLIST_REMOVE(&head, node, thread_node, entries);
    shutdown(node->client_fd, SHUT_RDWR);
    close(node->client_fd);
    syslog(LOG_INFO, "Closed connection from %s", node->client_ip);
    free(node->client_ip);
    free(node);
}

void thread_remove_all()
{
    struct thread_node *cur, *next;
    for (cur = SLIST_FIRST(&head); cur != NULL; cur = next)
    {
        next = SLIST_NEXT(cur, entries);
        thread_cleanup(cur);
    }
}

void thread_cleanup_completed()
{
    // Safely iterate through linked list
    struct thread_node *cur, *next;
    for (cur = SLIST_FIRST(&head); cur != NULL; cur = next)
    {
        next = SLIST_NEXT(cur, entries);
        if (cur->completed)
        {
            thread_cleanup(cur);
        }
    }
}