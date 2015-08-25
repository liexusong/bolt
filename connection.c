#include <stdlib.h>
#include "bolt.h"
#include "connection.h"

#define BOLT_MAX_FREE_CONNS  1024


static int
bolt_connection_process_request(bolt_connection_t *c);
static int
bolt_connection_http_parse_url(struct http_parser *parser,
    const char *at, size_t len);


static int freeconn_count;
static void *freeconn_list[BOLT_MAX_FREE_CONNS];

static struct http_parser_settings http_parser_callbacks = {
    .on_message_begin    = NULL,
    .on_url              = bolt_connection_http_parse_url,
    .on_header_field     = NULL,
    .on_header_value     = NULL,
    .on_headers_complete = NULL,
    .on_body             = NULL,
    .on_message_complete = NULL
};


int
bolt_init_connections()
{
    freeconn_count = 0;
    return 0;
}


int
bolt_connection_install_revent(bolt_connection_t *c,
    void (*handler)(int, short, void *))
{
    if (!c->revset) {
        event_set(&c->revent, c->sock,
                  EV_READ|EV_PERSIST, handler, c);
        event_base_set(service->ebase, &c->revent);
        if (event_add(&c->revent, NULL) == -1) {
            return -1;
        }
        c->revset = 1;
    }
    return 0;
}


int
bolt_connection_install_wevent(bolt_connection_t *c,
    void (*handler)(int, short, void *))
{
    if (!c->wevset) {
        event_set(&c->wevent, c->sock,
                  EV_WRITE|EV_PERSIST, handler, c);
        event_base_set(service->ebase, &c->wevent);
        if (event_add(&c->wevent, NULL) == -1) {
            return -1;
        }
        c->wevset = 1;
    }
    return 0;
}


void
bolt_connection_remove_revent(bolt_connection_t *c)
{
    if (c->revset) {
        if (event_del(&c->revent) == 0) {
            c->revset = 0;
        }
    }
}


void
bolt_connection_remove_wevent(bolt_connection_t *c)
{
    if (c->wevset) {
        if (event_del(&c->wevent) == 0) {
            c->wevset = 0;
        }
    }
}


bolt_connection_t *
bolt_create_connection(int sock)
{
    bolt_connection_t *c;

    if (freeconn_count > 0) {
        c = freeconn_list[--freeconn_count];

    } else {
        c = malloc(sizeof(*c));
        if (c == NULL) {
            return NULL;
        }
    }

    c->sock = sock;
    c->recv_state = BOLT_HTTP_STATE_START;
    c->revset = 0;
    c->wevset = 0;
    c->parse_error = 0;
    c->wakeup_close = 0;
    c->rpos = c->rbuf;
    c->rend = c->rbuf + BOLT_RBUF_SIZE;
    c->rlast = c->rbuf;
    c->icache = NULL;

    http_parser_init(&c->hp, HTTP_REQUEST);
    c->hp.data = c;

    if (bolt_connection_install_revent(c,
          bolt_connection_recv_handler) == -1)
    {
        bolt_free_connection(c);
        return NULL;
    }

    return c;
}


void
bolt_free_connection(bolt_connection_t *c)
{
    close(c->sock);

    bolt_connection_remove_revent(c);
    bolt_connection_remove_wevent(c);

    if (freeconn_count < BOLT_MAX_FREE_CONNECTIONS) {
        freeconn_list[freeconn_count++] = c;
    } else {
        free(c);
    }
}


static int
bolt_connection_recv_completed(bolt_connection_t *c)
{
    char last;

    while (c->rlast < c->rpos) {

        last = *c->rlast;

        switch (c->recv_state) {
        case BOLT_HTTP_STATE_START:
            if (last == BOLT_CR) {
                c->recv_state = BOLT_HTTP_STATE_CR;
            }
            break;

        case BOLT_HTTP_STATE_CR:
            if (last == BOLT_LF) {
                c->recv_state = BOLT_HTTP_STATE_CRLF;
            } else {
                c->recv_state = BOLT_HTTP_STATE_START;
            }
            break;

        case BOLT_HTTP_STATE_CRLF:
            if (last == BOLT_CR) {
                c->recv_state = BOLT_HTTP_STATE_CRLFCR;
            } else {
                c->recv_state = BOLT_HTTP_STATE_START;
            }
            break;

        case BOLT_HTTP_STATE_CRLFCR:
            if (last == BOLT_LF) {
                c->recv_state = BOLT_HTTP_STATE_CRLFCRLF;
            } else {
                c->recv_state = BOLT_HTTP_STATE_START;
            }
            break;

        default:
            c->recv_state = BOLT_HTTP_STATE_START;
            break;
        }

        c->rlast++;
    }

    return c->recv_state == BOLT_HTTP_STATE_CRLFCRLF ? 0 : -1;
}


