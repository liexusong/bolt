#include <stdlib.h>
#include <wand/magick_wand.h>
#include "bolt.h"

typedef struct {
    int width;
    int height;
    int quality;
    char path[BOLT_FILENAME_LENGTH];
} bolt_compress_t;


static bolt_compress_t compress_work;


int
bolt_worker_parse_task(bolt_task_t *task)
{
    
}


char *
bolt_worker_compress(char *path, int quality,
    int width, int height, int *length)
{
    MagickWand *wand = NULL;
    int orig_width, orig_height;
    float rate1, rate2;
    char *blob;

    MagickWandGenesis();

    wand = NewMagickWand();
    if (!wand) {
        goto failed;
    }

    if (MagickReadImage(wand, path) == MagickFalse) {
        goto failed;
    }

    orig_width = MagickGetImageWidth(wand);
    orig_height = MagickGetImageHeight(wand);

    if (width <= 0) {
        width = orig_width;
    }

    if (height <= 0) {
        height = orig_height;
    }

    rate1 = (float)width / (float)orig_width;
    rate2 = (float)height / (float)orig_height;

    if (rate1 <= rate2) {
        height = (float)width * ((float)orig_height / (float)orig_width);
    } else {
        width = (float)height * ((float)orig_width / (float)orig_height);
    }

    if (MagickResizeImage(wand, width, height, CatromFilter, 1)
        == MagickFalse)
    {
        goto failed;
    }

    if (MagickSetImageCompression(wand, JPEGCompression) == MagickFalse) {
        goto failed;
    }

    if (MagickSetImageCompressionQuality(wand, quality) == MagickFalse) {
        goto failed;
    }

    if ((blob = MagickWriteImageBlob(wand, length)) == NULL) {
        goto failed;
    }

    DestroyMagickWand(wand);
    MagickWandTerminus();

    return blob;

failed:

    if (wand)
        DestroyMagickWand(wand);
    MagickWandTerminus();

    return NULL;
}


void *
bolt_worker_process(void *arg)
{
    bolt_task_t       *task;
    struct list_head  *e;
    char              *blob;
    int                length;
    bolt_cache_t      *cache;
    struct list_head  *waitq;
    int                wakeup;
    bolt_connection_t *c;

    for (;;) {
        wakeup = 0;

        pthread_mutex_lock(&service->task_lock);

        while (list_empty(&service->task_queue)) {
            pthread_cond_wait(&service->task_cond, &service->task_lock);
        }

        e = service->task_queue.next;
        task = list_entry(e, bolt_task_t, link);
        list_del(e);

        pthread_mutex_unlock(&service->task_lock);

        if (bolt_worker_parse_task(task) == -1) {
            goto error;
        }

        blob = bolt_worker_compress(compress_work.path, compress_work.quality,
                                    compress_work.width, compress_work.height,
                                    &length);
        if (NULL == blob
            || NULL == (cache = malloc(sizeof(*cache))))
        {
            goto error;
        }

        cache->size = length;
        cache->cache = blob;
        cache->refcount = 0;

        /* add to cache table and wakeup waiting connections */

        pthread_mutex_lock(&service->cache_lock);

        jk_hash_insert(&service->cache_htb,
                       task->filename, task->fnlen, cache);

        if (jk_hash_find(&service->waiting_htb,
             task->filename, task->fnlen, &waitq) == JK_HASH_OK)
        {
            list_for_each(e, &waitq->wait_conns) {
                c = list_entry(e, bolt_connection_t, link);
                __sync_add_and_fetch(&cache->refcount, 1);
                c->icache = cache;
                c->wakeup_go = BOLT_WAKEUP_SEND;
            }

            jk_hash_remove(&service->waiting_htb,
                           task->filename, task->fnlen);
            wakeup = 1;
        }

        pthread_mutex_unlock(&service->cache_lock);

        if (wakeup) {
            pthread_mutex_lock(&service->wakeup_lock);
            list_add(&waitq->link, &service->wakeup_queue);
            pthread_mutex_unlock(&service->wakeup_lock);

            write(&service->wakeup_notify[1], "\0", 1);
        }

        continue;

error:
        pthread_mutex_lock(&service->cache_lock);

        if (jk_hash_find(&service->waiting_htb,
             task->filename, task->fnlen, &waitq) == JK_HASH_OK)
        {
            jk_hash_remove(&service->waiting_htb,
                           task->filename, task->fnlen);
            wakeup = 1;
        }

        pthread_mutex_unlock(&service->cache_lock);

        if (wakeup) {
            list_for_each(e, &waitq->wait_conns) {
                c = list_entry(e, bolt_connection_t, link);
                c->wakeup_go = BOLT_WAKEUP_CLOSE;
            }

            pthread_mutex_lock(&service->wakeup_lock);
            list_add(&waitq->link, &service->wakeup_queue);
            pthread_mutex_unlock(&service->wakeup_lock);

            write(&service->wakeup_notify[1], "\0", 1);
        }
    }
}


int bolt_init_workers(int num)
{
    int cnt;
    pid_t tid;

    for (cnt = 0; cnt < num; cnt++) {
        if (pthread_create(&tid, NULL,
             bolt_worker_process, NULL) == -1)
        {
            return -1;
        }
    }

    return 0;
}

