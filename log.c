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
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include "log.h"

static char *bolt_error_titles[BOLT_LOG_ERROR + 1] = {
    "DEBUG", "NOTICE", "ALERT", "ERROR"
};

static FILE *bolt_log_fp = NULL;
static int bolt_log_mark = BOLT_LOG_DEBUG;
static int bolt_log_initialize = 0;
static char bolt_log_buffer[4096];


int
bolt_init_log(char *file, int mark)
{
    if (bolt_log_initialize) {
        return 0;
    }

    if (mark < BOLT_LOG_DEBUG
        || mark > BOLT_LOG_ERROR)
    {
        return -1;
    }

    if (file) {
        bolt_log_fp = fopen(file, "a+");
        if (!bolt_log_fp) {
            fprintf(stderr, "Fatal: Failed to create log file `%s'\n", file);
            return -1;
        }

    } else {
        bolt_log_fp = stderr;
    }

    bolt_log_mark = mark;
    bolt_log_initialize = 1;

    return 0;
}


void
bolt_log(int level, char *fmt, ...)
{
    va_list al;
    time_t current;
    struct tm *dt;
    int off1, off2;

    if (!bolt_log_initialize
        || level < bolt_log_mark
        || level > BOLT_LOG_ERROR)
    {
        return;
    }

    /* Get current date and time */
    time(&current);
    dt = localtime(&current);

    off1 = sprintf(bolt_log_buffer,
                   "[%04d-%02d-%02d %02d:%02d:%02d] %s: ",
                   dt->tm_year + 1900,
                   dt->tm_mon + 1,
                   dt->tm_mday,
                   dt->tm_hour,
                   dt->tm_min,
                   dt->tm_sec,
                   bolt_error_titles[level]);

    va_start(al, fmt);
    off2 = vsprintf(bolt_log_buffer + off1, fmt, al);
    va_end(al);

    bolt_log_buffer[off1 + off2] = '\n';

    fwrite(bolt_log_buffer, off1 + off2 + 1, 1, bolt_log_fp);
}


void bolt_destroy_log()
{
    if (!bolt_log_initialize) {
        return;
    }

    if (bolt_log_fp != stderr) {
        fclose(bolt_log_fp);
    }
}
