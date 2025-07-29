/*********************************************************************
 *        _       _         _
 *  _ __ | |_  _ | |  __ _ | |__   ___
 * | '__|| __|(_)| | / _` || '_ \ / __|
 * | |   | |_  _ | || (_| || |_) |\__ \
 * |_|    \__|(_)|_| \__,_||_.__/ |___/
 *
 * www.rt-labs.com
 * Copyright 2025 rt-labs AB, Sweden.
 *
 * This software is licensed under the terms of the BSD 3-clause
 * license. See the file LICENSE distributed with this software for
 * full license information.
 ********************************************************************/

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <assert.h>

/* add system specific types prior to including osal.h */
#include "sys/osal_cc.h"
#include "sys/osal_sys.h"

#include "osal.h"
#include "osal_rt.h"

/* Cypress HAL (ModusToolbox) */
#include "cyhal.h"
#include "cybsp.h"

/* ============================= Configuration ============================== */

/* 1 MHz time base => 1 tick = 1 us */
#ifndef OS_TIMER_FREQ_HZ
#define OS_TIMER_FREQ_HZ (1000000u)
#endif

#ifndef OS_TIMER_ISR_PRIORITY
#define OS_TIMER_ISR_PRIORITY 2
#endif

#define OS_FLAG_US_TIMER_FIRED (1u << 0)

#ifndef USECS_PER_SEC
#define USECS_PER_SEC 1000000u
#endif

/* pure busy wait threshold: below this, skip event signalling */
#ifndef OS_SHORT_BUSYWAIT_ONLY_THRESH_US
#define OS_SHORT_BUSYWAIT_ONLY_THRESH_US 40u
#endif

/* small offset applied at final trim (event path only). Set to 0 to disable. */
#ifndef OS_TRIM_OFFSET_US
#define OS_TRIM_OFFSET_US 3u
#endif

/* compensation when going from tick sleep to us tail. Set to 0 to disable. */
#ifndef OS_TAIL_OFFSET_US
#define OS_TAIL_OFFSET_US 6u
#endif

/* ============================= Module state =============================== */

/* 16-bit up-counting timer used for both timekeeping and compare */
static cyhal_timer_t s_us_tmr;
static volatile uint32_t s_time_hi = 0; /* increments by 0x00010000 on overflow
                                         */
static os_event_t * s_evt        = NULL;
static os_mutex_t * s_sleep_lock = NULL;

/* ============================== Utilities ================================= */

static inline uint32_t os_u32_add (uint32_t a, uint32_t b)
{
   return (uint32_t)(a + b);
}
static inline uint32_t os_u32_diff (uint32_t start, uint32_t end)
{
   return (uint32_t)(end - start);
}

/* fast 16-bit read (single HW access) */
static inline uint16_t os_timer_read16 (void)
{
   return (uint16_t)Cy_TCPWM_Counter_GetCounter (
      s_us_tmr.tcpwm.base,
      s_us_tmr.tcpwm.resource.channel_num);
}

/* 16-bit busy-wait until deadline16 (< 65.536 ms) */
static inline void os_busywait_until16 (uint16_t deadline16)
{
   while ((int16_t)(deadline16 - os_timer_read16()) > 0)
   { /* busy wait */
   }
}

static inline void os_evt_clear_if_set (void)
{
   if (s_evt)
      os_event_clr (s_evt, OS_FLAG_US_TIMER_FIRED);
}

static inline void os_evt_wait_and_clear (void)
{
   uint32_t flags = 0;
   (void)os_event_wait (s_evt, OS_FLAG_US_TIMER_FIRED, &flags, OS_WAIT_FOREVER);
   if (flags & OS_FLAG_US_TIMER_FIRED)
      os_event_clr (s_evt, OS_FLAG_US_TIMER_FIRED);
}

/* ================================ ISR ===================================== */

static void os_us_isr (void * arg, cyhal_timer_event_t event)
{
   (void)arg;

   if (event & CYHAL_TIMER_IRQ_TERMINAL_COUNT)
   {
      /* advance upper 16 bits once per 65.536 ms overflow */
      s_time_hi += 0x00010000u;
   }

   if (event & CYHAL_TIMER_IRQ_CAPTURE_COMPARE)
   {
      os_event_set_from_isr (s_evt, OS_FLAG_US_TIMER_FIRED);
   }
}

