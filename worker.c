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
#include <wand/magick_wand.h>
#include "bolt.h"

typedef struct {
    int width;
    int height;
    int quality;
    char path[BOLT_FILENAME_LENGTH];
} bolt_compress_t;


bolt_compress_t *
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
    bolt_task_t       *task = NULL;
    bolt_compress_t   *work = NULL;
    struct list_head  *e;
    char              *blob;
    int                length;
    bolt_cache_t      *cache;
    struct list_head  *waitq;
    int                wakeup;
    bolt_connection_t *c;
    int                memory_used;
    int                http_code = 200;

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

        /* 1) Bad Request */
        if ((work = bolt_worker_parse_task(task)) == NULL) {
            http_code = 400;
            goto error;
        }

        /* 2) Not Found */
        if (!bolt_utils_file_exists(work->path)) {
            http_code = 404;
            goto error;
        }

        /* 3) Internal Server Error */
        blob = bolt_worker_compress(work.path, work.quality,
                                    work.width, work.height, &length);
        if (NULL == blob
            || NULL == (cache = malloc(sizeof(*cache))))
        {
            http_code = 500;
            goto error;
        }

        cache->size = length;
        cache->cache = blob;
        cache->refcount = 0;

        /* add to cache table and wakeup waiting connections */

        pthread_mutex_lock(&service->cache_lock);

        jk_hash_insert(&service->cache_htb,
                       task->filename, task->fnlen, cache);
        list_add_tail(&cache->link, &service->gc_lru);

        if (jk_hash_find(&service->waiting_htb,
             task->filename, task->fnlen, &waitq) == JK_HASH_OK)
        {
            list_for_each(e, &waitq->wait_conns) {
                c = list_entry(e, bolt_connection_t, link);
                __sync_add_and_fetch(&cache->refcount, 1);
                c->icache = cache;
                c->http_code = http_code; /* HTTP code 200 */
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

        memory_used = __sync_add_and_fetch(&service->memused, length);

        if (memory_used > setting->max_cache) { /* need start GC? */
            bolt_gc_start();
        }

        if (task) free(task);
        if (work) free(work);

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
                c->http_code = http_code;
            }

            pthread_mutex_lock(&service->wakeup_lock);
            list_add(&waitq->link, &service->wakeup_queue);
            pthread_mutex_unlock(&service->wakeup_lock);

            write(&service->wakeup_notify[1], "\0", 1);
        }

        if (task) free(task);
        if (work) free(work);
    }
}


int
bolt_init_workers(int num)
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

