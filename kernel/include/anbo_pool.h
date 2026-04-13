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
 * @file  anbo_pool.h
 * @brief Anbo Kernel — Fixed-size Block Memory Pool + Async Event Queue
 *
 * Design highlights:
 *   - O(1) alloc / free using a free-list of equally-sized blocks.
 *   - Designed for C-OOP event inheritance: derived event structs embed
 *     Anbo_PoolEvent as their first member (base class).
 *   - Async event pointer queue (ring buffer of Event* pointers) decouples
 *     ISR publishers from main-loop consumers — zero copy.
 *   - Reference counting: the framework decrements ref_count after each
 *     subscriber processes the event; when it reaches 0 the block is
 *     automatically returned to the pool.
 *   - Fully static memory, ISR-safe with critical sections.
 *
 * Compiled only when ANBO_CONF_POOL == 1.
 *
 * Constraints: C99 / fully static memory / no malloc / no sprintf
 */

#ifndef ANBO_POOL_H
#define ANBO_POOL_H

#include "anbo_config.h"

#if ANBO_CONF_POOL

#include <stdint.h>
#include <stddef.h>
#include "anbo_ebus.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================== */
/*  Pooled Event Base Class                                            */
/* ================================================================== */

/**
 * @struct Anbo_PoolEvent
 * @brief  Base class for all pool-allocated events.
 *
 * Application-defined derived events MUST place this as the first member:
 * @code
 *   typedef struct {
 *       Anbo_PoolEvent super;   // must be first
 *       float temperature;
 *       float humidity;
 *   } TempEvent;
 * @endcode
 */
typedef struct {
    uint16_t sig;           /**< Signal identifier (same semantics as Anbo_Event.sig) */
    uint8_t  ref_count;     /**< Reference count (managed by framework) */
    uint8_t  reserved;      /**< Padding / future use */
} Anbo_PoolEvent;

/* ================================================================== */
/*  Memory Pool API                                                    */
/* ================================================================== */

/**
 * @brief Initialize the fixed-size block pool.
 *        Call once at system startup (before any Alloc).
 */
void Anbo_Pool_Init(void);

/**
 * @brief Allocate one block from the pool.
 * @return Pointer to usable memory (cast to your derived event type),
 *         or NULL if the pool is exhausted.
 *
 * ISR-safe (uses critical section internally).
 * The returned block is uninitialised; caller must fill all fields.
 */
void *Anbo_Pool_Alloc(void);

/**
 * @brief Return a block to the pool.
 * @param ptr  Pointer previously obtained from Anbo_Pool_Alloc().
 *             NULL is silently ignored.
 *
 * ISR-safe.
 */
void Anbo_Pool_Free(void *ptr);

/**
 * @brief Number of blocks currently available.
 */
uint32_t Anbo_Pool_FreeCount(void);

/* ================================================================== */
/*  Reference-count Retain / Release                                   */
/* ================================================================== */

/**
 * @brief Increment the reference count of a pool-allocated event.
 *
 * Call this inside a subscriber callback if you need to keep the event
 * alive beyond the current Dispatch cycle (deferred / async consumption).
 * You MUST call Anbo_Pool_Release() once you are done with the event.
 *
 * ISR-safe.
 */
void Anbo_Pool_Retain(Anbo_PoolEvent *evt);

/**
 * @brief Decrement the reference count; free the block when it reaches 0.
 *
 * Subscribers that called Anbo_Pool_Retain() MUST call this when they
 * no longer need the event.  If no Retain was called, the framework
 * handles Release automatically inside Anbo_Pool_Dispatch().
 *
 * ISR-safe.
 */
void Anbo_Pool_Release(Anbo_PoolEvent *evt);

/* ================================================================== */
/*  Async Event Pointer Queue API                                      */
/* ================================================================== */

/**
 * @brief Initialize the async event pointer queue.
 *        Call once at system startup.
 */
void Anbo_EvtQ_Init(void);

/**
 * @brief Post an event pointer into the async queue.
 * @param evt  Pointer to a pool-allocated event (Anbo_PoolEvent* or derived).
 * @retval 0   Success.
 * @retval -1  Queue is full (event is NOT freed — caller decides).
 *
 * Typically called from ISR or publisher context.  ISR-safe.
 */
int Anbo_EvtQ_Post(Anbo_PoolEvent *evt);

/**
 * @brief Retrieve the next event pointer from the queue.
 * @param[out] evt  Receives the event pointer.
 * @retval 0   Success; *evt is valid.
 * @retval -1  Queue is empty.
 *
 * Called from the main loop.
 */
int Anbo_EvtQ_Get(Anbo_PoolEvent **evt);

/**
 * @brief Check whether the async event queue is empty.
 * @retval 1  Empty.
 * @retval 0  Not empty.
 */
int Anbo_EvtQ_IsEmpty(void);

/* ================================================================== */
/*  Framework Dispatch (convenience)                                   */
/* ================================================================== */

/**
 * @brief Dispatch one pooled event through the EBus, with automatic
 *        reference-count management and pool reclamation.
 *
 * Workflow (Retain / Release model):
 *   1. Set evt->ref_count = 1 (framework reference).
 *   2. Publish to EBus synchronously; all subscriber callbacks run.
 *   3. Any subscriber that needs deferred access calls Anbo_Pool_Retain()
 *      inside its callback, incrementing ref_count.
 *   4. After Publish returns, the framework calls Anbo_Pool_Release().
 *      - If no subscriber retained: ref_count drops to 0 → auto-free
 *        (identical to the previous synchronous behaviour).
 *      - If a subscriber retained: block survives until that subscriber
 *        calls Anbo_Pool_Release().
 *
 * @param evt  Pool-allocated event pointer (consumed by this call
 *             unless a subscriber retains it).
 */
void Anbo_Pool_Dispatch(Anbo_PoolEvent *evt);

#ifdef __cplusplus
}
#endif

#endif /* ANBO_CONF_POOL */

#endif /* ANBO_POOL_H */
