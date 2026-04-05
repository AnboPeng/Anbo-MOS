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
 * @file  anbo_ebus.c
 * @brief Anbo Kernel — Event bus implementation
 *
 * Internally uses hash buckets (sig % ANBO_CONF_EBUS_MAX_TOPICS) + intrusive linked list.
 * Subscribe O(1), Publish O(n/B) (n = total subscribers, B = bucket count).
 *
 * Constraints: C99 / fully static memory / no malloc / no sprintf
 */

#include "anbo_ebus.h"
#include "anbo_arch.h"
#include "anbo_log.h"

/* ================================================================== */
/*  Internal Data                                                      */
/* ================================================================== */

/** Hash bucket array — each bucket is an Anbo_ListNode doubly-linked list */
static Anbo_ListNode ANBO_POOL_SECTION s_buckets[ANBO_CONF_EBUS_MAX_TOPICS];

/* ================================================================== */
/*  Internal Helper                                                    */
/* ================================================================== */

/** sig -> bucket index (bitwise AND optimization possible, but modulo keeps it general) */
static inline uint16_t sig_to_idx(uint16_t sig)
{
    return (uint16_t)(sig % (uint16_t)ANBO_CONF_EBUS_MAX_TOPICS);
}

/* ================================================================== */
/*  API Implementation                                                 */
/* ================================================================== */

void Anbo_EBus_Init(void)
{
    uint16_t i;
    for (i = 0u; i < (uint16_t)ANBO_CONF_EBUS_MAX_TOPICS; i++) {
        Anbo_List_Init(&s_buckets[i]);
    }
}

/* ------------------------------------------------------------------ */

void Anbo_EBus_Subscribe(Anbo_Subscriber *sub,
                          uint16_t sig,
                          Anbo_EventHandler handler,
                          void *ctx)
{
    uint16_t idx;

    if ((sub == NULL) || (handler == NULL)) {
        return;
    }

    sub->sig     = sig;
    sub->handler = handler;
    sub->ctx     = ctx;
    Anbo_List_Init(&sub->node);

    idx = sig_to_idx(sig);

    Anbo_Arch_Critical_Enter();
    Anbo_List_InsertTail(&s_buckets[idx], &sub->node);
    Anbo_Arch_Critical_Exit();
}

/* ------------------------------------------------------------------ */

void Anbo_EBus_Unsubscribe(Anbo_Subscriber *sub)
{
    if (sub == NULL) {
        return;
    }

    Anbo_Arch_Critical_Enter();
    Anbo_List_Remove(&sub->node);
    Anbo_Arch_Critical_Exit();
}

/* ------------------------------------------------------------------ */

/**
 * @brief Internal dispatch — broadcast to all subscribers of evt->sig.
 *        Shared by Publish (with trace) and PublishSilent (without trace).
 */
static void ebus_dispatch(const Anbo_Event *evt)
{
    uint16_t idx = sig_to_idx(evt->sig);
    Anbo_ListNode *pos;
    Anbo_ListNode *tmp;
    Anbo_Subscriber *sub;

    Anbo_Arch_Critical_Enter();

    ANBO_LIST_FOR_EACH_SAFE(pos, tmp, &s_buckets[idx]) {
        sub = ANBO_LIST_ENTRY(pos, Anbo_Subscriber, node);
        if (sub->sig == evt->sig) {
            Anbo_Arch_Critical_Exit();
            sub->handler(evt, sub->ctx);
            Anbo_Arch_Critical_Enter();
        }
    }

    Anbo_Arch_Critical_Exit();
}

/* ------------------------------------------------------------------ */

void Anbo_EBus_Publish(const Anbo_Event *evt)
{
    if (evt == NULL) {
        return;
    }

    ANBO_TRACE_PUBLISH(evt->sig);
    ebus_dispatch(evt);
}

/* ------------------------------------------------------------------ */

void Anbo_EBus_PublishSilent(const Anbo_Event *evt)
{
    if (evt == NULL) {
        return;
    }

    ebus_dispatch(evt);
}

/* ------------------------------------------------------------------ */

void Anbo_EBus_PublishSig(uint16_t sig, void *param)
{
    Anbo_Event evt;
    evt.sig   = sig;
    evt.param = param;
    Anbo_EBus_Publish(&evt);
}

/* ------------------------------------------------------------------ */

void Anbo_EBus_PublishSigSilent(uint16_t sig, void *param)
{
    Anbo_Event evt;
    evt.sig   = sig;
    evt.param = param;
    Anbo_EBus_PublishSilent(&evt);
}
