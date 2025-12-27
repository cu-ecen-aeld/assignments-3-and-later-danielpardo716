#include <stdlib.h>
#include <syslog.h>

#include "cleanup.h"
#include "socket.h"
#include "file.h"
#include "thread.h"
#include "timer.h"

void cleanup_and_exit(int exit_code)
{
    socket_cleanup();
    file_cleanup();
    thread_remove_all();
    timer_cleanup();
    closelog();
    exit(exit_code);
}