/* ============================= Timebase API =============================== */

/* fast read (< 65.536 ms) */
uint32_t os_get_current_time_us_fast (void)
{
   uint32_t hi = s_time_hi & 0xFFFF0000u;
   uint32_t lo = cyhal_timer_read (&s_us_tmr) & 0xFFFFu;
   return hi | lo;
}

/* overflow-safe read  */
uint32_t os_get_current_time_us (void)
{
   uint32_t hi1, hi2, lo;
   do
   {
      hi1 = s_time_hi;
      lo  = cyhal_timer_read (&s_us_tmr) & 0xFFFFu;
      hi2 = s_time_hi;
      if (hi1 != hi2)
      {
         lo = cyhal_timer_read (&s_us_tmr) & 0xFFFFu;
      }
   } while (hi1 != hi2);
   return (hi1 & 0xFFFF0000u) | lo;
}

/* ============================== Internals ================================= */

/* set updated compare value */
static inline cy_rslt_t os_sleep_set_compare_abs (uint16_t abs16)
{
   Cy_TCPWM_Counter_SetCompare0Val (
      s_us_tmr.tcpwm.base,
      s_us_tmr.tcpwm.resource.channel_num,
      abs16);
   return CY_RSLT_SUCCESS;
}

/* =============================== Public API =============================== */

void os_time_initialize (void)
{
   uint32_t crit = os_enter_critical();
   cy_rslt_t rslt;
   (void)rslt;

   s_evt        = os_event_create();
   s_sleep_lock = os_mutex_create();
   if (!s_evt || !s_sleep_lock)
   {
      if (s_evt)
         os_event_destroy (s_evt);
      if (s_sleep_lock)
         os_mutex_destroy (s_sleep_lock);
      os_exit_critical (crit);
      return;
   }

   rslt = cyhal_timer_init (&s_us_tmr, NC, NULL);
   CC_ASSERT (rslt == CY_RSLT_SUCCESS);

   rslt = cyhal_timer_set_frequency (&s_us_tmr, OS_TIMER_FREQ_HZ);
   CC_ASSERT (rslt == CY_RSLT_SUCCESS);

   const cyhal_timer_cfg_t cfg = {
      .compare_value = 0,
      .period        = 0xFFFFu, /* 16-bit free-running */
      .direction     = CYHAL_TIMER_DIR_UP,
      .is_compare    = true, /* enable compare match */
      .is_continuous = true,
      .value         = 0};
   rslt = cyhal_timer_configure (&s_us_tmr, &cfg);
   CC_ASSERT (rslt == CY_RSLT_SUCCESS);

   cyhal_timer_register_callback (&s_us_tmr, os_us_isr, NULL);
   cyhal_timer_enable_event (
      &s_us_tmr,
      CYHAL_TIMER_IRQ_TERMINAL_COUNT,
      OS_TIMER_ISR_PRIORITY,
      true);
   cyhal_timer_enable_event (
      &s_us_tmr,
      CYHAL_TIMER_IRQ_CAPTURE_COMPARE,
      OS_TIMER_ISR_PRIORITY,
      true);

   rslt = cyhal_timer_start (&s_us_tmr);
   CC_ASSERT (rslt == CY_RSLT_SUCCESS);

   os_exit_critical (crit);
}

