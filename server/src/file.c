#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>
#include <pthread.h>
#include <fcntl.h>

#include "file.h"
#include "cleanup.h"

#ifdef USE_AESD_CHAR_DEVICE
    #define FILE_PATH           "/dev/aesdchar"
#else
    #define FILE_PATH           "/var/tmp/aesdsocketdata"
#endif

static FILE* file_ptr = NULL;
static pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;

static FILE* file_open(const char* mode)
{
    int flags = (strcmp(mode, "a") == 0) ? (O_RDWR | O_APPEND) : O_RDONLY;
    int fd = open(FILE_PATH, flags);
    if (fd < 0)
    {
        syslog(LOG_ERR, "Unable to get fd for "FILE_PATH": %s", strerror(errno));
    }
    return fdopen(fd, mode);
}

void file_append(const char* data, size_t length)
{
    pthread_mutex_lock(&file_mutex);
    if ((file_ptr = file_open("a")) == NULL)
    {
        syslog(LOG_ERR, "Unable to open "FILE_PATH", %s", strerror(errno));
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
    if ((file_ptr = file_open("r")) == NULL)
    {
        syslog(LOG_ERR, "Unable to open "FILE_PATH" , %s", strerror(errno));
        cleanup_and_exit(EXIT_FAILURE);
    }

    // Determine file size
    fseek(file_ptr, 0, SEEK_END);
    long file_size = ftell(file_ptr);
    rewind(file_ptr);
    syslog(LOG_DEBUG, "File size: %ld", file_size);

    // Allocate buffer for file contents
    *buffer = (char*)malloc(file_size);
    if (*buffer == NULL)
    {
        syslog(LOG_ERR, "Failed to allocate memory for file read buffer: %s", strerror(errno));
        cleanup_and_exit(EXIT_FAILURE);
    }

    // Read file contents into buffer
    size_t bytes_read = fread(*buffer, 1, file_size, file_ptr);
    if (bytes_read < file_size)
    {
        syslog(LOG_ERR, "Failed to read from file: %s. File size: %ld, read bytes: %ld", strerror(errno), file_size, bytes_read);
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
    
#ifndef USE_AESD_CHAR_DEVICE
    // Remove the file only if not using char device
    remove(FILE_PATH);
#endif
}