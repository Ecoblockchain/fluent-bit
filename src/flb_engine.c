/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*  Fluent Bit
 *  ==========
 *  Copyright (C) 2015-2016 Treasure Data Inc.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES ORc CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/uio.h>

#include <mk_core.h>
#include <fluent-bit/flb_info.h>
#include <fluent-bit/flb_bits.h>

#include <fluent-bit/flb_macros.h>
#include <fluent-bit/flb_input.h>
#include <fluent-bit/flb_output.h>
#include <fluent-bit/flb_error.h>
#include <fluent-bit/flb_utils.h>
#include <fluent-bit/flb_config.h>
#include <fluent-bit/flb_engine.h>
#include <fluent-bit/flb_engine_dispatch.h>
#include <fluent-bit/flb_task.h>
#include <fluent-bit/flb_router.h>
#include <fluent-bit/flb_http_server.h>
#include <fluent-bit/flb_buffer.h>
#include <fluent-bit/flb_buffer_qchunk.h>
#include <fluent-bit/flb_scheduler.h>

#ifdef FLB_HAVE_BUFFERING
#include <fluent-bit/flb_buffer_chunk.h>
#endif

#ifdef FLB_HAVE_STATS
#include <fluent-bit/flb_stats.h>
#endif

int flb_engine_destroy_tasks(struct mk_list *tasks)
{
    int c = 0;
    struct mk_list *tmp;
    struct mk_list *head;
    struct flb_task *task;

    mk_list_foreach_safe(head, tmp, tasks) {
        task = mk_list_entry(head, struct flb_task, _head);
        flb_task_destroy(task);
        c++;
    }

    return c;
}

int flb_engine_flush(struct flb_config *config,
                     struct flb_input_plugin *in_force)
{
    struct flb_input_instance *in;
    struct flb_input_plugin *p;
    struct mk_list *head;

    mk_list_foreach(head, &config->inputs) {
        in = mk_list_entry(head, struct flb_input_instance, _head);
        p = in->p;

        if (in_force != NULL && p != in_force) {
            continue;
        }
        flb_engine_dispatch(0, in, config);
    }

    return 0;
}

static inline int consume_byte(int fd)
{
    int ret;
    uint64_t val;

    /* We need to consume the byte */
    ret = read(fd, &val, sizeof(val));
    if (ret <= 0) {
        flb_errno();
        return -1;
    }

    return 0;
}

static inline int flb_engine_manager(int fd, struct flb_config *config)
{
    int ret;
    int bytes;
    int task_id;
    int thread_id;
    int retry_seconds;
    uint32_t type;
    uint32_t key;
    uint64_t val;
    struct flb_task *task;
    struct flb_output_thread *out_th;

    bytes = read(fd, &val, sizeof(val));
    if (bytes == -1) {
        flb_errno();
        return -1;
    }

    /* Get type and key */
    type = FLB_BITS_U64_HIGH(val);
    key  = FLB_BITS_U64_LOW(val);

    /* Flush all remaining data */
    if (type == 1) {                  /* Engine type */
        if (key == FLB_ENGINE_STOP) {
            flb_trace("[engine] flush enqueued data");
            flb_engine_flush(config, NULL);
#ifdef FLB_HAVE_BUFFERING
            if (config->buffer_ctx) {
                flb_buffer_stop(config->buffer_ctx);
            }
#endif
            return FLB_ENGINE_STOP;
        }
    }
    else if (type == FLB_ENGINE_IN_THREAD) {
        /* Event coming from an input thread */
        flb_input_thread_destroy_id(key, config);
    }
    else if (type == FLB_ENGINE_TASK) {
        /*
         * The notion of ENGINE_TASK is associated to outputs. All thread
         * references below belongs to flb_output_thread's.
         */
        ret       = FLB_TASK_RET(key);
        task_id   = FLB_TASK_ID(key);
        thread_id = FLB_TASK_TH(key);

#ifdef FLB_HAVE_TRACE
        char *trace_st = NULL;

        if (ret == FLB_OK) {
            trace_st = "OK";
        }
        else if (ret == FLB_ERROR) {
            trace_st = "ERROR";
        }
        else if (ret == FLB_RETRY) {
            trace_st = "RETRY";
        }

        flb_trace("%s[engine] [task event]%s task_id=%i thread_id=%i return=%s",
                  ANSI_YELLOW, ANSI_RESET,
                  task_id, thread_id, trace_st);
#endif

        task   = config->tasks_map[task_id].task;
        out_th = flb_output_thread_get(thread_id, task);

        /* A thread has finished, delete it */
        if (ret == FLB_OK) {
#ifdef FLB_HAVE_BUFFERING
            if (config->buffer_path) {
                flb_buffer_chunk_pop(config->buffer_ctx, thread_id, task);
            }
#endif
            flb_task_retry_clean(task, out_th->parent);
            flb_output_thread_destroy_id(thread_id, task);
            if (task->users == 0) {
                flb_task_destroy(task);
            }
        }
        else if (ret == FLB_RETRY) {
            /* Create a Task-Retry */
            struct flb_task_retry *retry;

            retry = flb_task_retry_create(task, out_th);
            if (!retry) {
                /*
                 * It can fail in two situations:
                 *
                 * - No enough memory (unlikely)
                 * - It reached the maximum number of re-tries
                 */
#ifdef FLB_HAVE_BUFFERING
                if (config->buffer_path) {
                    flb_buffer_chunk_pop(config->buffer_ctx, thread_id, task);
                }
#endif
                flb_output_thread_destroy_id(thread_id, task);
                if (task->users == 0) {
                    flb_task_destroy(task);
                }

                return 0;
            }

            /* Always destroy the old thread */
            flb_output_thread_destroy_id(thread_id, task);

            /* Let the scheduler to retry the failed task/thread */
            retry_seconds = flb_sched_request_create(config,
                                                     retry, retry->attemps);

            /*
             * If for some reason the Scheduler could not include this retry,
             * we need to get rid of it, likely this is because of not enough
             * memory available or we ran out of file descriptors.
             */
            if (retry_seconds == -1) {
                flb_warn("[sched] retry for task %i could not be scheduled",
                         task->id);
                flb_task_retry_destroy(retry);
                if (task->users == 0) {
                    flb_task_destroy(task);
                }
            }
            else {
                flb_debug("[sched] retry=%p %i in %i seconds",
                          retry, task->id, retry_seconds);
            }
        }
        else if (ret == FLB_ERROR) {
            flb_output_thread_destroy_id(thread_id, task);
            if (task->users == 0) {
                flb_task_destroy(task);
            }
        }
    }
#ifdef FLB_HAVE_BUFFERING
    else if (type == FLB_ENGINE_BUFFER) {
        flb_buffer_engine_event(config->buffer_ctx, val);
        /* CONTINUE:
         *
         * - create messages types for buffering interface
         * - let qchunk ingest data here
         */
    }
#endif

    return 0;
}

