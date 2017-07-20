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
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include "bolt.h"
#include "connection.h"
#include "worker.h"
#include "time.h"

#define BOLT_MAX_FREE_CONNECTIONS  1024

static int
bolt_connection_process_request(bolt_connection_t *c);
static int
bolt_connection_http_parse_url(struct http_parser *parser,
    const char *at, size_t len);
static int
bolt_connection_http_parse_field(struct http_parser *p,
    const char *at, size_t len);
static int
bolt_connection_http_parse_value(struct http_parser *p,
    const char *at, size_t len);
void
bolt_connection_recv_handler(int sock, short event, void *arg);
void
bolt_connection_send_handler(int sock, short event, void *arg);

static int freeconn_count;
static void *freeconn_list[BOLT_MAX_FREE_CONNECTIONS];

static struct http_parser_settings http_parser_callbacks = {
    .on_message_begin    = NULL,
    .on_url              = bolt_connection_http_parse_url,
    .on_header_field     = bolt_connection_http_parse_field,
    .on_header_value     = bolt_connection_http_parse_value,
    .on_headers_complete = NULL,
    .on_body             = NULL,
    .on_message_complete = NULL
};

char bolt_error_400_page[] =
"<html>"
"<head><title>400 Bad Request</title></head>"
"<body bgcolor=\"white\">"
"<center><h1>400 Bad Request</h1></center>"
"<hr><div align=\"center\">Bolt " BOLT_VERSION "</div>"
"</body>"
"</html>";

char bolt_error_404_page[] =
"<html>"
"<head><title>404 Not Found</title></head>"
"<body bgcolor=\"white\">"
"<center><h1>404 Not Found</h1></center>"
"<hr><div align=\"center\">Bolt " BOLT_VERSION "</div>"
"</body>"
"</html>";

char bolt_error_500_page[] =
"<html>"
"<head><title>500 Internal Server Error</title></head>"
"<body bgcolor=\"white\">"
"<center><h1>500 Internal Server Error</h1></center>"
"<hr><div align=\"center\">Bolt " BOLT_VERSION "</div>"
"</body>"
"</html>";

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
            bolt_log(BOLT_LOG_ERROR,
                     "Failed to install read event, socket(%d)", c->sock);
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
            bolt_log(BOLT_LOG_ERROR,
                     "Failed to install write event, socket(%d)", c->sock);
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
    int retval;

    if (freeconn_count > 0) {
        c = freeconn_list[--freeconn_count];

    } else {
        c = malloc(sizeof(*c));
        if (c == NULL) {
            bolt_log(BOLT_LOG_ERROR,
                     "Not enough memory to alloc connection object");
            return NULL;
        }
    }

    c->sock = sock;
    c->http_code = 200;
    c->recv_state = BOLT_HTTP_STATE_START;
    c->keepalive = 0;
    c->revset = 0;
    c->wevset = 0;
    c->parse_field = BOLT_PARSE_FIELD_START;
    c->parse_error = 0;
    c->header_only = 0;
    c->rpos = c->rbuf;
    c->rend = c->rbuf + BOLT_RBUF_SIZE;
    c->rlast = c->rbuf;
    c->icache = NULL;

    c->headers.tms = 0;

    http_parser_init(&c->hp, HTTP_REQUEST);

    c->hp.data = c;

    retval = bolt_connection_install_revent(c, bolt_connection_recv_handler);
    if (retval == -1) {
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

    if (c->icache) {
        c->icache->refcount--;
        c->icache = NULL;
    }

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

        if (c->recv_state == BOLT_HTTP_STATE_CRLFCRLF) {
            return 0;
        }

        c->rlast++;
    }

    return -1;
}

void
bolt_connection_keepalive(bolt_connection_t *c)
{
    if (c->http_code == 200) {
        c->icache->refcount--;
        c->icache = NULL;
    }

    c->http_code = 200;
    c->recv_state = BOLT_HTTP_STATE_START;
    c->parse_field = BOLT_PARSE_FIELD_START;
    c->keepalive = 0;
    c->parse_error = 0;
    c->header_only = 0;
    c->rpos = c->rbuf;
    c->rlast = c->rbuf;

    c->headers.tms = 0;

    http_parser_init(&c->hp, HTTP_REQUEST);
    c->hp.data = c;

    bolt_connection_remove_wevent(c);
    bolt_connection_install_revent(c, bolt_connection_recv_handler);
}

