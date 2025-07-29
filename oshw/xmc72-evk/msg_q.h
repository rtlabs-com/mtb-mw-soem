/*
 * This software is dual-licensed under GPLv3 and a commercial
 * license. See the file LICENSE.md distributed with this software for
 * full license information.
 */

#ifndef MSG_Q_H
#define MSG_Q_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "FreeRTOS.h"
#include "task.h"
#include "message_buffer.h"

// #define ENABLE_MSG_Q_LOGGER

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
   MessageBufferHandle_t handle;
   size_t capacity_bytes;
   volatile uint32_t total_enqueued;
   volatile uint32_t dropped;
   size_t high_watermark; /* all-time max used (bytes) */

   /* Min/Max usage tracked ONLY from ISR sends */
   size_t isr_min_used; /* min bytes used, updated in ISR after send */
   size_t isr_max_used; /* max bytes used, updated in ISR after send */

   TaskHandle_t logger_task;
   void *logger_ctx;
} msg_queue_t;

/* Core API */
bool msg_queue_init(msg_queue_t *q, size_t capacity_bytes);
void msg_queue_deinit(msg_queue_t *q);

bool msg_queue_send_from_isr(msg_queue_t *q,
                             const void *data,
                             size_t len,
                             BaseType_t *xHigherPriorityTaskWoken);

size_t msg_queue_receive(msg_queue_t *q,
                         void *dst,
                         size_t max_len,
                         TickType_t timeout_ticks);

/* Stats helpers */
static inline uint32_t msg_queue_total(const msg_queue_t *q) { return q->total_enqueued; }
static inline uint32_t msg_queue_dropped(const msg_queue_t *q) { return q->dropped; }
static inline size_t msg_queue_high_watermark(const msg_queue_t *q) { return q->high_watermark; }
static inline size_t msg_queue_spaces_available(const msg_queue_t *q)
{
   return (q->handle) ? xMessageBufferSpacesAvailable(q->handle) : 0;
}

/* Reset ISR-tracked min/max to the current usage */
static inline void msg_queue_reset_isr_minmax(msg_queue_t *q)
{
   if (!q || !q->handle) return;
   size_t free_now = xMessageBufferSpacesAvailable(q->handle);
   size_t used_now = (q->capacity_bytes > free_now) ? (q->capacity_bytes - free_now) : 0;
   q->isr_min_used = used_now;
   q->isr_max_used = used_now;
}

#ifdef ENABLE_MSG_Q_LOGGER
bool msg_queue_start_logger(msg_queue_t *q, uint32_t interval_ms, UBaseType_t priority);
void msg_queue_stop_logger(msg_queue_t *q);
#endif

#ifdef __cplusplus
}
#endif
#endif // MSG_Q_H