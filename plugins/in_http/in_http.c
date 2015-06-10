/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*  Fluent Bit
 *  ==========
 *  Copyright (C) 2015 Treasure Data Inc.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <msgpack.h>
#include <fluent-bit/flb_input.h>
#include <fluent-bit/flb_config.h>

#include "in_http.h"

/* Init CPU input */
int in_http_init(struct flb_config *config)
{
    return 0;
}

/* Callback invoked after setup but before to join the main loop */
int in_http_pre_run(void *in_context, struct flb_config *config)
{
    return 0;
}

/* Callback to gather CPU usage between now and previous snapshot */
int in_http_collect(struct flb_config *config, void *in_context)
{
    return 0;
}

void *in_http_flush(void *in_context, int *size)
{
    return NULL;
}

/* Plugin reference */
struct flb_input_plugin in_http_plugin = {
    .name         = "http",
    .description  = "HTTP Service",
    .cb_init      = in_http_init,
    .cb_pre_run   = in_http_pre_run,
    .cb_collect   = in_http_collect,
    .cb_flush_buf = in_http_flush
};