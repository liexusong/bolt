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
#include "bolt.h"
#include "log.h"
#include "utils.h"

#define  BOLT_LINE_SIZE  1024


typedef int (bolt_conf_handler_t *)(char *, int);

typedef struct {
    char *name;
    bolt_conf_handler_t handler;
} bolt_conf_item_t;


static int bolt_conf_parse_host(char *value, int length);
static int bolt_conf_parse_prot(char *value, int length);
static int bolt_conf_parse_workers(char *value, int length);
static int bolt_conf_parse_logfile(char *value, int length);
static int bolt_conf_parse_logmark(char *value, int length);
static int bolt_conf_parse_maxcache(char *value, int length);
static int bolt_conf_parse_gcthreshold(char *value, int length);
static int bolt_conf_parse_path(char *value, int length);
static int bolt_conf_parse_watermark(char *value, int length);
static int bolt_conf_parse_daemon(char *value, int length);


static bolt_conf_item_t bolt_conf_imtes[] = {
    {"host",         bolt_conf_parse_host},
    {"port",         bolt_conf_parse_prot},
    {"workers",      bolt_conf_parse_workers},
    {"logfile",      bolt_conf_parse_logfile},
    {"logmark",      bolt_conf_parse_logmark},
    {"max-cache",    bolt_conf_parse_maxcache},
    {"gc-threshold", bolt_conf_parse_gcthreshold},
    {"path",         bolt_conf_parse_path},
    {"watermark",    bolt_conf_parse_watermark},
    {"daemon",       bolt_conf_parse_daemon},
    {NULL,           NULL},
};


static bolt_conf_item_t *
bolt_conf_find_item(char *name, int length)
{
    bolt_conf_item_t *found;

    for (found = bolt_conf_imtes; found->name != NULL; found++) {
        if (!strncmp(name, found->name, length)) {
            return found;
        }
    }

    return NULL;
}


static int
bolt_parse_conf(const char *start)
{
    enum {
        conf_want_name = 0,
        conf_read_name,
        conf_want_equal,
        conf_want_value,
        conf_read_value,
    } state = conf_want_name;
    char name[256], value[512];
    int noff = 0, voff = 0;
    bolt_conf_item_t *conf;

    for (; *start; start++) {
        switch (state) {
        case conf_want_name:
            if (*start == ' ' || *start == '\t') {
                continue;
            } else if (*start == '#') { /* skip comment */
                return 0;
            }

            state = conf_read_name;
            break;

        case conf_read_name:
            if (*start == ' ') {
                state = conf_want_equal;
            } else if (*start == '=') {
                state = conf_want_value;
            } else {
                name[noff++] = *start;
            }

            break;

        case conf_want_equal:
            if (*start == ' ' || *start == '\t') {
                continue;
            } else if (*start == '=') {
                state = conf_want_value;
            } else {
                return -1;
            }

            break;

        case conf_want_value:
            if (*start == ' ' || *start == '\t') {
                continue;
            } else {
                state = conf_read_value;
            }

            break;

        case conf_read_value:
            if (*start == ' '
                || *start == '\t'
                || *start == '\n'
                || *start == '\r'
                || *start == '#')
            {
                conf = bolt_conf_find_item(name, noff);
                if (!conf) {
                    return -1;
                }

                return conf->handler(value, voff);

            } else {
                value[voff++] = *start;
            }
        }
    }

    return -1;
}


int
bolt_read_confs(char *file)
{
    FILE *filep;
    char buf[BOLT_LINE_SIZE];
    int line = 1;

    filep = fopen(file, "r");
    if (NULL == filep) {
        fprintf(stderr, "Fatal: failed to open configure file `%s'\n", file);
        return -1;
    }

    while (fgets(buf, BOLT_LINE_SIZE, filep)) {
        if (bolt_parse_conf(buf) == -1) {
            fprintf(stderr, "Fatal: configure file error in line %d\n", line);
            fclose(filep);
            return -1;
        }
        line++;
    }

    fclose(filep);
    return 0;
}


/* Parse configure file callback handler */


static int
bolt_conf_parse_host(char *value, int length)
{
    setting->host = bolt_strndup(value, length);
    if (!setting->host) {
        return -1;
    }
    return 0;
}


static int
bolt_conf_parse_port(char *value, int length)
{
    int retval;

    retval = bolt_atoi(value, length, &setting->port);
    if (retval == -1) {
        return -1;
    }

    if (setting->port <= 0) {
        setting->port = 80;
    }

    return 0;
}


static int
bolt_conf_parse_workers(char *value, int length)
{
    int retval;

    retval = bolt_atoi(value, length, &setting->workers);
    if (retval == -1) {
        return -1;
    }

    if (setting->workers <= 0) {
        setting->workers = 5;
    }

    return 0;
}


static int
bolt_conf_parse_logfile(char *value, int length)
{
    setting->logfile = bolt_strndup(value, length);
    if (!setting->logfile) {
        return -1;
    }
    return 0;
}


static int
bolt_conf_parse_logmark(char *value, int length)
{
    if (!strncmp(value, "DEBUG", length)) {
        setting->logmark = BOLT_LOG_DEBUG;
    } else if (!strncmp(optarg, "NOTICE", length)) {
        setting->logmark = BOLT_LOG_NOTICE;
    } else if (!strncmp(optarg, "ALERT", length)) {
        setting->logmark = BOLT_LOG_ALERT;
    } else if (!strncmp(optarg, "ERROR", length)) {
        setting->logmark = BOLT_LOG_ERROR;
    } else {
        return -1;
    }
    return 0;
}


static int
bolt_conf_parse_maxcache(char *value, int length)
{
    int retval;

    retval = bolt_atoi(value, length, &setting->max_cache);
    if (retval == -1) {
        return -1;
    }

    if (setting->max_cache < BOLT_MIN_CACHE_SIZE) {
        setting->max_cache = BOLT_MIN_CACHE_SIZE;
    }

    return 0;
}


static int
bolt_conf_parse_gcthreshold(char *value, int length)
{
    int retval;

    retval = bolt_atoi(value, length, &setting->gc_threshold);
    if (retval == -1) {
        return -1;
    }

    if (setting->gc_threshold < 0
        || setting->gc_threshold >= 100)
    {
        setting->gc_threshold = 80;
    }

    return 0;
}


static int
bolt_conf_parse_path(char *value, int length)
{
    if (length <= 0) {
        return -1;
    }

    setting->path = bolt_strndup(value, length);
    if (!setting->path) {
        return -1;
    }

    setting->path_len = length;

    return 0;
}


static int
bolt_conf_parse_watermark(char *value, int length)
{
    setting->watermark = bolt_strndup(value, length);
    if (!setting->watermark) {
        return -1;
    }

    setting->watermark_enable = 1;

    return 0;
}


static int
bolt_conf_parse_daemon(char *value, int length)
{
    if (!strncasecmp(value, "yes", length)
        || !strncasecmp(value, "1", length)
        || !strncasecmp(value, "on", length))
    {
        setting->daemon = 1;
    } else {
        setting->daemon = 0;
    }

    return 0;
}
