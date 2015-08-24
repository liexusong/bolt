#ifndef __BOLT_H
#define __BOLT_H


#include <pthread.h>
#include <event.h>
#include "hash.h"
#include "list.h"

#define  BOLT_MIN_CACHE_SIZE   (1024 * 1024 * 10)    /* 10MB */
#define  BOLT_FILENAME_LENGTH  1024
#define  BOLT_RBUF_SIZE        2048
#define  BOLT_WBUF_SIZE        512


#define  BOLT_LF  '\n'
#define  BOLT_CR  '\r'


#define  BOLT_HTTP_STATE_START     0
#define  BOLT_HTTP_STATE_CR        1
#define  BOLT_HTTP_STATE_CRLF      2
#define  BOLT_HTTP_STATE_CRLFCR    3
#define  BOLT_HTTP_STATE_CRLFCRLF  4


typedef struct {
    char *host;
    short port;
    int workers;
    char *logfile;
    int daemon;
    int max_cache;     /* The max cache size */
    int gc_threshold;  /* The range 1 ~ 100 */
    char *image_path;
} bolt_setting_t;


typedef struct {
    int sock;

    struct event_base *ebase;
    struct event event;

    /* Image cache info */
    pthread_mutex_t cache_lock;
    jk_hash_t *cache_htb;
    struct list_head gc_lru;
    jk_hash_t *waiting_htb;

    /* Task queue info */
    pthread_mutex_t task_lock;
    pthread_cond_t task_cond;
    struct list_head task_queue;

    int connections;
} bolt_service_t;


typedef struct {
    struct list_head link;  /* Link LRU */
    int size;
    int refcount;
    void *cache;
} bolt_cache_t;


typedef struct {
    struct list_head link;  /* Link waiting queue/free queue */
    int sock;
    int recv_state;
    int send_state;
    struct event revent;
    struct event wevent;
    int revset:1;
    int wevset:1;
    struct http_parser hp;
    char rbuf[BOLT_RBUF_SIZE];
    char *rpos;
    char *rend;
    char *rlast;
    char wbuf[BOLT_WBUF_SIZE];
    bolt_cache_t *icache;
} bolt_connection_t;


typedef struct {
    struct list_head link;  /* Link all tasks */
    char file[BOLT_FILENAME_LENGTH];
    int width;
    int height;
    int quality;
} bolt_task_t;

#endif
