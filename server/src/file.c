#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>
#include <pthread.h>
#include <fcntl.h>

#include "file.h"
#include "cleanup.h"
#include "../../aesd-char-driver/aesd_ioctl.h"

#ifdef USE_AESD_CHAR_DEVICE
    #define FILE_PATH           "/dev/aesdchar"
#else
    #define FILE_PATH           "/var/tmp/aesdsocketdata"
#endif

#define DEFAULT_FILE_SIZE   1024L

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

    // Allocate buffer for file contents
    size_t buffer_size = DEFAULT_FILE_SIZE;
    size_t bytes_written = 0;
    *buffer = (char*)malloc(buffer_size);
    if (*buffer == NULL)
    {
        syslog(LOG_ERR, "Failed to allocate memory for file read buffer: %s", strerror(errno));
        cleanup_and_exit(EXIT_FAILURE);
    }

    // Read file contents into buffer line by line
    while (fgets(*buffer + bytes_written, buffer_size - bytes_written, file_ptr) != NULL)
    {
        bytes_written = strlen(*buffer);
        
        // Check if we need to realloc buffer
        if (bytes_written >= buffer_size - 1)
        {
            buffer_size *= 2;
            char* temp = realloc(*buffer, buffer_size);
            if (temp == NULL)
            {
                syslog(LOG_ERR, "Failed to reallocate memory for file read buffer: %s", strerror(errno));
                free(*buffer);
                cleanup_and_exit(EXIT_FAILURE);
            }
            *buffer = temp;
        }
    }

    if (errno != 0)
    {
        syslog(LOG_ERR, "Failed to read from file: %s", strerror(errno));
        free(*buffer);
        cleanup_and_exit(EXIT_FAILURE);
    }

    fclose(file_ptr);
    file_ptr = NULL;
    pthread_mutex_unlock(&file_mutex);
    return strlen(*buffer);
}

int file_ioctl(uint32_t write_cmd, uint32_t write_cmd_offset)
{
    struct aesd_seekto seekto = {
        .write_cmd = write_cmd,
        .write_cmd_offset = write_cmd_offset
    };
    int fd = fileno(file_ptr);
    return ioctl(fd, AESDCHAR_IOCSEEKTO, &seekto);
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