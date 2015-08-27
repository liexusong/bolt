#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

int
bolt_utils_file_exists(char *path)
{
   return access((const char *)path, F_OK) == 0; 
}