static FLB_INLINE int flb_engine_handle_event(int fd, int mask,
                                              struct flb_config *config)
{
    int ret;

    if (mask & MK_EVENT_READ) {
        /* Check if we need to flush */
        if (config->flush_fd == fd) {
            consume_byte(fd);
            flb_engine_flush(config, NULL);
#ifdef FLB_HAVE_BUFFERING
            /*
             * Upon flush request, let the buffering interface to enqueue
             * a new buffer chunk.
             */
            if (config->buffer_ctx) {
                flb_buffer_qchunk_signal(FLB_BUFFER_QC_PUSH_REQUEST, 0,
                                         config->buffer_ctx->qworker);
            }
#endif
            return 0;
        }
        else if (config->shutdown_fd == fd) {
            return FLB_ENGINE_SHUTDOWN;
        }
#ifdef FLB_HAVE_STATS
        else if (config->stats_fd == fd) {
            consume_byte(fd);
            return FLB_ENGINE_STATS;
        }
#endif
        else if (config->ch_manager[0] == fd) {
            ret = flb_engine_manager(fd, config);
            if (ret == FLB_ENGINE_STOP) {
                return FLB_ENGINE_STOP;
            }
        }

        /* Try to match the file descriptor with a collector event */
        ret = flb_input_collector_fd(fd, config);
        if (ret != -1) {
            return ret;
        }
    }

    return 0;
}

static int flb_engine_started(struct flb_config *config)
{
    uint64_t val;

    /* Check the channel is valid (enabled by library mode) */
    if (config->ch_notif[1] <= 0) {
        return -1;
    }

    val = FLB_ENGINE_EV_STARTED;
    return write(config->ch_notif[1], &val, sizeof(uint64_t));
}

