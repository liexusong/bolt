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
#include <stdio.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <signal.h>
#include "bolt.h"
#include "net.h"
#include "connection.h"
#include "worker.h"
#include "config.h"
#include "utils.h"

bolt_setting_t *setting, _setting = {
    .host = "0.0.0.0",
    .port = 80,
    .workers = 10,
    .logfile = NULL,
    .logmark = BOLT_LOG_ERROR,
    .daemon = 0,
    .max_cache = BOLT_MIN_CACHE_SIZE,
    .gc_threshold = 80,
    .nocache = 0,
    .path = NULL,
    .path_len = 0,
    .watermark = NULL,
    .watermark_enable = 0,
};

bolt_service_t *service, _service;

void
bolt_accept_handler(int sock, short event, void *arg)
{
    struct sockaddr_in addr;
    socklen_t size = sizeof(addr);
    int nsock;

    for (;;) {

        nsock = accept(sock, (struct sockaddr *)&addr, &size);
        if (nsock == -1) {
            return;
        }

        if (bolt_set_nonblock(nsock) == -1) {
            close(nsock);
            bolt_log(BOLT_LOG_ERROR,
                     "Failed to set socket(%d) to nonblocking", nsock);
            return;
        }

        if (bolt_create_connection(nsock) == NULL) {
            bolt_log(BOLT_LOG_ERROR,
                     "Failed to create connection object, socket(%d)", nsock);
        }
    }
}

void
bolt_wakeup_handler(int sock, short event, void *arg)
{
    char byte;
    struct list_head *e;
    bolt_wait_queue_t *waitq;
    bolt_connection_t *c;

    if (sock != service->wakeup_notify[0]) {
        bolt_log(BOLT_LOG_ERROR, "Wakeup handler called by exception");
        return;
    }

    for (;;) {

        if (read(sock, (char *)&byte, 1) != 1) {
            return;
        }

again:
        waitq = NULL;

        LOCK_WAKEUP();

        e = service->wakeup_queue.next;
        if (e != &service->wakeup_queue) {
            waitq = list_entry(e, bolt_wait_queue_t, link);
            list_del(e);
        }

        UNLOCK_WAKEUP();

        if (waitq) {
            list_for_each(e, &waitq->wait_conns) {
                c = list_entry(e, bolt_connection_t, link);
                bolt_connection_begin_send(c);
            }

            free(waitq);

            goto again;
        }
    }
}

void
bolt_clock_handler(int sock, short event, void *arg)
{
    struct timeval tv = {.tv_sec = 1, .tv_usec = 0};
    static int clock_init = 0, clock_calls = 0, gc_run = 0;

    if (clock_init) {
        evtimer_del(&service->clock_event);
    } else {
        clock_init = 1;
    }

    evtimer_set(&service->clock_event, bolt_clock_handler, 0);
    event_base_set(service->ebase, &service->clock_event);
    evtimer_add(&service->clock_event, &tv);

    /* Update current time */
    service->current_time = time(NULL);

    if (service->memory_usage >= setting->max_cache) {
        write(service->gc_notify[1], "\0", 1); /* Notify GC thread */
    }
}

