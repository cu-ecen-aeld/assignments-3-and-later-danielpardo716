#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>
#include <signal.h>
#include <time.h>

#include "timer.h"
#include "file.h"
#include "cleanup.h"

static timer_t timer_id;

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

void timer_init()
{
    // Create timer
    struct sigevent sev = {
        .sigev_notify = SIGEV_THREAD,
        .sigev_notify_function = timer_handler,
    };
    if (timer_create(CLOCK_MONOTONIC, &sev, &timer_id) != 0)
    {
        syslog(LOG_ERR, "Unable to create timer: %s", strerror(errno));
        cleanup_and_exit(EXIT_FAILURE);
    }

    // Set timer period
    struct itimerspec its = {
        .it_interval.tv_sec = 10,
        .it_value.tv_sec = 10,
    };
    if (timer_settime(timer_id, 0, &its, NULL) != 0)
    {
        syslog(LOG_ERR, "Unable to set timer: %s", strerror(errno));
        cleanup_and_exit(EXIT_FAILURE);
    }
}

void timer_cleanup()
{
    timer_delete(timer_id);
}