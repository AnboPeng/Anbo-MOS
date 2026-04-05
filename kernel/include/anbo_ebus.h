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
 * @file  anbo_ebus.h
 * @brief Anbo Kernel — Event Bus Publish/Subscribe model
 *
 * Design highlights:
 *   - Fully static memory: subscriber nodes are embedded in user struct Anbo_Subscriber.
 *   - Many-to-many: each signal (sig) can have multiple subscribers;
 *     a subscriber can subscribe to multiple signals.
 *   - Synchronous broadcast: Publish iterates the linked list and invokes
 *     callbacks; no queue buffering.
 *   - Hardware isolated: depends only on anbo_arch.h critical section interface.
 *
 * Constraints: C99 / fully static memory / no malloc / no sprintf
 */

#ifndef ANBO_EBUS_H
#define ANBO_EBUS_H

#include <stdint.h>
#include "anbo_config.h"
#include "anbo_list.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================== */
/*  Configuration                                                      */
/* ================================================================== */

/**
 * @def   ANBO_CONF_EBUS_MAX_TOPICS
 * @brief Maximum number of event bus topic (signal value) buckets.
 *        sig is mapped to a bucket via modulo; exact sig matching
 *        is performed within each bucket's linked list.
 */
#ifndef ANBO_CONF_EBUS_MAX_TOPICS
#define ANBO_CONF_EBUS_MAX_TOPICS   32
#endif

/* ================================================================== */
/*  Data Types                                                         */
/* ================================================================== */

/**
 * @struct Anbo_Event
 * @brief  Lightweight event carrier.
 */
typedef struct {
    uint16_t sig;       /**< Signal identifier */
    void    *param;     /**< Event payload parameter (does not own lifetime) */
} Anbo_Event;

/**
 * @typedef Anbo_EventHandler
 * @brief   Subscriber callback function prototype.
 * @param   evt  Pointer to the current event (read-only semantics).
 * @param   ctx  User context passed at subscription time.
 */
typedef void (*Anbo_EventHandler)(const Anbo_Event *evt, void *ctx);

/**
 * @struct Anbo_Subscriber
 * @brief  Statically allocated subscriber descriptor. Users embed it in
 *         their own structures or declare it as a global variable.
 */
typedef struct {
    Anbo_ListNode     node;     /**< Intrusive list node (linked into bucket list) */
    uint16_t          sig;      /**< Signal this subscriber is interested in */
    Anbo_EventHandler handler;  /**< Callback function */
    void             *ctx;      /**< Callback user context */
} Anbo_Subscriber;

/* ================================================================== */
/*  API                                                                */
/* ================================================================== */

/**
 * @brief Initialize the event bus (clear all bucket lists).
 *        Call once at system startup.
 */
void Anbo_EBus_Init(void);

/**
 * @brief Subscribe to a specified signal.
 * @param sub      Subscriber descriptor pointer (statically allocated,
 *                 caller manages lifetime).
 * @param sig      Signal identifier to subscribe to.
 * @param handler  Callback function.
 * @param ctx      User context for the callback.
 *
 * @note  The same sub must not be subscribed twice (caller's responsibility).
 *        Linked list operations are performed inside a critical section (ISR-safe).
 */
void Anbo_EBus_Subscribe(Anbo_Subscriber *sub,
                          uint16_t sig,
                          Anbo_EventHandler handler,
                          void *ctx);

/**
 * @brief Unsubscribe.
 * @param sub  A previously subscribed subscriber descriptor.
 */
void Anbo_EBus_Unsubscribe(Anbo_Subscriber *sub);

/**
 * @brief Publish an event — synchronous broadcast to all callbacks
 *        subscribed to evt->sig.
 * @param evt  Event pointer (typically a stack-local temporary).
 *
 * Internally calls ANBO_TRACE_PUBLISH() to log the event for debugging.
 *
 * @note  Callbacks execute in the publisher's context (or ISR context); keep them short.
 * @note  Do NOT call from a context that is triggered by the log output
 *        system itself (e.g. log-UART TX-complete ISR), otherwise the
 *        trace output creates an infinite feedback loop:
 *          Publish → Trace → Log → UART TX → ISR → Publish → ...
 *        Use Anbo_EBus_PublishSilent() for such cases.
 */
void Anbo_EBus_Publish(const Anbo_Event *evt);

/**
 * @brief Silent publish — identical to Anbo_EBus_Publish() but
 *        skips the ANBO_TRACE_PUBLISH() hook.
 *
 * Use this variant when the publish originates from a path that feeds
 * back into the log/trace system.  Typical example: the log-UART’s
 * TX-complete ISR publishes a “TX done” signal — if that signal were
 * traced, the trace line would itself require UART TX, creating an
 * infinite cycle.  PublishSilent breaks the loop while still delivering
 * the event to all subscribers.
 *
 * Rule of thumb: normal code uses Publish(); only drivers that sit on
 * the log output path need PublishSilent().
 *
 * @param evt  Event pointer.
 */
void Anbo_EBus_PublishSilent(const Anbo_Event *evt);

/**
 * @brief Convenience publish: constructs Anbo_Event internally.
 *
 * Equivalent to:
 * @code
 *   Anbo_Event e = { .sig = sig, .param = param };
 *   Anbo_EBus_Publish(&e);
 * @endcode
 */
void Anbo_EBus_PublishSig(uint16_t sig, void *param);

/**
 * @brief Silent convenience publish (no trace).
 * @see   Anbo_EBus_PublishSilent() for when to use this.
 */
void Anbo_EBus_PublishSigSilent(uint16_t sig, void *param);

#ifdef __cplusplus
}
#endif

#endif /* ANBO_EBUS_H */
