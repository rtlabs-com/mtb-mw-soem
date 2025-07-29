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

#ifndef OSAL_RT_H
#define OSAL_RT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "sys/osal_cc.h"

/* align with standard timespec */
typedef struct
{
   uint32_t tv_sec;  /* Seconds */
   uint32_t tv_nsec; /* Nanoseconds [0, 999'999'999] */
} os_time_t;

typedef struct
{
   os_time_t stop_time;
} os_timert_t;

#define osal_mutext os_mutex_t
#define osal_timert os_timert_t
#define ec_timet    os_time_t

os_time_t os_current_time (void);

/**
 * @brief Returns monotonic time from some unspecified moment in the
 * past.
 *
 * This time must be strictly increasing. It is used for time
 * intervals measurement.
 *
 * @param ts Pointer to an ec_timet structure where the time will be
 * stored.
 */
void osal_get_monotonic_time (ec_timet * ts);

/**
 * @brief Returns the current time.
 *
 * This time is used to set the initial EtherCAT network DC time and
 * for logging purposes.
 *
 * @return ec_timet containing the current time.
 */

#define osal_current_time() os_current_time()

/**
 * @brief Calculates the difference between two timestamps.
 *
 * @param start Pointer to the start timestamp.
 * @param end Pointer to the end timestamp.
 * @param diff Pointer to an ec_timet structure where the difference
 * will be stored.
 */
void osal_time_diff (ec_timet * start, ec_timet * end, ec_timet * diff);

/**
 * @brief Starts the timer with a specified timeout.
 *
 * @param self Pointer to the timer object.
 * @param timeout_usec Timeout in microseconds.
 */
void osal_timer_start (osal_timert * self, uint32 timeout_usec);

/**
 * @brief Checks if the timer has expired.
 *
 * @param self Pointer to the timer object.
 * @return True if the timer is expired, false otherwise.
 */
boolean osal_timer_is_expired (osal_timert * self);

/**
 * @brief Sleeps for a specified duration in microseconds.
 *
 * @param usec Duration in microseconds.
 * @return 0 on success, -1 on failure.
 */

/**
 * @brief Initialize cyclic wait.
 *
 * @param period_us Cycle in microseconds
 * @param next_dealine_us Next absolute expiration time
 * @return void
 */
void os_cycle_init (uint32_t period_us, uint32_t * next_deadline_us);

/**
 * @brief Wait until absolute deadline
 *
 * @param period_us Cycle in microseconds
 * @param next_deadline_us Next absolute expiration time
 * @return void
 */

/* wait until absolute deadline; return signed jitter (late/early) */
int32_t os_wait_until_deadline_us (uint32_t abs_deadline_us);

/**
 * @brief Sleeps until the specified monotonic time.
 *
 * @param ts Pointer to an ec_timet structure representing the
 * absolute time to sleep until.
 * @return 0 on success, -1 on failure.
 */
int osal_monotonic_sleep (os_time_t * ts);

/**
 * @brief Initialize high precision timekeeping
 *
 * @return void
 */
void os_time_initialize (void);

/* Ethercat library wrappers (to be aligned with rte variant) */
#define osal_usleep(us) os_usleep (us)

#define osal_malloc(size) os_malloc (size)

#define osal_free(ptr) os_free (ptr)

#define osal_mutex_create() os_mutex_create()

#define osal_mutex_destroy(mutex) os_mutex_destroy (mutex)

#define osal_mutex_lock(mutex) os_mutex_lock (mutex)

#define osal_mutex_unlock(mutex) os_mutex_unlock (mutex)

#ifndef osal_timespec_from_usec
#define osal_timespec_from_usec(usec, result)                                  \
   do                                                                          \
   {                                                                           \
      (result)->tv_sec  = usec / 1000000;                                      \
      (result)->tv_nsec = (usec % 1000000) * 1000;                             \
   } while (0)
#endif

#ifndef osal_timespeccmp
#define osal_timespeccmp(a, b, CMP)                                            \
   (((a)->tv_sec == (b)->tv_sec) ? ((a)->tv_nsec CMP (b)->tv_nsec)             \
                                 : ((a)->tv_sec CMP (b)->tv_sec))
#endif

#ifndef osal_timespecadd
#define osal_timespecadd(a, b, result)                                         \
   do                                                                          \
   {                                                                           \
      (result)->tv_sec  = (a)->tv_sec + (b)->tv_sec;                           \
      (result)->tv_nsec = (a)->tv_nsec + (b)->tv_nsec;                         \
      if ((result)->tv_nsec >= 1000000000)                                     \
      {                                                                        \
         ++(result)->tv_sec;                                                   \
         (result)->tv_nsec -= 1000000000;                                      \
      }                                                                        \
   } while (0)
#endif

#ifndef osal_timespecsub
#define osal_timespecsub(a, b, result)                                         \
   do                                                                          \
   {                                                                           \
      (result)->tv_sec  = (a)->tv_sec - (b)->tv_sec;                           \
      (result)->tv_nsec = (a)->tv_nsec - (b)->tv_nsec;                         \
      if ((result)->tv_nsec < 0)                                               \
      {                                                                        \
         --(result)->tv_sec;                                                   \
         (result)->tv_nsec += 1000000000;                                      \
      }                                                                        \
   } while (0)
#endif

#ifdef __cplusplus
}
#endif

#endif /* OSAL_RT_H */
