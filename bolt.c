/*
 * Bolt - The Realtime Compress Image System
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
#include "bolt.h"
#include "net.h"
#include "connection.h"
#include "worker.h"

bolt_setting_t _setting, *setting;
bolt_service_t _service, *service;


void
bolt_accept_handler(int sock, short event, void *arg)
{
    struct sockaddr_in addr;
    int nsock, size;

    nsock = accept(sock, (struct sockaddr*)&addr, &size);
    if (nsock == -1) {
        return;
    }

    if (bolt_set_nonblock(nsock) == -1) {
        close(nsock);
        return;
    }

    if (bolt_create_connection(nsock) == NULL) {
    }

    return;
}



int bolt_init_env()
{
    /* Init cache lock and task lock */
    if (pthread_mutex_init(&service->cache_lock, NULL) == -1
        || pthread_mutex_init(&service->task_lock, NULL) == -1
        || pthread_mutex_init(&service->wakeup_lock, NULL) == -1)
    {
        return -1;
    }

    /* Init task condition */
    if (pthread_cond_init(&service->task_cond) == -1) {
        return -1;
    }

    /* Create cache HashTable and waiting HashTable */
    if ((service->cache_htb = jk_hash_new(0, NULL, NULL)) == NULL
        || (service->waiting_htb = jk_hash_new(0, NULL, NULL)) == NULL)
    {
        return -1;
    }

    INIT_LIST_HEAD(&service->gc_lru);
    INIT_LIST_HEAD(&service->task_queue);
    INIT_LIST_HEAD(&service->wakeup_queue);

    /* Create listen socket */
    service->sock = bolt_listen_socket(setting->host,
                                       setting->port, 1);
    if (service->sock == -1) {
        return -1;
    }

    /* Add listen socket to libevent */
    event_set(&service->event, service->sock,
              EV_READ|EV_PERSIST, bolt_accept_handler, NULL);
    event_base_set(service->evbase, &service->event);
    if (event_add(&service->event, NULL) == -1) {
        return -1;
    }

    service->connections = 0;
    service->memused = 0;

    return 0;
}


void bolt_usage()
{
    fprintf(stderr, "\nbolt usage:\n");
    fprintf(stderr, "----------------------------------------------------\n");
    fprintf(stderr, "\t--workers <int>       The worker threads number\n");
    fprintf(stderr, "\t--logfile <str>       The log file\n");
    fprintf(stderr, "\t--max-cache <int>     The max cache size\n");
    fprintf(stderr, "\t--gc-threshold <int>  The GC threshold (range 1 ~ 100)\n");
    fprintf(stderr, "\t--path <str>          The image source path\n");
    fprintf(stderr, "\t--daemon              Using daemonize mode\n");
    fprintf(stderr, "\t--help                Display the usage\n\n");
    exit(0);
}


struct option long_options[] = {
    {"workers",      required_argument, 0, 'w'},
    {"max-cache",    required_argument, 0, 'C'},
    {"gc-threshold", required_argument, 0, 'F'},
    {"path",         required_argument, 0, 'P'},
    {"logfile",      required_argument, 0, 'L'},
    {"daemon",       no_argument,       0, 'd'},
    {"help",         no_argument,       0, 'h'},
    {0, 0, 0, 0},
};


void bolt_parse_options(int argc, char *argv[])
{
    int c;

    while ((c = getopt_long(argc, argv, "w:L:dh",
        long_options, NULL)) != -1)
    {
        switch (c) {
        case 'w':
            setting->workers = atoi(optarg);
            if (setting->workers <= 0) {
                setting->workers = 5;
            }
            break;
        case 'C':
            setting->max_cache = atoi(optarg);
            if (setting->max_cache < BOLT_MIN_CACHE_SIZE) {
                setting->max_cache = BOLT_MIN_CACHE_SIZE;
            }
            break;
        case 'F':
            setting->gc_threshold = atoi(optarg);
            if (setting->gc_threshold < 0
                || setting->gc_threshold >= 100)
            {
                setting->gc_threshold = 80;
            }
            break;
        case 'L':
            setting->logfile = strdup(optarg);
            break;
        case 'P':
            setting->image_path = strdup(optarg);
            break;
        case 'd':
            setting->daemon = 1;
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
    setting = &_setting;
    service = &_service;

    memset(setting, 0, sizeof(*setting));
    memset(serivce, 0, sizeof(*service));

    bolt_parse_options(argc, argv);

    if (bolt_init_env() == -1) {
        exit(1);
    }

    bolt_init_connections();
    bolt_init_workers(setting->workers);

    event_base_dispatch(service->ebase); /* Running */

    exit(0);
}
