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
#include <string.h>
#include <unistd.h>
#include <MagickWand/MagickWand.h>
#include "bolt.h"
#include "utils.h"
#include "time.h"

typedef struct {
    int  width;
    int  height;
    int  quality;
    char format[32];
    char path[BOLT_FILENAME_LENGTH];
} bolt_job_t;

static MagickWand *bolt_watermark_wand = NULL;
static int bolt_watermark_width;
static int bolt_watermark_height;

static char *support_formats[] = {
    "PNG", "JPG", "WEBP", NULL,
};

/**
 * Parse task to compress job
 * like: "ooooooooo-00x00_00.webp"
 */
bolt_job_t *
bolt_worker_get_job(bolt_task_t *task)
{
    enum {
        BOLT_PT_GET_EXT = 0,
        BOLT_PT_GET_QUALITY,
        BOLT_PT_GET_HEIGHT,
        BOLT_PT_GET_WIDTH,
        BOLT_PT_GET_FOUND,
    } state = BOLT_PT_GET_EXT;

    char *start = task->filename;
    char *ptail = task->filename + task->fnlen - 1;
    char *pcurr;
    char buf[32], format[32] = {0};
    int last = 0;
    char ch;
    int width = 0, height = 0, quality = 0;
    int fnlen;
    bolt_job_t *job;
    char *path;
    int plen;

    for (pcurr = ptail; pcurr >= start; pcurr--) {

        ch = *pcurr;

        switch (state) {

        case BOLT_PT_GET_EXT:

            if (ch == '.') {
                int len = ptail - pcurr;

                if (len > 0 && len < 32) {
                    memcpy(format, pcurr + 1, len);
                    bolt_strtoupper(format, len);
                } else {
                    return NULL;
                }

                state = BOLT_PT_GET_QUALITY;
                ptail = pcurr - 1;
            }

            break;

        case BOLT_PT_GET_QUALITY:

            if (ch == '_') {
                int len = ptail - pcurr;

                if (len > 0) {
                    memcpy(buf, pcurr + 1, len);
                    buf[len] = 0;
                    quality = atoi(buf);
                }

                if (quality <= 0) {
                    return NULL;
                }

                state = BOLT_PT_GET_HEIGHT;
                ptail = pcurr - 1;
            }

            break;

        case BOLT_PT_GET_HEIGHT:

            if (ch == 'x') {
                int len = ptail - pcurr;

                if (len > 0) {
                    memcpy(buf, pcurr + 1, len);
                    buf[len] = 0;
                    height = atoi(buf);
                }

                if (height <= 0) {
                    return NULL;
                }

                state = BOLT_PT_GET_WIDTH;
                ptail = pcurr - 1;
            }

            break;

        case BOLT_PT_GET_WIDTH:

            if (ch == '-') {
                int len = ptail - pcurr;

                if (len > 0) {
                    memcpy(buf, pcurr + 1, len);
                    buf[len] = 0;
                    width = atoi(buf);
                }

                if (width <= 0) {
                    return NULL;
                }

                state = BOLT_PT_GET_FOUND;
            }

            break;

        default:
            break;
        }

        if (state == BOLT_PT_GET_FOUND) {
            break;
        }
    }

    fnlen = pcurr - start;

    /* File name format invalid or empty */

    if (state != BOLT_PT_GET_FOUND
        || fnlen <= 0
        || setting->path_len + fnlen + 1 > BOLT_FILENAME_LENGTH)
    {
        return NULL;
    }

    job = malloc(sizeof(*job));
    if (job == NULL) {
        return NULL;
    }

    job->width   = width;
    job->height  = height;
    job->quality = quality;

    strcpy(job->format, format); /* Copy changing extension name */

    path = setting->path;
    plen = setting->path_len;

    memcpy(job->path, path, plen);

    last += plen;
    memcpy(job->path + last, start, fnlen);

    last += fnlen;
    memcpy(job->path + last, ".jpg\0", 5);

    return job;
}

int bolt_format_support(char *format)
{
    char **fmt = support_formats;

    for (; *fmt; fmt++) {
        if (!strcmp(format, *fmt)) {
            return 0;
        }
    }
    return -1;
}