void
bolt_connection_recv_handler(int sock, short event, void *arg)
{
    bolt_connection_t *c = (bolt_connection_t *)arg;
    int nbytes, would, retval, moveb;
    char last;

    if (!c || c->sock != sock) {
        return;
    }

    would = c->rend - c->rpos;
    if (would <= 0) {
        bolt_free_connection(c);
        return;
    }

    nbytes = read(c->sock, c->rpos, would);
    if (nbytes < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            bolt_free_connection(conn);
        }

        bolt_connection_install_revent(c,
                                       bolt_connection_recv_handler);
        return;

    } else if (nbytes == 0) {
        bolt_free_connection(conn);
        return;
    }

    c->rpos += nbytes;

    if (bolt_connection_recv_completed(c) == 0) {
        retval = http_parser_execute(&c->hp, &http_parser_callbacks,
                                     c->rbuf, c->rlast - c->rbuf);

        if (c->hp.method != HTTP_GET) {
            bolt_free_connection(c);
            return;
        }

        /* Process connection request */

        if (bolt_connection_process_request(c) == 0) {
            moveb = c->rpos - c->rlast;

            if (moveb > 0) { /* move remain stream to head */
                memmove(c->rbuf, c->rlast, moveb);
                c->rlast = c->rbuf;
                c->rpos = c->rbuf + moveb;

            } else {
                c->rlast = c->rpos = c->rbuf;
            }

        } else {
            bolt_free_connection(c);
        }
    }
}


void
bolt_connection_send_handler(int sock, short event, void *arg)
{
    bolt_connection_t *c = (bolt_connection_t *)arg;
    int nsend, nbytes;

    if (!c || c->sock != sock) {
        return;
    }

    nsend = c->wend - c->wpos;

    nbytes = write(c->sock, c->wpos, nsend);
    if (nbytes < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            bolt_free_connection(conn);
        }
        bolt_connection_install_wevent(c,
                                       bolt_connection_send_handler)
        return;

    } else if (nbytes == 0) {
        bolt_free_connection(conn);
        return;
    }

    c->wpos += nbytes;

    if (c->wpos >= c->wend) {

        switch (c->send_state) {
        case BOLT_SEND_HEADER_STATE:
            c->wpos = c->icache->cache;
            c->wend = c->icache->size;
            c->send_state = BOLT_SEND_CONTENT_STATE;
            break;

        case BOLT_SEND_CONTENT_STATE:
            __sync_sub_and_fetch(&c->icache->refcount, 1);
            bolt_connection_remove_wevent(c); /* remove write event */
            bolt_connection_recv_handler(c->sock, EV_READ, c);
            break;
        }
    }
}


#define BOLT_HEADER_TEMPLATE              \
    "HTTP/1.1 200 OK\r\n"                 \
    "Content-Type: image/jpeg\r\n"        \
    "Server: Bolt\r\n"                    \
    "Content-Length: %d\r\n\r\n"

void
bolt_connection_begin_send(bolt_connection_t *c)
{
    int nsend;

    nsend = snprintf(c->wbuf, BOLT_WBUF_SIZE,
                     BOLT_HEADER_TEMPLATE, c->icache->size);

    c->wpos = c->wbuf;
    c->wend = c->wbuf + nsend;
    c->send_state = BOLT_SEND_HEADER_STATE;

    bolt_connection_send_handler(c->sock, EV_WRITE, c);
}


static int
bolt_connection_process_request(bolt_connection_t *c)
{
    bolt_cache_t *cache;
    bolt_wait_queue_t *waitq;
    int action = 1;
    bolt_task_t *task;

    if (c->parse_error != 0) {
        return -1;
    }

    /* 1) Get cache? */

    pthread_mutex_lock(&service->cache_lock);

    if (jk_hash_find(service->cache_htb, c->filename,
          c->filename_len, &cache) == JK_HASH_OK)
    {
        __sync_add_and_fetch(&cache->refcount, 1);
        c->cache = cache;
        action = 0;

    } else {
        if (jk_hash_find(service->waiting_htb, c->filename,
              c->filename_len, &waitq) == JK_HASH_ERR)
        {
            waitq = malloc(sizeof(*waitq));
            if (NULL == waitq) { /* out of memory */
                exit(1);
            }

            INIT_LIST_HEAD(&waitq->wait_conns);

            jk_hash_insert(service->waiting_htb,
                           c->filename, c->filename_len, waitq, 0);
        }

        list_add(&c->link, &waitq->wait_conns);
    }

    pthread_mutex_unlock(&service->cache_lock);

    /* 2) Do task? */

    if (action) {
        task = malloc(sizeof(*task));
        if (NULL == task) {
            exit(1);
        }

        memcpy(task->filename, c->filename, c->fnlen);
        task->fnlen = c->fnlen;

        pthread_mutex_lock(&service->task_lock);
        list_add(&task->link, &service->task_queue); /* add to tasks queue */
        pthread_cond_signal(&service->task_cond, &service->task_lock);
        pthread_mutex_unlock(&service->task_lock);

    } else {
        bolt_connection_begin_send(c);
    }

    bolt_connection_remove_revent(c); /* remove read event */

    return 0;
}


static int
bolt_connection_http_parse_url(struct http_parser *p,
    const char *at, size_t len)
{
    bolt_connection_t *c = p->data;

    if (len > BOLT_FILENAME_LENGTH) {
        c->parse_error = 1;
        return -1;
    }

    memcpy(c->filename, at, len); c->fnlen = len;

    return 0;
}

