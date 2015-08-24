#include <stdlib.h>
#include "bolt.h"

#define BOLT_MAX_FREE_CONNECTIONS  1024


void bolt_free_connection(bolt_connection_t *c);


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
        if (event_add(&c->revent, NULL) == -1)
            return -1;
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
        if (event_add(&c->wevent, NULL) == -1)
            return -1;
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
    c->revset = 0;
    c->wevset = 0;
    c->rpos = c->rbuf;
    c->rend = c->rbuf + BOLT_RBUF_SIZE;
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

    list_add(&c->link, &freeconn_list);

    freeconn_count++;
}

