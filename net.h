#ifndef __BOLT_NET_H
#define __BOLT_NET_H

int bolt_set_nonblock(int fd);
int bolt_listen_socket(char *addr, short port, int nonblock);

#endif
