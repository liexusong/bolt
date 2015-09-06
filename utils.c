#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>

int
bolt_utils_file_exists(char *path)
{
   return access((const char *)path, F_OK) == 0; 
}


void
bolt_daemonize()
{
    int fd;

    if (fork() != 0) {
        exit(0);
    }

    setsid();

    if ((fd = open("/dev/null", O_RDWR, 0)) != -1) {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);

        if (fd > STDERR_FILENO) {
            close(fd);
        }
    }
}


char *
bolt_strndup(char *str, int length)
{
    char *retval;

    retval = malloc(length + 1);
    if (!retval) {
        return NULL;
    }

    memcpy(retval, str, length);

    retval[length] = 0;

    return retval;
}


int
bolt_atoi(char *start, int length, int *retval)
{
#define  BOLT_DIGIT_CHAR(c)  ((c) >= '0' && (c) <= '9')

    int result;
    char *off;
    int times;

    for (off = start + length - 1, times = 1, result = 0;
         off >= start; off--)
    {
        if (BOLT_DIGIT_CHAR(*off)) {
            result += (*off - '0') * times;
            times *= 10;
        } else {
            break;
        }
    }

    if (off > start) {
        return -1;
    } else if (off == start) {
        if (*off == '-') {
            result = -result;
        } else if (*off != '+') {
            return -1;
        }
    }

    if (retval) {
        *retval = result;
    }

    return 0;

#undef  BOLT_DIGIT_CHAR
}
