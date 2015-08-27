#include <stdlib.h>
#include <unistd.h>
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