int flb_engine_start(struct flb_config *config)
{
    int fd;
    int ret;
    struct mk_list *head;
    struct mk_event *event;
    struct mk_event_loop *evl;
    struct flb_input_collector *collector;

    /* HTTP Server */
#ifdef FLB_HAVE_HTTP
    if (config->http_server == FLB_TRUE) {
        flb_http_server_start(config);
    }
#endif

    flb_info("[engine] started");
    flb_thread_prepare();

    /* Create the event loop and set it in the global configuration */
    evl = mk_event_loop_create(256);
    if (!evl) {
        return -1;
    }
    config->evl = evl;

    /*
     * Create a communication channel: this routine creates a channel to
     * signal the Engine event loop. It's useful to stop the event loop
     * or to instruct anything else without break.
     */
    ret = mk_event_channel_create(config->evl,
                                  &config->ch_manager[0],
                                  &config->ch_manager[1],
                                  config);
    if (ret != 0) {
        flb_error("[engine] could not create manager channels");
        exit(EXIT_FAILURE);
    }

    /* Initialize input plugins */
    flb_input_initialize_all(config);

    /* Inputs pre-run */
    flb_input_pre_run_all(config);

    /* Outputs pre-run */
    ret = flb_output_init(config);
    if (ret == -1) {
        flb_engine_shutdown(config);
        return -1;
    }

    flb_output_pre_run(config);

    /* Create and register the timer fd for flush procedure */
    event = &config->event_flush;
    event->mask = MK_EVENT_EMPTY;
    event->status = MK_EVENT_NONE;

    config->flush_fd = mk_event_timeout_create(evl, config->flush, 0, event);
    if (config->flush_fd == -1) {
        flb_utils_error(FLB_ERR_CFG_FLUSH_CREATE);
    }

    /* Initialize the stats interface (just if FLB_HAVE_STATS is defined) */
    flb_stats_init(config);

    /* For each Collector, register the event into the main loop */
    mk_list_foreach(head, &config->collectors) {
        collector = mk_list_entry(head, struct flb_input_collector, _head);
        event = &collector->event;

        if (collector->type == FLB_COLLECT_TIME) {
            event->mask = MK_EVENT_EMPTY;
            event->status = MK_EVENT_NONE;
            fd = mk_event_timeout_create(evl, collector->seconds,
                                         collector->nanoseconds, event);
            if (fd == -1) {
                continue;
            }
            collector->fd_timer = fd;
        }
        else if (collector->type & (FLB_COLLECT_FD_EVENT | FLB_COLLECT_FD_SERVER)) {
            event->fd     = collector->fd_event;
            event->mask   = MK_EVENT_EMPTY;
            event->status = MK_EVENT_NONE;

            ret = mk_event_add(evl,
                               collector->fd_event,
                               FLB_ENGINE_EV_CORE,
                               MK_EVENT_READ, event);
            if (ret == -1) {
                close(collector->fd_event);
                continue;
            }
        }
    }

    /* Prepare routing paths */
    ret = flb_router_io_set(config);
    if (ret == -1) {
        return -1;
    }

    /* Enable Buffering Support */
#ifdef FLB_HAVE_BUFFERING
    struct flb_buffer *buf_ctx;

    /* If a path exists, initialize the Buffer service and workers */
    if (config->buffer_path) {
        buf_ctx = flb_buffer_create(config->buffer_path,
                                    config->buffer_workers,
                                    config);
        if (!buf_ctx) {
            flb_error("[engine] could not initialize buffer workers");
            return -1;
        }

        /* Start buffer engine workers */
        config->buffer_ctx = buf_ctx;
        ret = flb_buffer_start(config->buffer_ctx);
        if (ret == -1) {
            flb_error("[buffer] buffering could not start");
            return -1;
        }
    }
#endif

    /* Signal that we have started */
    flb_engine_started(config);
    while (1) {
        mk_event_wait(evl);
        mk_event_foreach(event, evl) {
            if (event->type == FLB_ENGINE_EV_CORE) {
                ret = flb_engine_handle_event(event->fd, event->mask, config);
                if (ret == FLB_ENGINE_STOP) {
                    /*
                     * We are preparing to shutdown, we give a graceful time
                     * of 5 seconds to process any pending event.
                     */
                    event = &config->event_shutdown;
                    event->mask = MK_EVENT_EMPTY;
                    event->status = MK_EVENT_NONE;
                    config->shutdown_fd = mk_event_timeout_create(evl, 5, 0, event);
                    flb_warn("[engine] service will stop in 5 seconds");
                }
                else if (ret == FLB_ENGINE_SHUTDOWN) {
                    flb_info("[engine] service stopped");
                    return flb_engine_shutdown(config);
                }
#ifdef FLB_HAVE_STATS
                else if (ret == FLB_ENGINE_STATS) {
                    //flb_stats_collect(config);
                }
#endif
            }
            else if (event->type == FLB_ENGINE_EV_SCHED) {
                /* Event type registered by the Scheduler */
                flb_sched_event_handler(config, event);
            }
            else if (event->type == FLB_ENGINE_EV_CUSTOM) {
                event->handler(event);
            }
#if defined (FLB_HAVE_FLUSH_UCONTEXT) || defined (FLB_HAVE_FLUSH_LIBCO)
            else if (event->type == FLB_ENGINE_EV_THREAD) {
                struct flb_upstream_conn *u_conn;
                struct flb_thread *th;

                /*
                 * Check if we have some co-routine associated to this event,
                 * if so, resume the co-routine
                 */
                u_conn = (struct flb_upstream_conn *) event;
                th = u_conn->thread;
                flb_trace("[engine] resuming thread=%p", th);
                flb_thread_resume(th);
            }
#endif
        }
    }
}

/* Release all resources associated to the engine */
int flb_engine_shutdown(struct flb_config *config)
{

#ifdef FLB_HAVE_BUFFERING
    if (config->buffer_ctx) {
        flb_buffer_stop(config->buffer_ctx);
    }
#endif

    /* router */
    flb_router_exit(config);

    /* cleanup plugins */
    flb_input_exit_all(config);
    flb_output_exit(config);

    flb_config_exit(config);

    return 0;
}
