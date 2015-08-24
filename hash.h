/*
 * Copyright (c) 2012 - 2013, Liexusong <280259971@qq.com>
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

#ifndef __JK_HASH_H
#define __JK_HASH_H

#define  JK_HASH_OK              (0)
#define  JK_HASH_ERR             (-1)
#define  JK_HASH_DUPLICATE_KEY   (-2)

#define  JK_HASH_BUCKETS_MIN_SIZE   7
#define  JK_HASH_BUCKETS_MAX_SIZE   2147483647

typedef long jk_hash_hash_fn(char *, int);
typedef void jk_hash_free_fn(void *);

typedef struct jk_hash_s {
    jk_hash_hash_fn *hash;
    jk_hash_free_fn *free;
    void **buckets;
    unsigned int buckets_size;
    unsigned int elm_nums;
} jk_hash_t;

typedef struct jk_hash_entry_s jk_hash_entry_t;

struct jk_hash_entry_s {
    int hashval;
    int klen;
    void *data;
    jk_hash_entry_t *next;
    char key[0];
};


int jk_hash_init(jk_hash_t *o, unsigned int init_buckets, jk_hash_hash_fn *hash,
    jk_hash_free_fn *free);
jk_hash_t *jk_hash_new(unsigned int init_buckets, jk_hash_hash_fn *hash,
    jk_hash_free_fn *free);
int jk_hash_find(jk_hash_t *o, char *key, int klen, void **ret);
int jk_hash_insert(jk_hash_t *o, char *key, int klen, void *data, int replace);
int jk_hash_remove(jk_hash_t *o, char *key, int klen);
void jk_hash_destroy(jk_hash_t *o);
void jk_hash_free(jk_hash_t *o);

#endif
