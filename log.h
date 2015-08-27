#ifndef __BOLT_LOG_H
#define __BOLT_LOG_H

#define BOLT_LOG_DEBUG   0
#define BOLT_LOG_NOTICE  1
#define BOLT_LOG_ALERT   2
#define BOLT_LOG_ERROR   3

int bolt_init_log(char *file, int mark);
void bolt_log(int level, char *fmt, ...);
void bolt_destroy_log();

#endif
