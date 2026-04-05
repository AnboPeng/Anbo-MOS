/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Anbo Peng
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
/**
 * @file  anbo_timer.h
 * @brief Anbo Kernel — Software Timer (SoftTimer)
 *
 * Design highlights:
 *   - Ascending intrusive linked list: sorted by absolute deadline.
 *   - Anbo_Timer_Update() only checks the list head — average O(1) / worst O(k)
 *     (k = number of timers expiring simultaneously).
 *   - Fires a callback upon expiry; supports One-shot and Periodic modes.
 *   - Fully static: timer nodes are declared by the user; kernel allocates no memory.
 *   - Hardware isolated: only calls Anbo_Arch_GetTick().
 *
 * Constraints: C99 / fully static memory / no malloc / no sprintf
 */

#ifndef ANBO_TIMER_H
#define ANBO_TIMER_H

#include <stdint.h>
#include "anbo_config.h"
#include "anbo_list.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================== */
/*  Data Types                                                         */
/* ================================================================== */

/** Timer mode */
typedef enum {
    ANBO_TIMER_ONESHOT  = 0,    /**< Single-shot: stops automatically after firing */
    ANBO_TIMER_PERIODIC = 1     /**< Periodic: fires repeatedly */
} Anbo_TimerMode;

/** Timer state */
typedef enum {
    ANBO_TIMER_STOPPED = 0,     /**< Not running */
    ANBO_TIMER_RUNNING = 1      /**< Inserted in the active list */
} Anbo_TimerState;

/**
 * @typedef Anbo_TimerCallback
 * @brief   Timer expiry callback.
 * @param   timer  Pointer to the timer that fired.
 */
typedef struct Anbo_Timer Anbo_Timer;
typedef void (*Anbo_TimerCallback)(Anbo_Timer *timer);

/**
 * @struct Anbo_Timer
 * @brief  Software timer descriptor (statically allocated by user).
 */
struct Anbo_Timer {
    Anbo_ListNode       node;       /**< Intrusive list node (linked into global active list) */
    uint32_t            deadline;   /**< Absolute expiry tick (ms) */
    uint32_t            period;     /**< Period (ms); One-shot retains the initial interval */
    Anbo_TimerMode      mode;       /**< One-shot / Periodic */
    Anbo_TimerState     state;      /**< Current state */
    Anbo_TimerCallback  callback;   /**< Expiry callback */
    void               *user_data;  /**< User-defined context */
};

/* ================================================================== */
/*  API                                                                */
/* ================================================================== */

/**
 * @brief Initialize the timer subsystem (clear the active list).
 *        Call once at system startup.
 */
void Anbo_Timer_Init(void);

/**
 * @brief Initialize a timer descriptor (does not start it).
 * @param tmr       Timer pointer.
 * @param mode      One-shot or Periodic.
 * @param period_ms Timeout / period duration (milliseconds).
 * @param cb        Expiry callback.
 * @param user_data User context (may be NULL).
 */
void Anbo_Timer_Create(Anbo_Timer         *tmr,
                        Anbo_TimerMode      mode,
                        uint32_t            period_ms,
                        Anbo_TimerCallback  cb,
                        void               *user_data);

/**
 * @brief Start (or restart) a timer.
 *
 * If the timer is already in the active list, it is first removed and then
 * re-inserted in ascending order.
 * deadline = Anbo_Arch_GetTick() + period.
 */
void Anbo_Timer_Start(Anbo_Timer *tmr);

/**
 * @brief Stop a timer (remove from the active list).
 */
void Anbo_Timer_Stop(Anbo_Timer *tmr);

/**
 * @brief Change the period (does not restart; takes effect on next Start or auto-reload).
 */
void Anbo_Timer_SetPeriod(Anbo_Timer *tmr, uint32_t period_ms);

/**
 * @brief Query whether a timer is currently running.
 */
int Anbo_Timer_IsRunning(const Anbo_Timer *tmr);

/**
 * @brief Heartbeat driver — call from the per-millisecond SysTick ISR (or main loop).
 * @param now  Current system tick (ms), typically pass Anbo_Arch_GetTick().
 *
 * Algorithm:
 *   Starting from the list head, check each timer with deadline <= now and fire its callback.
 *   Because the list is in ascending order, once deadline > now is encountered
 *   we can stop — O(1) average complexity.
 */
void Anbo_Timer_Update(uint32_t now);

/**
 * @brief Return the remaining milliseconds until the next timer expires.
 * @return Remaining ms; UINT32_MAX if no active timers exist.
 *
 * Useful for low-power: Anbo_Arch_Idle(Anbo_Timer_MsToNext()).
 */
uint32_t Anbo_Timer_MsToNext(uint32_t now);

#ifdef __cplusplus
}
#endif

#endif /* ANBO_TIMER_H */