void
bolt_connection_recv_handler(int sock, short event, void *arg)
{
    bolt_connection_t *c = (bolt_connection_t *)arg;
    int nbytes, remain, retval;

    if (!c || c->sock != sock) {
        bolt_log(BOLT_LOG_ERROR, "Connection was broken, address `%p'", c);
        return;
    }

    remain = c->rend - c->rpos;
    if (remain <= 0) {
        bolt_free_connection(c);
        bolt_log(BOLT_LOG_ERROR,
                 "Connection header too big, socket(%d)", c->sock);
        return;
    }

    nbytes = read(c->sock, c->rpos, remain);
    if (nbytes < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            bolt_free_connection(c);
            bolt_log(BOLT_LOG_ERROR,
                     "Connection read error, socket(%d)", c->sock);
        }
        return;

    } else if (nbytes == 0) {
        bolt_free_connection(c);
        return;
    }

    c->rpos += nbytes;

    if (bolt_connection_recv_completed(c) == 0) {

        retval = http_parser_execute(&c->hp, &http_parser_callbacks,
                                     c->rbuf, c->rlast - c->rbuf);

        if (c->hp.method != HTTP_GET) {
            bolt_free_connection(c);
            bolt_log(BOLT_LOG_ERROR,
                     "Connection request method was not GET, socket(%d)",
                     c->sock);
            return;
        }

        c->keepalive = http_should_keep_alive(&c->hp);

        /* Process connection request */
        if (bolt_connection_process_request(c) == -1) {
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
        bolt_log(BOLT_LOG_ERROR, "Connection was broken, address `%p'", c);
        return;
    }

    nsend = c->wend - c->wpos;

    nbytes = write(c->sock, c->wpos, nsend);
    if (nbytes < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            bolt_free_connection(c);
            bolt_log(BOLT_LOG_ERROR,
                     "Connection write error, socket(%d)", c->sock);
        }
        return;

    } else if (nbytes == 0) {
        bolt_free_connection(c);
        return;
    }

    c->wpos += nbytes;

    if (c->wpos >= c->wend) { /* Finished sent data to client */

        if (c->send_state == BOLT_SEND_HEADER_STATE && !c->header_only) {

            switch (c->http_code) {
            case 200:
                c->wpos = (char *)c->icache->cache;
                c->wend = c->wpos + c->icache->size;
                break;

            case 400:
                c->wpos = bolt_error_400_page;
                c->wend = c->wpos + sizeof(bolt_error_400_page) - 1;
                break;

            case 404:
                c->wpos = bolt_error_404_page;
                c->wend = c->wpos + sizeof(bolt_error_404_page) - 1;
                break;

            case 500:
            default:
                c->wpos = bolt_error_500_page;
                c->wend = c->wpos + sizeof(bolt_error_500_page) - 1;
                break;
            }

            c->send_state = BOLT_SEND_CONTENT_STATE;

        } else {
            if (c->keepalive) { /* Do keepalive */
                bolt_connection_keepalive(c);
            } else {
                bolt_free_connection(c);
            }
        }
    }
}

void
bolt_connection_begin_send(bolt_connection_t *c)
{
    int nsend;

    switch (c->http_code) {
    case 200:
        nsend = snprintf(c->wbuf, BOLT_WBUF_SIZE,
                         "HTTP/1.1 200 OK" BOLT_CRLF
                         "Content-Type: image/jpeg" BOLT_CRLF
                         "Content-Length: %d" BOLT_CRLF
                         "Last-Modified: %s" BOLT_CRLF
                         "Server: Bolt" BOLT_CRLF BOLT_CRLF,
                         c->icache->size,
                         c->icache->datetime);
        break;

    case 304:
        nsend = snprintf(c->wbuf, BOLT_WBUF_SIZE,
                         "HTTP/1.1 304 Not Modified" BOLT_CRLF
                         "Server: Bolt" BOLT_CRLF BOLT_CRLF);
        break;

    case 400:
        nsend = snprintf(c->wbuf, BOLT_WBUF_SIZE,
                         "HTTP/1.1 400 Bad Request" BOLT_CRLF
                         "Content-Type: text/html" BOLT_CRLF
                         "Content-Length: %d" BOLT_CRLF
                         "Server: Bolt" BOLT_CRLF BOLT_CRLF,
                         (int)(sizeof(bolt_error_400_page) - 1));
        break;

    case 404:
        nsend = snprintf(c->wbuf, BOLT_WBUF_SIZE,
                         "HTTP/1.1 404 Not Found" BOLT_CRLF
                         "Content-Type: text/html" BOLT_CRLF
                         "Content-Length: %d" BOLT_CRLF
                         "Server: Bolt" BOLT_CRLF BOLT_CRLF,
                         (int)(sizeof(bolt_error_404_page) - 1));
        break;

    case 500:
    default:
        nsend = snprintf(c->wbuf, BOLT_WBUF_SIZE,
                         "HTTP/1.1 500 Internal Server Error" BOLT_CRLF
                         "Content-Type: text/html" BOLT_CRLF
                         "Content-Length: %d" BOLT_CRLF
                         "Server: Bolt" BOLT_CRLF BOLT_CRLF,
                         (int)(sizeof(bolt_error_500_page) - 1));
        break;
    }

    c->wpos = c->wbuf;
    c->wend = c->wbuf + nsend;
    c->send_state = BOLT_SEND_HEADER_STATE;

    bolt_connection_install_wevent(c, bolt_connection_send_handler);
}

