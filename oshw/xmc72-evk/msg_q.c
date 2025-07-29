/*
 * This software is dual-licensed under GPLv3 and a commercial
 * license. See the file LICENSE.md distributed with this software for
 * full license information.
 */

#include "msg_q.h"
#include <stdio.h>
#include <string.h>

static inline size_t msgq_used_now(const msg_queue_t *q)
{
   size_t free_now = xMessageBufferSpacesAvailable(q->handle);
   return (q->capacity_bytes > free_now) ? (q->capacity_bytes - free_now) : 0;
}

bool msg_queue_init(msg_queue_t *q, size_t capacity_bytes)
{
   if (!q || capacity_bytes == 0) return false;

   q->handle = xMessageBufferCreate(capacity_bytes);
   if (q->handle == NULL) return false;

   q->capacity_bytes = capacity_bytes;
   q->total_enqueued = 0;
   q->dropped = 0;
   q->high_watermark = 0;
   q->logger_task = NULL;
   q->logger_ctx = NULL;

   q->isr_min_used = 0xFFFFFFFF;
   q->isr_max_used = 0;

   return true;
}

void msg_queue_deinit(msg_queue_t *q)
{
   if (!q) return;
#ifdef ENABLE_MSG_Q_LOGGER
   msg_queue_stop_logger(q);
#endif
   if (q->handle) vMessageBufferDelete(q->handle);
   memset(q, 0, sizeof(*q));
}

bool msg_queue_send_from_isr(msg_queue_t *q,
                             const void *data,
                             size_t len,
                             BaseType_t *xHigherPriorityTaskWoken)
{
   if (!q || q->handle == NULL || data == NULL || len == 0)
   {
      if (q) q->dropped++;
      return false;
   }

   size_t sent = xMessageBufferSendFromISR(q->handle, data, len, xHigherPriorityTaskWoken);

   if (sent == len)
   {
      q->total_enqueued++;

      size_t used_now = msgq_used_now(q);

      if (used_now > q->high_watermark) q->high_watermark = used_now;
      if (used_now < q->isr_min_used) q->isr_min_used = used_now;
      if (used_now > q->isr_max_used) q->isr_max_used = used_now;

      return true;
   }
   else
   {
      q->dropped++;
      return false;
   }
}

size_t msg_queue_receive(msg_queue_t *q,
                         void *dst,
                         size_t max_len,
                         TickType_t timeout_ticks)
{
   if (!q || q->handle == NULL || dst == NULL || max_len == 0) return 0;

   return xMessageBufferReceive(q->handle, dst, max_len, timeout_ticks);
}

#ifdef ENABLE_MSG_Q_LOGGER

typedef struct
{
   msg_queue_t *q;
   uint32_t interval_ms;
} msgq_logger_ctx_t;

static void msgq_logger_task(void *pv)
{
   msgq_logger_ctx_t *ctx = (msgq_logger_ctx_t *)pv;
   msg_queue_t *q = ctx->q;
   const TickType_t period = pdMS_TO_TICKS(ctx->interval_ms);

   for (;;)
   {
      vTaskDelay(period);
      size_t used_now = msgq_used_now(q);
      printf("[msg_queue] used=%lu/%lu  isr_min=%lu  isr_max=%lu  high_watermark=%lu  total=%lu  dropped=%lu\n",
             (unsigned long)used_now,
             (unsigned long)q->capacity_bytes,
             (unsigned long)q->isr_min_used,
             (unsigned long)q->isr_max_used,
             (unsigned long)q->high_watermark,
             (unsigned long)q->total_enqueued,
             (unsigned long)q->dropped);
   }
}

bool msg_queue_start_logger(msg_queue_t *q, uint32_t interval_ms, UBaseType_t priority)
{
   if (!q || q->handle == NULL) return false;
   if (q->logger_task) return true;

   msgq_logger_ctx_t *ctx = (msgq_logger_ctx_t *)pvPortMalloc(sizeof(*ctx));
   if (!ctx) return false;

   ctx->q = q;
   ctx->interval_ms = (interval_ms == 0) ? 1000u : interval_ms;

   BaseType_t ok = xTaskCreate(
       msgq_logger_task, "msgq_logger",
       configMINIMAL_STACK_SIZE + 128, ctx, priority, &q->logger_task);

   if (ok != pdPASS)
   {
      vPortFree(ctx);
      q->logger_task = NULL;
      return false;
   }

   q->logger_ctx = ctx;

   msg_queue_reset_isr_minmax(q);
   return true;
}

void msg_queue_stop_logger(msg_queue_t *q)
{
   if (!q) return;
   if (q->logger_task)
   {
      TaskHandle_t t = q->logger_task;
      q->logger_task = NULL;
      vTaskDelete(t);
   }
   if (q->logger_ctx)
   {
      vPortFree(q->logger_ctx);
      q->logger_ctx = NULL;
   }
}
#endif