int bolt_init_service()
{
    /* Init cache lock and task lock */
    if (pthread_mutex_init(&service->cache_lock, NULL) == -1
        || pthread_mutex_init(&service->task_lock, NULL) == -1
        || pthread_mutex_init(&service->waitq_lock, NULL) == -1
        || pthread_mutex_init(&service->wakeup_lock, NULL) == -1)
    {
        bolt_log(BOLT_LOG_ERROR,
                 "Failed to initialize service's locks");
        return -1;
    }

    /* Init task condition */
    if (pthread_cond_init(&service->task_cond, NULL) == -1) {
        bolt_log(BOLT_LOG_ERROR,
                 "Failed to initialize task's condition");
        return -1;
    }

    /* Create cache HashTable and waiting HashTable */
    if ((service->cache_htb = jk_hash_new(0, NULL, NULL)) == NULL
        || (service->waiting_htb = jk_hash_new(0, NULL, NULL)) == NULL)
    {
        bolt_log(BOLT_LOG_ERROR,
                 "Failed to create cache and waiting HashTables");
        return -1;
    }

    INIT_LIST_HEAD(&service->gc_lru);
    INIT_LIST_HEAD(&service->task_queue);
    INIT_LIST_HEAD(&service->wakeup_queue);

    /* Create listen socket */
    service->sock = bolt_listen_socket(setting->host, setting->port, 1);
    if (service->sock == -1) {
        bolt_log(BOLT_LOG_ERROR,
                 "Failed to create listen socket");
        return -1;
    }

    /* Init wakeup context */
    if (pipe(service->wakeup_notify) == -1
        || bolt_set_nonblock(service->wakeup_notify[0]) == -1)
    {
        bolt_log(BOLT_LOG_ERROR,
                 "Failed to create wakeup notify pipe");
        return -1;
    }

    if (pipe(service->gc_notify) == -1
        || bolt_set_nonblock(service->gc_notify[1]) == -1)
    {
        bolt_log(BOLT_LOG_ERROR,
                 "Failed to create GC notify pipe");
        return -1;
    }

    service->ebase = event_base_new();
    if (service->ebase == NULL) {
        bolt_log(BOLT_LOG_ERROR,
                 "Failed to create event base object");
        return -1;
    }

    /* Add listen socket to libevent */
    event_set(&service->event, service->sock,
              EV_READ|EV_PERSIST, bolt_accept_handler, NULL);

    event_base_set(service->ebase, &service->event);

    if (event_add(&service->event, NULL) == -1) {
        bolt_log(BOLT_LOG_ERROR,
                 "Failed to add accept event to libevent");
        return -1;
    }

    /* Add wakeup notify fd to libevent */
    event_set(&service->wakeup_event, service->wakeup_notify[0],
              EV_READ|EV_PERSIST, bolt_wakeup_handler, NULL);

    event_base_set(service->ebase, &service->wakeup_event);

    if (event_add(&service->wakeup_event, NULL) == -1) {
        bolt_log(BOLT_LOG_ERROR,
                 "Failed to add wakeup event to libevent");
        return -1;
    }

    service->connections = 0;
    service->memory_usage = 0;

    return 0;
}

void bolt_usage()
{
    fprintf(stderr, "                                         \n"
                    "       _/_/_/              _/    _/      \n"
                    "      _/    _/    _/_/    _/  _/_/_/_/   \n"
                    "     _/_/_/    _/    _/  _/    _/        \n"
                    "    _/    _/  _/    _/  _/    _/         \n"
                    "   _/_/_/      _/_/    _/      _/_/      \n"
                    "                                         \n");
    fprintf(stderr, "-----------------------------------------\n");
    fprintf(stderr, "  -c <path>   Configure file path to read\n");
    fprintf(stderr, "  -h          Display bolt's help\n\n");
    exit(0);
}

void bolt_parse_options(int argc, char *argv[])
{
    int c;

    while((c = getopt(argc, argv,"c:h"))!= -1) {
        switch (c) {
        case 'c':
            if (bolt_read_confs(optarg) == -1) {
                exit(1);
            }
            break;
        case 'h':
            bolt_usage();
            break;
        default:
            fprintf(stderr, "Option `%d' invalid\n", c);
            break;
        }
    }
}

int main(int argc, char *argv[])
{
    sigset_t signal_mask;

    setting = &_setting;
    service = &_service;

    memset(service, 0, sizeof(*service));

    bolt_parse_options(argc, argv);

    if (setting->path == NULL
        || !bolt_file_exists(setting->path))
    {
        fprintf(stderr, "Fatal: Image source path must be set and exists\n\n");
        exit(1);
    }

    if (setting->daemon) {
        bolt_daemonize();
    }

    sigemptyset(&signal_mask);
    sigaddset(&signal_mask, SIGPIPE);

    pthread_sigmask(SIG_BLOCK, &signal_mask, NULL);

    if (bolt_init_log(setting->logfile, setting->logmark) == -1
        || bolt_init_service() == -1
        || bolt_init_connections() == -1
        || bolt_init_workers(setting->workers) == -1)
    {
        exit(1);
    }

    bolt_clock_handler(0, 0, 0);
    event_base_dispatch(service->ebase); /* Being run */

    bolt_destroy_log();

    exit(0);
}