static int
bolt_connection_process_request(bolt_connection_t *c)
{
    bolt_cache_t *cache;
    bolt_wait_queue_t *waitq;
    int dopass = 0;
    int retval;

    if (c->parse_error != 0) {
        bolt_log(BOLT_LOG_ERROR,
                 "Header was invaild when parsed, socket(%d)", c->sock);
        return -1;
    }

    bolt_connection_remove_revent(c); /*  Remove read event */

    if (setting->nocache) { /* For testing no cache feature */
        goto nocache;
    }

    /* First: get image from cache */

    pthread_mutex_lock(&service->cache_lock); /* Lock cache */

    retval = jk_hash_find(service->cache_htb,
                          c->filename, c->fnlen, (void **)&cache);

    if (retval == JK_HASH_OK) {

        /* Move cache to LRU tail */
        list_del(&cache->link);
        list_add_tail(&cache->link, &service->gc_lru);

        if (cache->time == c->headers.tms) {
            c->http_code = 304;
            c->header_only = 1;
        } else {
            c->http_code = 200;
            c->icache = cache;
            cache->refcount++;
            cache->last = service->current_time;
        }

        pthread_mutex_unlock(&service->cache_lock); /* Unlock cache */

        bolt_connection_begin_send(c);

        return 0;
    }

    pthread_mutex_unlock(&service->cache_lock); /* Unlock cache */

nocache:

    pthread_mutex_lock(&service->waitq_lock); /* Lock wait queue */

    retval = jk_hash_find(service->waiting_htb,
                          c->filename, c->fnlen, (void **)&waitq);

    if (retval == JK_HASH_ERR) { /* Free by bolt_wakeup_handler() */

        waitq = malloc(sizeof(*waitq));
        if (NULL == waitq) {
            pthread_mutex_unlock(&service->waitq_lock);
            bolt_log(BOLT_LOG_ERROR,
                     "Not enough memory to alloc wait queue");
            return -1;
        }

        INIT_LIST_HEAD(&waitq->wait_conns);

        jk_hash_insert(service->waiting_htb,
                       c->filename, c->fnlen, waitq, 0);

        dopass = 1;
    }

    list_add(&c->link, &waitq->wait_conns);

    pthread_mutex_unlock(&service->waitq_lock); /* Unlock wait queue */

    if (dopass) {
        return bolt_worker_pass_task(c);
    }

    return 0;
}

static int
bolt_connection_http_parse_url(struct http_parser *p,
    const char *at, size_t len)
{
    bolt_connection_t *c = p->data;
    char *start, *end;

    if (len > BOLT_FILENAME_LENGTH) {
        c->parse_error = 1;
        return -1;
    }

    start = (char *)at;
    end = (char *)at + len;

    while (start < end && *start == '/') start++;

    len = end - start;
    if (len == 0) {
        c->parse_error = 1;
        return -1;
    }

    memcpy(c->filename, "/", 1);
    memcpy(c->filename + 1, start, len);

    c->filename[len+1] = 0;

    c->fnlen = len + 1;

    return 0;
}

static int
bolt_connection_http_parse_field(struct http_parser *p,
    const char *at, size_t len)
{
    bolt_connection_t *c = p->data;

    if (!strncasecmp(at, "If-Modified-Since", len)) {
        c->parse_field = BOLT_PARSE_FIELD_IF_MODIFIED_SINCE;
    }

    return 0;
}

static int
bolt_connection_http_parse_value(struct http_parser *p,
    const char *at, size_t len)
{
    bolt_connection_t *c = p->data;

    if (c->parse_field == BOLT_PARSE_FIELD_IF_MODIFIED_SINCE) {
        c->headers.tms = bolt_parse_time((char *)at, len);
    }

    return 0;
}
