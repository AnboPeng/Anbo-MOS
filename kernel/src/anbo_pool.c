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
 * @file  anbo_pool.c
 * @brief Anbo Kernel — Fixed-size block pool + async event queue + dispatch
 *
 * Block pool:
 *   O(1) alloc / free via a singly-linked free-list embedded in the
 *   unused block payload (union trick).
 *
 * Async event queue:
 *   Ring buffer of Anbo_PoolEvent* pointers, power-of-two depth,
 *   index masking instead of modulo.
 *
 * Dispatch:
 *   Publishes pooled events through the synchronous Anbo_EBus while
 *   managing ref_count and automatic pool reclamation.
 *
 * Compiled only when ANBO_CONF_POOL == 1.
 *
 * Constraints: C99 / fully static memory / no malloc / no sprintf
 */

#include "anbo_pool.h"

#if ANBO_CONF_POOL

#include "anbo_arch.h"
#include "anbo_ebus.h"
#include "anbo_list.h"
#include <stddef.h>
#include <string.h>

/* ================================================================== */
/*  Compile-time checks (C99 compatible)                               */
/* ================================================================== */

/* Negative-size array trick for C99 static assertions */
#define ANBO_STATIC_ASSERT(cond, msg) \
    typedef char anbo_sa_##msg[(cond) ? 1 : -1]

/* Block size must be large enough to hold a free-list pointer */
ANBO_STATIC_ASSERT(ANBO_CONF_POOL_BLOCK_SIZE >= sizeof(void *),
                   block_size_too_small);

/* Queue depth must be a power of two */
ANBO_STATIC_ASSERT((ANBO_CONF_EVTQ_DEPTH & (ANBO_CONF_EVTQ_DEPTH - 1)) == 0,
                   evtq_depth_not_power_of_two);

/* ================================================================== */
/*  1. Fixed-size Block Pool                                           */
/* ================================================================== */

/**
 * Each block is a union: when free it holds a next-pointer,
 * when allocated it holds the user payload.
 */
typedef union PoolBlock {
    union PoolBlock *next;                              /**< Free-list link */
    uint8_t          payload[ANBO_CONF_POOL_BLOCK_SIZE]; /**< User data */
} PoolBlock;

static PoolBlock  ANBO_POOL_SECTION s_blocks[ANBO_CONF_POOL_BLOCK_COUNT];
static PoolBlock *s_free_head;
static uint32_t   s_free_count;

/* ------------------------------------------------------------------ */

void Anbo_Pool_Init(void)
{
    uint32_t i;

    for (i = 0u; i < (uint32_t)(ANBO_CONF_POOL_BLOCK_COUNT - 1); i++) {
        s_blocks[i].next = &s_blocks[i + 1u];
    }
    s_blocks[ANBO_CONF_POOL_BLOCK_COUNT - 1].next = NULL;

    s_free_head  = &s_blocks[0];
    s_free_count = (uint32_t)ANBO_CONF_POOL_BLOCK_COUNT;
}

/* ------------------------------------------------------------------ */

void *Anbo_Pool_Alloc(void)
{
    PoolBlock *blk;

    Anbo_Arch_Critical_Enter();

    blk = s_free_head;
    if (blk != NULL) {
        s_free_head = blk->next;
        s_free_count--;
    }

    Anbo_Arch_Critical_Exit();
    return (void *)blk;
}

/* ------------------------------------------------------------------ */

void Anbo_Pool_Free(void *ptr)
{
    PoolBlock *blk;

    if (ptr == NULL) {
        return;
    }

    blk = (PoolBlock *)ptr;

    Anbo_Arch_Critical_Enter();

    blk->next   = s_free_head;
    s_free_head  = blk;
    s_free_count++;

    Anbo_Arch_Critical_Exit();
}

/* ------------------------------------------------------------------ */

uint32_t Anbo_Pool_FreeCount(void)
{
    return s_free_count;
}

/* ================================================================== */
/*  2. Async Event Pointer Queue                                       */
/* ================================================================== */

#define EVTQ_MASK   ((uint32_t)(ANBO_CONF_EVTQ_DEPTH) - 1u)

static Anbo_PoolEvent * ANBO_POOL_SECTION s_evtq[ANBO_CONF_EVTQ_DEPTH];
static volatile uint32_t s_evtq_head;   /**< Write index (producer) */
static volatile uint32_t s_evtq_tail;   /**< Read index  (consumer) */

