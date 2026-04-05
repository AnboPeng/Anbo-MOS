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
 * @file  anbo_config.h
 * @brief Anbo Kernel — Global compile-time configuration center
 *
 * All feature switches use the ANBO_CONF_ prefix. Users may override
 * them on the compiler command line or within this file.
 * Constraints: C99 / fully static memory / no malloc / no sprintf
 */

#ifndef ANBO_CONFIG_H
#define ANBO_CONFIG_H

/* ------------------------------------------------------------------ */
/*  User-overridable defaults (redefine via -D at compile time)        */
/* ------------------------------------------------------------------ */

/**
 * @def   ANBO_CONF_IDLE_SLEEP
 * @brief Low-power idle switch. 1 = call Anbo_Arch_Idle() when idle; 0 = busy-wait.
 */
#ifndef ANBO_CONF_IDLE_SLEEP
#define ANBO_CONF_IDLE_SLEEP        1
#endif

/**
 * @def   ANBO_CONF_WDT
 * @brief Watchdog monitor switch. 1 = kernel periodically calls Anbo_Arch_WDT_Feed(); 0 = off.
 */
#ifndef ANBO_CONF_WDT
#define ANBO_CONF_WDT               1
#endif

/* ------------------------------------------------------------------ */
/*  Kernel internal dimension limits (adjust as needed)                */
/* ------------------------------------------------------------------ */

/**
 * @def   ANBO_CONF_MAX_TIMERS
 * @brief Static soft-timer pool capacity.
 */
#ifndef ANBO_CONF_MAX_TIMERS
#define ANBO_CONF_MAX_TIMERS        16
#endif

/**
 * @def   ANBO_CONF_MAX_TASKS
 * @brief Static task (coroutine / cooperative body) pool capacity.
 */
#ifndef ANBO_CONF_MAX_TASKS
#define ANBO_CONF_MAX_TASKS         16
#endif

/**
 * @def   ANBO_CONF_TICK_HZ
 * @brief System heartbeat frequency (Hz), determines minimum timing resolution.
 */
#ifndef ANBO_CONF_TICK_HZ
#define ANBO_CONF_TICK_HZ           1000
#endif

/* ------------------------------------------------------------------ */
/*  Log buffer sizing (convenience alias)                              */
/* ------------------------------------------------------------------ */

/**
 * @def   ANBO_CONF_LOG_BUF_SIZE
 * @brief Log ring buffer capacity in bytes.
 *
 * Derived from ANBO_CONF_LOG_RB_ORDER (capacity = 1 << order).
 * Users may define either macro; LOG_BUF_SIZE takes precedence.
 */
#ifndef ANBO_CONF_LOG_BUF_SIZE
#  ifndef ANBO_CONF_LOG_RB_ORDER
#    define ANBO_CONF_LOG_RB_ORDER   10          /* 1024 bytes */
#  endif
#  define ANBO_CONF_LOG_BUF_SIZE    (1u << ANBO_CONF_LOG_RB_ORDER)
#endif

/* ------------------------------------------------------------------ */
/*  Kernel Trace (event & FSM state transition logging)                */
/* ------------------------------------------------------------------ */

/**
 * @def   ANBO_CONF_TRACE
 * @brief Kernel event/FSM trace switch.
 *        1 = trace hooks in EBus Publish and FSM Transfer output log lines.
 *        0 = all trace macros expand to nothing (zero cost).
 */
#ifndef ANBO_CONF_TRACE
#define ANBO_CONF_TRACE             1
#endif

/* ------------------------------------------------------------------ */
/*  Async Event Pool (C-OOP inheritance + zero-copy dispatch)          */
/* ------------------------------------------------------------------ */

/**
 * @def   ANBO_CONF_POOL
 * @brief Async event memory pool switch.
 *        1 = enable fixed-block pool + pointer-based async event queue.
 *        0 = disabled; the synchronous Anbo_EBus remains the only path.
 */
#ifndef ANBO_CONF_POOL
#define ANBO_CONF_POOL              1
#endif

#if ANBO_CONF_POOL

/**
 * @def   ANBO_CONF_POOL_BLOCK_SIZE
 * @brief Size (bytes) of each memory block in the event pool.
 *        Must be >= the largest derived event structure in the application.
 */
#ifndef ANBO_CONF_POOL_BLOCK_SIZE
#define ANBO_CONF_POOL_BLOCK_SIZE   64
#endif

/**
 * @def   ANBO_CONF_POOL_BLOCK_COUNT
 * @brief Number of blocks in the event pool.
 */
#ifndef ANBO_CONF_POOL_BLOCK_COUNT
#define ANBO_CONF_POOL_BLOCK_COUNT  20
#endif

/**
 * @def   ANBO_CONF_EVTQ_DEPTH
 * @brief Async event pointer queue depth (must be a power of two).
 */
#ifndef ANBO_CONF_EVTQ_DEPTH
#define ANBO_CONF_EVTQ_DEPTH       32
#endif

#endif /* ANBO_CONF_POOL */

/* ------------------------------------------------------------------ */
/*  Memory placement                                                   */
/* ------------------------------------------------------------------ */

/**
 * @def   ANBO_POOL_SECTION
 * @brief Section attribute for kernel object pool storage arrays.
 *
 * Override in the port layer to place pools in a dedicated memory region
 * (e.g. SRAM2 with ECC on STM32L4+).
 * Default: empty (pools go to regular .bss).
 */
#ifndef ANBO_POOL_SECTION
#define ANBO_POOL_SECTION
#endif

#endif /* ANBO_CONFIG_H */