/* short, high-precision sleep (us), using the 16-bit timebase only */
cy_rslt_t os_sleep_us_short (uint32_t usec)
{
   if (usec == 0u)
      return CY_RSLT_SUCCESS;

   const uint16_t entry16 = os_timer_read16();

   /* very short waits, pure busy wait */
   if (usec <= OS_SHORT_BUSYWAIT_ONLY_THRESH_US)
   {
      uint16_t deadline16 = (uint16_t)(entry16 + (uint16_t)usec);
      os_busywait_until16 (deadline16);
      return CY_RSLT_SUCCESS;
   }

   os_mutex_lock (s_sleep_lock);

   /* program early compare so we can finish using event signalling
      small early margin (~30–40 us)
    */
   const uint32_t EARLY_MARGIN_US = 36u;
   uint32_t arm_us = (usec > EARLY_MARGIN_US) ? (usec - EARLY_MARGIN_US) : 1u;

   os_evt_clear_if_set();

   const uint16_t now16 = os_timer_read16();
   uint16_t tgt16       = (uint16_t)(now16 + (uint16_t)arm_us);
   if (tgt16 == now16)
      tgt16 = (uint16_t)(now16 + 1u);

   (void)os_sleep_set_compare_abs (tgt16);

   /* sleep until compare irq triggers */
   os_evt_wait_and_clear();

   /* final trim to entry16 + usec (with a tiny offset to compensate exec time)
    */
   uint16_t final16 = (uint16_t)(entry16 + (uint16_t)usec);
#if OS_TRIM_OFFSET_US > 0
   if (OS_TRIM_OFFSET_US < usec)
      final16 = (uint16_t)(final16 - (uint16_t)OS_TRIM_OFFSET_US);
#endif
   os_busywait_until16 (final16);

   os_mutex_unlock (s_sleep_lock);
   return CY_RSLT_SUCCESS;
}

/* override default os_usleep with high precision wait */
void os_usleep (uint32_t usec)
{
   (void)os_sleep_us_short (usec);
}

/* use fast time read for shorter than 65 ms (16 bit timer) */
static inline uint32_t os_read_time (uint32_t window_us)
{
   return (window_us <= 65000u) ? os_get_current_time_us_fast()
                                : os_get_current_time_us();
}

void os_sleep_us (uint32_t usec)
{
   if (usec == 0u)
      return;

   const uint32_t deadline = os_u32_add (os_read_time (usec), usec);

   for (;;)
   {
      uint32_t now    = os_read_time (usec);
      uint32_t remain = os_u32_diff (now, deadline);
      if ((int32_t)remain <= 0)
         return;

      /* sleep most of it with the tick sleep, leave ~1 tick margin */
      os_tick_t ticks = os_tick_from_us (remain);
      if (ticks > 1)
      {
         os_tick_sleep (ticks - 1);
         continue;
      }

      /* sleep remainder using high precision wait */
#if OS_TAIL_OFFSET_US > 0
      if (remain > OS_TAIL_OFFSET_US)
         remain -= OS_TAIL_OFFSET_US;
#endif
      (void)os_sleep_us_short (remain);
   }
}

/* ============================ Time helpers ================================ */

int gettimeofday (os_time_t * tp, void * tzp)
{
   (void)tzp;
   CC_ASSERT (tp != NULL);
   uint32_t t  = os_get_current_time_us();
   tp->tv_sec  = t / USECS_PER_SEC;
   tp->tv_nsec = (t % USECS_PER_SEC) * 1000u;
   return 0;
}

os_time_t os_current_time (void)
{
   os_time_t r;
   gettimeofday (&r, 0);
   return r;
}

void osal_get_monotonic_time (os_time_t * tv)
{
   uint64_t usec = (uint64_t)os_get_current_time_us();
   osal_timespec_from_usec (usec, tv);
}

void osal_time_diff (os_time_t * start, os_time_t * end, os_time_t * diff)
{
   osal_timespecsub (end, start, diff);
}

void osal_timer_start (osal_timert * self, uint32 timeout_usec)
{
   os_time_t start, to;
   osal_get_monotonic_time (&start);
   osal_timespec_from_usec (timeout_usec, &to);
   osal_timespecadd (&start, &to, &self->stop_time);
}

boolean osal_timer_is_expired (osal_timert * self)
{
   os_time_t now;
   osal_get_monotonic_time (&now);
   return osal_timespeccmp (&now, &self->stop_time, >=) ? TRUE : FALSE;
}

/* wait until absolute deadline (us), return signed jitter */
int32_t os_wait_until_deadline_us (uint32_t abs_deadline_us)
{
   uint32_t now   = os_get_current_time_us();
   int32_t remain = (int32_t)(abs_deadline_us - now);
   if (remain > 0)
      os_sleep_us ((uint32_t)remain);
   return (int32_t)(os_get_current_time_us() - abs_deadline_us);
}

/* initialize next cycle deadline */
void os_cycle_init (uint32_t period_us, uint32_t * next_deadline_us)
{
   if (next_deadline_us)
      *next_deadline_us = os_u32_add (os_get_current_time_us(), period_us);
}