/* ------------------------------------------------------------------ */

void Anbo_EvtQ_Init(void)
{
    s_evtq_head = 0u;
    s_evtq_tail = 0u;
}

/* ------------------------------------------------------------------ */

int Anbo_EvtQ_Post(Anbo_PoolEvent *evt)
{
    uint32_t next;

    if (evt == NULL) {
        return -1;
    }

    Anbo_Arch_Critical_Enter();

    next = (s_evtq_head + 1u) & EVTQ_MASK;
    if (next == s_evtq_tail) {
        /* Queue full */
        Anbo_Arch_Critical_Exit();
        return -1;
    }

    s_evtq[s_evtq_head] = evt;
    s_evtq_head = next;

    Anbo_Arch_Critical_Exit();
    return 0;
}

/* ------------------------------------------------------------------ */

int Anbo_EvtQ_Get(Anbo_PoolEvent **evt)
{
    if (evt == NULL) {
        return -1;
    }

    Anbo_Arch_Critical_Enter();

    if (s_evtq_head == s_evtq_tail) {
        Anbo_Arch_Critical_Exit();
        return -1;   /* Empty */
    }

    *evt = s_evtq[s_evtq_tail];
    s_evtq_tail = (s_evtq_tail + 1u) & EVTQ_MASK;

    Anbo_Arch_Critical_Exit();
    return 0;
}

/* ------------------------------------------------------------------ */

int Anbo_EvtQ_IsEmpty(void)
{
    return (s_evtq_head == s_evtq_tail) ? 1 : 0;
}

/* ================================================================== */
/*  3. Retain / Release                                                */
/* ================================================================== */

void Anbo_Pool_Retain(Anbo_PoolEvent *evt)
{
    if (evt == NULL) {
        return;
    }

    Anbo_Arch_Critical_Enter();
    evt->ref_count++;
    Anbo_Arch_Critical_Exit();
}

/* ------------------------------------------------------------------ */

void Anbo_Pool_Release(Anbo_PoolEvent *evt)
{
    uint8_t rc;

    if (evt == NULL) {
        return;
    }

    Anbo_Arch_Critical_Enter();
    if (evt->ref_count > 0u) {
        evt->ref_count--;
    }
    rc = evt->ref_count;
    Anbo_Arch_Critical_Exit();

    if (rc == 0u) {
        Anbo_Pool_Free(evt);
    }
}

/* ================================================================== */
/*  4. Framework Dispatch with auto-reclaim                            */
/* ================================================================== */

/*
 * Dispatch strategy (Retain / Release model):
 *   1. Set ref_count = 1 (framework owns one reference).
 *   2. Build an Anbo_Event with param = Anbo_PoolEvent* (upcast).
 *   3. Anbo_EBus_Publish() broadcasts synchronously — all subscriber
 *      callbacks run and return before Publish returns.
 *   4. Subscribers that need deferred access call Anbo_Pool_Retain()
 *      inside their callback (ref_count becomes 2, 3, …).
 *   5. Framework calls Anbo_Pool_Release() — ref_count decrements.
 *      If no subscriber retained, it reaches 0 → auto-free (backward
 *      compatible with the original synchronous model).
 *
 * Subscribers downcast evt->param to their derived type:
 *   TempEvent *te = (TempEvent *)evt->param;
 */

void Anbo_Pool_Dispatch(Anbo_PoolEvent *evt)
{
    Anbo_Event ebus_evt;

    if (evt == NULL) {
        return;
    }

    /* Framework holds one reference */
    evt->ref_count = 1u;

    /*
     * Build a lightweight Anbo_Event that carries the pool event pointer
     * as its param.  Subscribers receive (sig, param = Anbo_PoolEvent*),
     * and downcast param to their derived type.
     */
    ebus_evt.sig   = evt->sig;
    ebus_evt.param = (void *)evt;

    /* Synchronous broadcast — all subscribers run before Publish returns */
    Anbo_EBus_Publish(&ebus_evt);

    /*
     * Release the framework's reference.
     * If no subscriber called Retain → ref_count drops to 0 → auto-free.
     * If a subscriber called Retain → block survives until they Release.
     */
    Anbo_Pool_Release(evt);
}

#endif /* ANBO_CONF_POOL */

/* ISO C requires at least one external declaration per translation unit */
typedef int anbo_pool_empty_tu;
