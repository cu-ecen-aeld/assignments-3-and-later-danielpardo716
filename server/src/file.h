#ifndef FILE_H
#define FILE_H
#include <stdint.h>

#define FILE_IOCTL_SEEKTO_CMD       "AESDCHAR_IOCSEEKTO:"
#define FILE_IOCTL_SEEKTO_CMD_LEN   (sizeof(FILE_IOCTL_SEEKTO_CMD) - 1)

void file_append(const char* data, size_t length);
long file_read(char** buffer);
int file_ioctl(uint32_t write_cmd, uint32_t write_cmd_offset);
void file_cleanup();

#endif