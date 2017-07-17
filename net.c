/*
 * Bolt - The Realtime Image Compress System
 * Copyright (c) 2015 - 2016, Liexusong <280259971@qq.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

/**************************
 * set fd to non-blocking *
 **************************/
int bolt_set_nonblock(int fd)
{
    int flags;

    if ((flags = fcntl(fd, F_GETFL, 0)) == -1 ||
        fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
    {
        return -1;
    }
    return 0;
}

/************************
 * create listen socket *
 ************************/
int bolt_listen_socket(char *addr, short port, int nonblock)
{
    int sock;
    int flags;
    struct linger ln = {0, 0};
    struct sockaddr_in si;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        return -1;
    }

    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &flags, sizeof(flags));
    setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &flags, sizeof(flags));
    setsockopt(sock, SOL_SOCKET, SO_LINGER, &ln, sizeof(ln));
#if !defined(TCP_NOPUSH) && defined(TCP_NODELAY)
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &flags, sizeof(flags));
#endif

    if (nonblock && bolt_set_nonblock(sock) == -1) {
        goto err;
    }

    si.sin_family = AF_INET;
    si.sin_port = htons(port);
    si.sin_addr.s_addr = htonl(inet_addr(addr));

    if (bind(sock, (struct sockaddr *)&si, sizeof(si)) == -1) {
        goto err;
    }

    if (listen(sock, 1024) == -1) {
        goto err;
    }

    return sock;

err:
    close(sock);
    return -1;
}
