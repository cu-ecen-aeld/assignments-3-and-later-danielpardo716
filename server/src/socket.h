#ifndef SOCKET_H
#define SOCKET_H

#include <arpa/inet.h>

struct client_info {
    int client_fd;
    char client_ip[INET_ADDRSTRLEN];
};

void socket_init();
void socket_start_listening();
void socket_wait_for_connection(struct client_info* client);
void socket_cleanup();

#endif