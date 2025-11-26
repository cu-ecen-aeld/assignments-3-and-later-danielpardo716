#include <syslog.h>
#include <stdio.h>

int main(int argc, char* argv[])
{
    openlog(NULL, 0, LOG_USER);

    // Check argument count
    // argc counts the program name as argv[0]
    if (argc != 3)
    {
	syslog(LOG_ERR, "Must provide filename and writestr arguments");
	return 1;
    }

    char* file_name = argv[1];
    char* write_str = argv[2];

    FILE* file_ptr = fopen(file_name, "w");
    if (file_ptr != NULL)
    {
        syslog(LOG_DEBUG, "Writing %s to %s", write_str, file_name);
        fputs(write_str, file_ptr);
        fclose(file_ptr);
    }
    else
    {
        syslog(LOG_ERR, "Unable to open %s", file_name);
        return 1;
    }

    return 0;
}
