#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>

#include "file.h"
#include "cleanup.h"
#include "../../aesd-char-driver/aesd_ioctl.h"

#ifdef USE_AESD_CHAR_DEVICE
    #define FILE_PATH           "/dev/aesdchar"
#else
    #define FILE_PATH           "/var/tmp/aesdsocketdata"
#endif

#define DEFAULT_FILE_SIZE   1024L

static int fd = -1;
static pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;

void file_open()
{
    pthread_mutex_lock(&file_mutex);
    fd = open(FILE_PATH, (O_RDWR | O_APPEND | O_SYNC));
    if (fd < 0)
    {
        syslog(LOG_ERR, "Unable to get fd for "FILE_PATH": %s", strerror(errno));
    }
    else
    {
        syslog(LOG_INFO, "Opened file "FILE_PATH" with fd %d", fd);
    }
    pthread_mutex_unlock(&file_mutex);
}

void file_append(const char* data, size_t length)
{
    pthread_mutex_lock(&file_mutex);
    if (fd < 0)
    {
        syslog(LOG_ERR, "File not opened for append operation");
        cleanup_and_exit(EXIT_FAILURE);
    }

    if (write(fd, data, length) != length)
    {
        syslog(LOG_ERR, "Failed to write to file: %s", strerror(errno));
        cleanup_and_exit(EXIT_FAILURE);
    }
    pthread_mutex_unlock(&file_mutex);
}

long file_read(char** buffer)
{
    pthread_mutex_lock(&file_mutex);
    if (fd < 0)
    {
        syslog(LOG_ERR, "File not opened for read operation");
        cleanup_and_exit(EXIT_FAILURE);
    }

    // Allocate buffer for file contents
    size_t buffer_size = DEFAULT_FILE_SIZE;
    *buffer = (char*)malloc(buffer_size);
    if (*buffer == NULL)
    {
        syslog(LOG_ERR, "Failed to allocate memory for file read buffer: %s", strerror(errno));
        cleanup_and_exit(EXIT_FAILURE);
    }
    
    // Read file contents into buffer line by line
    size_t bytes_read = 0;
    size_t bytes_in_buffer = 0;
    while ((bytes_read = read(fd, *buffer + bytes_in_buffer, buffer_size - bytes_in_buffer)) > 0)
    {
        bytes_in_buffer += bytes_read;

        // Check if we need to realloc buffer
        if (bytes_in_buffer >= buffer_size - 1)
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

    pthread_mutex_unlock(&file_mutex);
    return bytes_in_buffer;
}

int file_seekto(uint32_t write_cmd, uint32_t write_cmd_offset)
{
    pthread_mutex_lock(&file_mutex);
    if (fd < 0)
    {
        syslog(LOG_ERR, "File not opened for ioctl operation");
        cleanup_and_exit(EXIT_FAILURE);
    }

    struct aesd_seekto seekto = {
        .write_cmd = write_cmd,
        .write_cmd_offset = write_cmd_offset
    };
    int ret = ioctl(fd, AESDCHAR_IOCSEEKTO, &seekto);
    pthread_mutex_unlock(&file_mutex);
    return ret;
}

void file_close()
{
    pthread_mutex_lock(&file_mutex);
    syslog(LOG_INFO, "Closing file "FILE_PATH" with fd %d", fd);
    close(fd);
    fd = -1;
    pthread_mutex_unlock(&file_mutex);
}

void file_cleanup()
{
    file_close();
    pthread_mutex_destroy(&file_mutex);
    
#ifndef USE_AESD_CHAR_DEVICE
    // Remove the file only if not using char device
    remove(FILE_PATH);
#endif
}