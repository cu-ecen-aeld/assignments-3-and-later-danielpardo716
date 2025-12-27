#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>
#include <pthread.h>

#include "file.h"
#include "cleanup.h"

#define FILE_PATH           "/var/tmp/aesdsocketdata"

static FILE* file_ptr = NULL;
static pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;

void file_append(const char* data, size_t length)
{
    pthread_mutex_lock(&file_mutex);
    if ((file_ptr = fopen(FILE_PATH, "a")) == NULL)
    {
        syslog(LOG_ERR, "Unable to open "FILE_PATH" , %s", strerror(errno));
        cleanup_and_exit(EXIT_FAILURE);
    }
    if (fwrite(data, 1, length, file_ptr) != length)
    {
        syslog(LOG_ERR, "Failed to write to file: %s", strerror(errno));
        cleanup_and_exit(EXIT_FAILURE);
    }
    fflush(file_ptr);
    fclose(file_ptr);
    file_ptr = NULL;
    pthread_mutex_unlock(&file_mutex);
}

long file_read(char** buffer)
{
    pthread_mutex_lock(&file_mutex);
    if ((file_ptr = fopen(FILE_PATH, "r")) == NULL)
    {
        syslog(LOG_ERR, "Unable to open "FILE_PATH" , %s", strerror(errno));
        cleanup_and_exit(EXIT_FAILURE);
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
        cleanup_and_exit(EXIT_FAILURE);
    }

    // Read file contents into buffer
    if (fread(*buffer, 1, file_size, file_ptr) < file_size)
    {
        syslog(LOG_ERR, "Failed to read from file: %s", strerror(errno));
        free(*buffer);
        cleanup_and_exit(EXIT_FAILURE);
    }

    fclose(file_ptr);
    file_ptr = NULL;
    pthread_mutex_unlock(&file_mutex);
    return file_size;
}

void file_cleanup()
{
    pthread_mutex_lock(&file_mutex);
    if (file_ptr != NULL)
    {
        fclose(file_ptr);
        file_ptr = NULL;
    }
    pthread_mutex_unlock(&file_mutex);

    pthread_mutex_destroy(&file_mutex);
    
    remove(FILE_PATH);
}