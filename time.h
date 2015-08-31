#ifndef __BOLT_TIME_H
#define __BOLT_TIME_H

time_t bolt_parse_time(char *value, size_t len);
void bolt_gmtime(time_t t, struct tm *tp);
size_t bolt_format_time(char *buf, time_t t);

#endif
