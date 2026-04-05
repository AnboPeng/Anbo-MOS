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
 * @file  anbo_wdt.h
 * @brief Anbo Kernel — Software Watchdog Monitor
 *
 * Design highlights:
 *   - Registration-based: each critical FSM / task registers with the monitor
 *     and receives a slot.
 *   - Periodic check-in: tasks call Anbo_WDT_Checkin() during normal execution.
 *   - Kernel main loop calls Anbo_WDT_Monitor():
 *       - Iterates all registered slots, checking whether each has checked in
 *         within its timeout window.
 *       - If all are normal -> calls Anbo_Arch_WDT_Feed() to feed the physical watchdog.
 *       - If any has timed out -> does not feed; the physical watchdog eventually resets
 *         the system.
 *   - Fully static: slot pool is allocated at compile time, sized by
 *     ANBO_CONF_WDT_MAX_SLOTS.
 *   - Non-blocking: no busy-wait loops.
 *
 * Compiled only when ANBO_CONF_WDT == 1.
 *
 * Constraints: C99 / fully static memory / no malloc / no sprintf
 */

#ifndef ANBO_WDT_H
#define ANBO_WDT_H

#include "anbo_config.h"

#if ANBO_CONF_WDT

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================== */
/*  Configuration                                                      */
/* ================================================================== */

/**
 * @def   ANBO_CONF_WDT_MAX_SLOTS
 * @brief Maximum number of monitored tasks (static pool size).
 */
#ifndef ANBO_CONF_WDT_MAX_SLOTS
#define ANBO_CONF_WDT_MAX_SLOTS     8
#endif

/* ================================================================== */
/*  Data Types                                                         */
/* ================================================================== */

/** Slot handle returned by Register; -1 means invalid */
typedef int8_t Anbo_WDT_Slot;

#define ANBO_WDT_SLOT_INVALID   ((Anbo_WDT_Slot)-1)

/* ================================================================== */
/*  API                                                                */
/* ================================================================== */

/**
 * @brief Initialize the software watchdog monitor (clear the slot pool).
 *        Call once at system startup.
 */
void Anbo_WDT_Init(void);

/**
 * @brief Register a monitored task.
 * @param name       Task name (for debugging, may be NULL).
 * @param timeout_ms Timeout threshold (milliseconds). If the interval between
 *                   two Checkin calls exceeds this value, the task is considered abnormal.
 * @return Slot handle (>= 0); returns ANBO_WDT_SLOT_INVALID if the pool is full.
 */
Anbo_WDT_Slot Anbo_WDT_Register(const char *name, uint32_t timeout_ms);

/**
 * @brief Task "check-in" — indicates the task is still running normally.
 * @param slot  Handle returned by Register.
 *
 * Refreshes the slot's most recent activity timestamp.
 * May be called from ISR or main loop.
 */
void Anbo_WDT_Checkin(Anbo_WDT_Slot slot);

/**
 * @brief Suspend monitoring of a slot (e.g., task entering extended sleep).
 * @param slot  Handle.
 */
void Anbo_WDT_Suspend(Anbo_WDT_Slot slot);

/**
 * @brief Resume monitoring of a slot (also refreshes the timestamp).
 * @param slot  Handle.
 */
void Anbo_WDT_Resume(Anbo_WDT_Slot slot);

/**
 * @brief Kernel main-loop monitoring check (non-blocking).
 * @param now  Current system tick (ms), typically pass Anbo_Arch_GetTick().
 * @retval 1   All tasks normal; Anbo_Arch_WDT_Feed() has been called.
 * @retval 0   A timed-out task exists; watchdog was NOT fed.
 *
 * This function does not block; each call is O(N) (N = registered slot count).
 */
int Anbo_WDT_Monitor(uint32_t now);

/**
 * @brief Get the name of the first timed-out slot (for debug diagnostics).
 * @param now  Current system tick (ms).
 * @return Name of the timed-out task; NULL if all are normal.
 */
const char *Anbo_WDT_FirstTimeout(uint32_t now);

#ifdef __cplusplus
}
#endif

#endif /* ANBO_CONF_WDT */

#endif /* ANBO_WDT_H */
