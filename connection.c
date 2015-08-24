#include <stdlib.h>
#include "bolt.h"
#include "connection.h"

#define BOLT_MAX_FREE_CONNECTIONS  1024

static int freeconn_count;
static struct list_head freeconn_list;


void bolt_init_connections()
{
    freeconn_count = 0;
    INIT_LIST_HEAD(&freeconn_list);
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
        struct list_entry *e;

        e = freeconn_list.next;
        c = list_entry(e, bolt_connection_t, link);
        list_del(e);

        freeconn_count--;

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

    if (freeconn_count > BOLT_MAX_FREE_CONNECTIONS) {
        free(c);
        return;
    }

    /* Link in free connections list */
    list_add(&c->link, &freeconn_list);

    freeconn_count++;
}


void
bolt_connection_recv_handler(int sock, short event, void *arg)
{
    bolt_connection_t *c = (bolt_connection_t *)arg;
    int nbytes, would, retval;
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
        return;

    } else if (nbytes == 0) {
        bolt_free_connection(conn);
        return;
    }

    c->rpos += nbytes;

agian:
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

        if (c->recv_state == BOLT_HTTP_STATE_CRLFCRLF) {
            retval = http_parser_execute(&c->hp, &http_parser_callbacks,
                                         c->rbuf, c->rlast - conn->rbuf);

            if (c->hp.method != HTTP_GET) {
                bolt_free_connection(c);
                return;
            }

            bolt_connection_process(c);

            if (c->rpos > c->rlast) {
                int move = c->rpos - c->rlast;

                memmove(c->rbuf, c->rlast, move);

                c->rlast = c->rbuf;
                c->rpos = c->rbuf + move;

                goto agian;

            } else {
                c->rlast = c->rpos = c->rbuf;
                break;
            }
        }
    }
}