char *
bolt_worker_compress(
    char *path, int quality, int width,
    int height, char *format, int *length)
{
    MagickWand *wand = NULL;
    int orig_width, orig_height;
    float rate1, rate2;
    char *blob;

    wand = NewMagickWand();
    if (!wand) {
        bolt_log(BOLT_LOG_ERROR, "Failed to create Magick object");
        goto failed;
    }

    if (MagickReadImage(wand, path) == MagickFalse) {
        bolt_log(BOLT_LOG_ERROR, "Failed to read image from `%s'", path);
        goto failed;
    }

    orig_width  = MagickGetImageWidth(wand);
    orig_height = MagickGetImageHeight(wand);

    if (setting->watermark_enable) { /* Watermark process */
        int wm_x, wm_y;

        if (orig_width > bolt_watermark_width + BOLT_WATERMARK_PADDING
            && orig_height > bolt_watermark_height + BOLT_WATERMARK_PADDING)
        {
            int ret;

            wm_x = orig_width - bolt_watermark_width - BOLT_WATERMARK_PADDING;
            wm_y = orig_height - bolt_watermark_height - BOLT_WATERMARK_PADDING;

            ret = MagickCompositeImage(wand, bolt_watermark_wand,
                       MagickGetImageCompose(bolt_watermark_wand),
                       MagickFalse, wm_x, wm_y);

            if (ret == MagickFalse) {
                bolt_log(BOLT_LOG_ERROR,
                         "Failed to add water mark to image `%s'", path);
            }
        }
    }

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

    if (MagickResizeImage(wand, width, height, CatromFilter) == MagickFalse) {
        bolt_log(BOLT_LOG_ERROR, "Failed to resize image `%s'", path);
        goto failed;
    }

    if (MagickSetImageCompressionQuality(wand, quality) == MagickFalse) {
        goto failed;
    }

    if (bolt_format_support(format) == -1) {
        format = "JPG";
    }

    if (MagickSetImageFormat(wand, format) == MagickFalse) {
        bolt_log(BOLT_LOG_ERROR,
                 "Failed to set image to %s format `%s'", format, path);
        goto failed;
    }

    if ((blob = MagickGetImageBlob(wand, (size_t *)length)) == NULL) {
        bolt_log(BOLT_LOG_ERROR, "Failed to read image blob `%s'", path);
        goto failed;
    }

    DestroyMagickWand(wand);

    return blob;

failed:

    if (wand) DestroyMagickWand(wand);

    return NULL;
}

/*
 * Wakeup wait queue and send cache(locked) to client
 */
static void bolt_wakeup_cache_locked(char *queuename,
    int namelen, bolt_cache_t *cache, int http_code)
{
    struct list_head *e;
    bolt_wait_queue_t *waitq;
    bolt_connection_t *c;
    int wakeup = 0;
    int retval;

    LOCK_WAITQUEUE();

    retval = jk_hash_find(service->waiting_htb,
                          queuename, namelen, (void **)&waitq);

    if (retval == JK_HASH_OK) {

        list_for_each(e, &waitq->wait_conns) {
            c = list_entry(e, bolt_connection_t, link);

            c->http_code = http_code;

            if (cache) {
                cache->refcount++;
                cache->last = service->current_time;

                c->icache = cache;
            }
        }

        jk_hash_remove(service->waiting_htb, queuename, namelen);

        wakeup = 1;
    }

    UNLOCK_WAITQUEUE();

    if (wakeup) {

        LOCK_WAKEUP();
        list_add(&waitq->link, &service->wakeup_queue);
        UNLOCK_WAKEUP();

        write(service->wakeup_notify[1], "\0", 1);
    }
}

void *
bolt_worker_process(void *arg)
{
    bolt_task_t       *task = NULL;
    bolt_job_t        *work = NULL;
    struct list_head  *e;
    char              *blob;
    int                size;
    bolt_cache_t      *cache, *ocache;
    int                http_code;
    int                retval;

    for (;;) {

        /* Get a task from queue */

        LOCK_TASK();

        while (list_empty(&service->task_queue)) {
            pthread_cond_wait(&service->task_cond, &service->task_lock);
        }

        e = service->task_queue.next;

        task = list_entry(e, bolt_task_t, link);

        list_del(e);

        UNLOCK_TASK();

        if (!task) continue;

        /* 1) Bad Request */

        if ((work = bolt_worker_get_job(task)) == NULL) {
            http_code = 400;
            bolt_log(BOLT_LOG_DEBUG,
                     "Request file format was invaild `%s'", task->filename);
            goto fatal;
        }

        /* 2) Not Found */

        if (!bolt_file_exists(work->path)) {
            http_code = 404;
            bolt_log(BOLT_LOG_DEBUG,
                     "Request file was not found `%s'", work->path);
            goto fatal;
        }

        /* 3) Internal Server Error */

        blob = bolt_worker_compress(work->path,
                                    work->quality,
                                    work->width,
                                    work->height,
                                    work->format,
                                    &size);

        if (NULL == blob || NULL == (cache = malloc(sizeof(*cache)))) {

            if (blob) free(blob);

            http_code = 500;

            bolt_log(BOLT_LOG_ERROR,
                     "Failed to compress file `%s'", task->filename);

            goto fatal;
        }

        cache->size = size;
        cache->cache = blob;
        cache->refcount = 0;
        cache->flags = CACHE_FLAG_INUSED;
        cache->time = service->current_time;
        cache->life_time = cache->time + setting->cache_life;
        cache->fnlen = task->fnlen;

        memcpy(cache->filename, task->filename, cache->fnlen);

        bolt_format_time(cache->datetime, cache->time);

        /* Lock cache here */

        LOCK_CACHE();

        /* Try to get cache because the cache may be existsed (not often) */

        retval = jk_hash_find(service->cache_htb,
                              task->filename, task->fnlen, (void **)&ocache);

        if (retval == JK_HASH_OK) {

            bolt_wakeup_cache_locked(task->filename,
                                     task->fnlen, ocache, 200);

            UNLOCK_CACHE();

            free(task);
            free(work);
            free(cache->cache);
            free(cache);

            continue;
        }

        retval = jk_hash_insert(service->cache_htb,
                                task->filename, task->fnlen,
                                (void *)cache, 0);

        if (retval == JK_HASH_OK) {

            /* Add LRU list */
            list_add_tail(&cache->link, &service->gc_lru);

            http_code = 200;

            service->memory_usage += size;

        } else {
            free(cache->cache);
            free(cache);

            cache = NULL;
            http_code = 500;

            bolt_log(BOLT_LOG_ERROR, "Failed to add cache to hash table");
        }

        bolt_wakeup_cache_locked(task->filename,
                                 task->fnlen, cache, http_code);

        UNLOCK_CACHE();

        free(task);
        free(work);

        continue;

fatal:

        bolt_wakeup_cache_locked(task->filename,
                                 task->fnlen, NULL, http_code);

        if (work) free(work);

        free(task);
    }
}

