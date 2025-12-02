#include "systemcalls.h"
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

/**
 * @param cmd the command to execute with system()
 * @return true if the command in @param cmd was executed
 *   successfully using the system() call, false if an error occurred,
 *   either in invocation of the system() call, or if a non-zero return
 *   value was returned by the command issued in @param cmd.
*/
bool do_system(const char *cmd)
{
    return (system(cmd) == 0);
}

/**
* @param count -The numbers of variables passed to the function. The variables are command to execute.
*   followed by arguments to pass to the command
*   Since exec() does not perform path expansion, the command to execute needs
*   to be an absolute path.
* @param ... - A list of 1 or more arguments after the @param count argument.
*   The first is always the full path to the command to execute with execv()
*   The remaining arguments are a list of arguments to pass to the command in execv()
* @return true if the command @param ... with arguments @param arguments were executed successfully
*   using the execv() call, false if an error occurred, either in invocation of the
*   fork, waitpid, or execv() command, or if a non-zero return value was returned
*   by the command issued in @param arguments with the specified arguments.
*/

bool do_exec(int count, ...)
{
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;

    // Fork child process
    int pid = fork();
    if (pid < 0)
    {
        // Fork failed
        return false;
    }
    else if (pid == 0)
    {
        // Run child process - should not return
        int result = execv(command[0], command);
        exit(result);
    }

    // Parent process - wait for child process to exit
    int status;
    if (waitpid(pid, &status, 0) == -1)
    {
	    return false;
    }

    va_end(args);

    // Check if child returned normally
    return ((WIFEXITED(status) && WEXITSTATUS(status)) == 0);
}

/**
* @param outputfile - The full path to the file to write with command output.
*   This file will be closed at completion of the function call.
* All other parameters, see do_exec above
*/
bool do_exec_redirect(const char *outputfile, int count, ...)
{
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;

    // Open file for redirected output
    int fd = open(outputfile, O_WRONLY|O_TRUNC|O_CREAT, 0644);
    if (fd < 0) return false;

    // Fork child process
    int pid = fork();
    if (pid < 0)
    {
        // Fork failed
        return false;
    }
    else if (pid == 0)
    {
        // Child process - close file using duplicated fd and execute command
        if (dup2(fd, 1) < 0) return false;
        close(fd);
        int result = execvp(command[0], command);
        exit(result);
    }

    // Parent process - close file and wait for child process to exit
    close(fd);
    int status;
    if (waitpid(pid, &status, 0) == -1)
    {
	    return false;
    }

    va_end(args);

    // Check if child returned normally
    return ((WIFEXITED(status) && WEXITSTATUS(status)) == 0);
}
