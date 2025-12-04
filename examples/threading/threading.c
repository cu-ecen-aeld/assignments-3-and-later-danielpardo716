
#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

// Optional: use these functions to add debug or error prints to your application
#define DEBUG_LOG(msg,...)
//#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

#define MILLISECONDS_TO_MICROSECONDS (1000)

void* threadfunc(void* thread_param)
{
    struct thread_data* thread_func_args = (struct thread_data *) thread_param;

    // Wait to acquire mutex
    usleep(thread_func_args->wait_to_obtain_ms * MILLISECONDS_TO_MICROSECONDS);

    // Obtain mutex
    int ret = pthread_mutex_lock(thread_func_args->mutex);
    if (ret != 0)
    {
        ERROR_LOG("Failed to lock mutex: %d", ret);
        thread_func_args->thread_complete_success = false;
    }
    else
    {
        // Wait to release mutex
        usleep(thread_func_args->wait_to_release_ms * MILLISECONDS_TO_MICROSECONDS);
    }

    // Release mutex
    ret = pthread_mutex_unlock(thread_func_args->mutex);
    if (ret != 0)
    {
        ERROR_LOG("Failed to unlock mutex: %d", ret);
        thread_func_args->thread_complete_success = false;
    }
    else 
    {
        thread_func_args->thread_complete_success = true;
    }

    return thread_param;
}

bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex, int wait_to_obtain_ms, int wait_to_release_ms)
{
    // Allocate memory for thread data
    struct thread_data* thread_param = (struct thread_data*)malloc(sizeof(struct thread_data));

    // Setup mutex and wait arguments
    thread_param->mutex = mutex;
    thread_param->wait_to_obtain_ms = wait_to_obtain_ms;
    thread_param->wait_to_release_ms = wait_to_release_ms;

    // Create thread with default attributes and thread data
    int ret = pthread_create(thread, NULL, threadfunc, thread_param);
    if (ret != 0)
    {
        ERROR_LOG("Failed to create thread: %d", ret);
    }

    return (ret == 0);
}
