#ifndef __BOLT_H
#define __BOLT_H


#include <pthread.h>
#include <event.h>
#include "http_parser.h"
#include "hash.h"
#include "list.h"

#define  BOLT_MIN_CACHE_SIZE   (1024 * 1024 * 10)    /* 10MB */
#define  BOLT_FILENAME_LENGTH  1024
#define  BOLT_RBUF_SIZE        2048
#define  BOLT_WBUF_SIZE        512


#define  BOLT_LF    '\n'
#define  BOLT_CR    '\r'
#define  BOLT_CRLF  "\r\n"


#define  BOLT_HTTP_STATE_START     0
#define  BOLT_HTTP_STATE_CR        1
#define  BOLT_HTTP_STATE_CRLF      2
#define  BOLT_HTTP_STATE_CRLFCR    3
#define  BOLT_HTTP_STATE_CRLFCRLF  4


#define  BOLT_SEND_HEADER_STATE    1
#define  BOLT_SEND_CONTENT_STATE   2


typedef struct {
    char *host;
    short port;
    int workers;
    char *logfile;
    int daemon;
    int max_cache;     /* The max cache size */
    int gc_threshold;  /* The range 1 ~ 100 */
    char *image_path;
    int path_len;
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

    /* Wakeup queue info */
    pthread_mutex_t wakeup_lock;
    struct list_head wakeup_queue;
    int wakeup_notify[2];
    struct event wakeup_event;

    int connections;
    int total_mem_used;
} bolt_service_t;


typedef struct {
    struct list_head link;  /* Link LRU */
    int size;
    int refcount;
    void *cache;
    char filename[BOLT_FILENAME_LENGTH];
    int fnlen;
} bolt_cache_t;


typedef struct {
    struct list_head link;  /* Link waiting queue/free queue */
    int sock;
    int http_code;
    int recv_state;
    int send_state;
    int keepalive;
    int parse_error;
    struct event revent;
    struct event wevent;
    int revset:1;
    int wevset:1;
    struct http_parser hp;
    /* read buffer */
    char rbuf[BOLT_RBUF_SIZE];
    char *rpos;
    char *rend;
    char *rlast;
    /* write buffer */
    char wbuf[BOLT_WBUF_SIZE];
    char *wpos;
    char *wend;
    bolt_cache_t *icache;
    int fnlen;
    char filename[BOLT_FILENAME_LENGTH];
} bolt_connection_t;


typedef struct {
    struct list_head link;  /* Link all tasks */
    int fnlen;
    char filename[BOLT_FILENAME_LENGTH];
} bolt_task_t;


typedef struct {
    struct list_head link;  /* Link all wait queue */
    struct list_head wait_conns;
} bolt_wait_queue_t;


extern bolt_setting_t *setting;
extern bolt_service_t *service;

#endif
