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
#ifndef USE_AESD_CHAR_DEVICE
    timer_cleanup();
#endif
    closelog();
    exit(exit_code);
}