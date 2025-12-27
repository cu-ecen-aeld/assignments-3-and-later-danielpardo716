#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>

#include "socket.h"
#include "cleanup.h"

#define SERVER_PORT         "9000"
#define LISTEN_BACKLOG      10

static int socket_fd = -1;

void socket_init()
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
        cleanup_and_exit(EXIT_FAILURE);
    }

    // Create IPv4 TCP/IP socket
    if ((socket_fd = socket(server_addr->ai_family, server_addr->ai_socktype, server_addr->ai_protocol)) < 0)
    {
        syslog(LOG_ERR, "Unable to create socket: %s", strerror(errno));
        cleanup_and_exit(EXIT_FAILURE);
    }

    // Set socket to be reusable
    int optval = 1;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0)
    {
        syslog(LOG_ERR, "Unable to set socket options: %s", strerror(errno));
        freeaddrinfo(server_addr);
        cleanup_and_exit(EXIT_FAILURE);
    }

    // Bind address to socket
    if (bind(socket_fd, server_addr->ai_addr, server_addr->ai_addrlen) < 0)
    {
        syslog(LOG_ERR, "Unable to bind socket: %s", strerror(errno));
        freeaddrinfo(server_addr);
        cleanup_and_exit(EXIT_FAILURE);
    }

    // Free server_addr since we are done with it
    freeaddrinfo(server_addr);
}

void socket_start_listening()
{
    if (listen(socket_fd, LISTEN_BACKLOG) < 0)
    {
        syslog(LOG_ERR, "Unable to listen for connection: %s", strerror(errno));
        cleanup_and_exit(EXIT_FAILURE);
    }
}

void socket_wait_for_connection(struct client_info* client)
{
    struct sockaddr_storage client_addr;
    socklen_t client_addr_size = sizeof(client_addr);
    if ((client->client_fd = accept(socket_fd, (struct sockaddr*)&client_addr, &client_addr_size)) < 0)
    {
        syslog(LOG_ERR, "Unable to accept connection: %s", strerror(errno));
        cleanup_and_exit(EXIT_FAILURE);
    }

    // Get client IP address
    inet_ntop(client_addr.ss_family, &((struct sockaddr_in*)&client_addr)->sin_addr, client->client_ip, sizeof(client->client_ip));
}

void socket_cleanup()
{
    if (socket_fd != -1)
    {
        shutdown(socket_fd, SHUT_RDWR);
        close(socket_fd);
    }
}