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
#include "bolt.h"

pthread_mutex_t bolt_gc_lock;
pthread_cond_t  bolt_gc_cond;


void
bolt_gc_start()
{
    pthread_mutex_lock(&bolt_gc_lock);
    pthread_cond_signal(&bolt_gc_cond);
    pthread_mutex_unlock(&bolt_gc_lock);
}


void *
bolt_gc_process(void *arg)
{
    int freesize;
    struct list_head *e, *n;
    bolt_cache_t *cache;

    for (;;) {
        pthread_mutex_lock(&bolt_gc_lock);

        while (service->memused < setting->max_cache) {
            pthread_cond_wait(&bolt_gc_cond, &bolt_gc_lock);
        }

        pthread_mutex_unlock(&bolt_gc_lock);

        freesize = service->memused - 
                   (setting->max_cache * setting->gc_threshold / 100);

        /* GC begin (Would be lock cache hashtable) */

        pthread_mutex_lock(&service->cache_lock);

        list_for_each_safe(e, n, &service->gc_lru) {

            cache = list_entry(e, bolt_cache_t, link);
            if (cache->refcount > 0) { /* This cache using */
                continue;
            }

            list_del(e); /* Remove from GC LRU queue */

            /* Remove from cache hashtable */
            jk_hash_remove(service->cache_htb,
                           cache->filename, cache->fnlen);

            __sync_sub_and_fetch(&service->memused, cache->size);

            freesize -= cache->size;

            free(cache->cache);
            free(cache);

            if (freesize <= 0) {
                break;
            }
        }

        pthread_mutex_unlock(&service->cache_lock);
    }
}


int
bolt_init_gc()
{
    pthread_t tid;
 
    if (pthread_mutex_init(&bolt_gc_lock, NULL) == -1
        || pthread_cond_init(&bolt_gc_cond, NULL) == -1
        || pthread_create(&tid, NULL, bolt_gc_process, NULL) == -1)
    {
        return -1;
    }

    return 0;
}