int
bolt_worker_pass_task(bolt_connection_t *c)
{
    bolt_task_t *task;

    task = (bolt_task_t *)malloc(sizeof(*task));
    if (NULL == task) {
        bolt_log(BOLT_LOG_ERROR, "Not enough memory to alloc task struct");
        return -1;
    }

    memcpy(task->filename, c->filename, c->fnlen);
    task->filename[c->fnlen] = 0;
    task->fnlen = c->fnlen;

    LOCK_TASK();

    list_add(&task->link, &service->task_queue);

    pthread_cond_signal(&service->task_cond);

    UNLOCK_TASK();

    return 0;
}

void *
bolt_gc_thread(void *arg)
{
    char byte;
    int freesize, tofree;
    struct list_head *e, *n;
    bolt_cache_t *cache;

    for (;;) {

        if (read(service->gc_notify[0], (char *)&byte, 1) != 1) {
            continue;
        }

        LOCK_CACHE();

        freesize = service->memory_usage
                 - (setting->max_cache * setting->gc_threshold / 100);

        tofree = freesize;

        list_for_each_safe(e, n, &service->gc_lru) {

            if (tofree <= 0) {
                break;
            }

            cache = list_entry(e, bolt_cache_t, link);

            /* If this cache in used by client skip it */
            if (cache->refcount > 0) {
                continue;
            }

            list_del(e); /* Remove from GC LRU queue */

            /* Remove from cache hash table */
            jk_hash_remove(service->cache_htb, cache->filename, cache->fnlen);

            service->memory_usage -= cache->size;

            tofree -= cache->size;

            free(cache->cache);
            free(cache);
        }

        UNLOCK_CACHE();

        bolt_log(BOLT_LOG_DEBUG, "Freed `%d' bytes by GC thread", freesize);
    }

    return NULL;
}

int
bolt_init_workers(int num)
{
    int cnt;
    pthread_t tid;

    MagickWandGenesis(); /* Init ImageMagick */

    if (setting->watermark_enable) {

        bolt_watermark_wand = NewMagickWand();

        if (bolt_watermark_wand) {
            if (MagickReadImage(bolt_watermark_wand, setting->watermark)
                == MagickFalse)
            {
                bolt_log(BOLT_LOG_ERROR,
                         "Failed to get watermark from image file `%s'",
                         setting->watermark);
                return -1;
            }

        } else {
            bolt_log(BOLT_LOG_ERROR,
                     "Failed to create ImageMagickWand object");
            return -1;
        }

        bolt_watermark_width  = MagickGetImageWidth(bolt_watermark_wand);
        bolt_watermark_height = MagickGetImageHeight(bolt_watermark_wand);
    }

    for (cnt = 0; cnt < num; cnt++) {
        if (pthread_create(&tid, NULL, bolt_worker_process, NULL) == -1) {
            bolt_log(BOLT_LOG_ERROR, "Failed to create worker thread");
            return -1;
        }
    }

    if (pthread_create(&tid, NULL, bolt_gc_thread, NULL) == -1) {
        bolt_log(BOLT_LOG_ERROR, "Failed to create GC thread");
        return -1;
    }

    return 0;
}
