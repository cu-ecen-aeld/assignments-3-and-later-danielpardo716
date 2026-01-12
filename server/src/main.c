#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>
#include <signal.h>
#include <unistd.h>

#include "cleanup.h"
#include "socket.h"
#include "thread.h"
#include "file.h"
#include "timer.h"

static void signal_handler(int signal_number)
{
    if ((signal_number == SIGINT) || (signal_number == SIGTERM))
    {
        syslog(LOG_INFO, "Caught signal %d, exiting", signal_number);
        cleanup_and_exit(EXIT_SUCCESS);
    }
}

static void init_signal_handler()
{
    struct sigaction signal = { .sa_handler = signal_handler };
    if (sigaction(SIGINT, &signal, NULL) != 0)
    {
        syslog(LOG_ERR, "Unable to register SIGINT: %s", strerror(errno));
        cleanup_and_exit(EXIT_FAILURE);
    }
    if (sigaction(SIGTERM, &signal, NULL) != 0)
    {
        syslog(LOG_ERR, "Unable to register SIGTERM: %s", strerror(errno));
        cleanup_and_exit(EXIT_FAILURE);
    }
}

int main(int argc, char* argv[])
{
    // Open logger
    openlog(NULL, 0, LOG_USER);

    // Setup signal handler
    init_signal_handler();

    // Setup socket
    socket_init();
    
    // Handle -d argument to run as daemon
    if ((argc == 2) && (strcmp(argv[1], "-d") == 0) && (daemon(0, 0) < 0))
    {
        syslog(LOG_ERR, "Failed to create daemon: %s", strerror(errno));
    }

#ifndef USE_AESD_CHAR_DEVICE
    // Setup timer (only if using file, not char device)
    timer_init();
#endif
    
    // Listen for connections
    socket_start_listening();

    while (true)
    {
        // Accept connection
        struct client_info client = {0};
        socket_wait_for_connection(&client);

        // Add new thread to the linked list
        thread_spawn(client.client_fd, client.client_ip);

        // Check if any threads have completed
        thread_cleanup_completed();
    }

    return 0;
}
