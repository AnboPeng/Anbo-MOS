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
 * @file  anbo_fsm.h
 * @brief Anbo Kernel — Object-oriented Finite State Machine (FSM)
 *
 * Design highlights:
 *   - Each state is described by Anbo_State, with on_entry / on_exit / on_event callbacks.
 *   - FSM instance (Anbo_FSM) embeds Anbo_Subscriber, acting automatically as
 *     an event bus subscriber.
 *   - Anbo_FSM_Transfer() performs non-blocking state transitions:
 *         old state on_exit -> update current state -> new state on_entry.
 *   - Fully static memory, zero blocking (no while(1)).
 *
 * Constraints: C99 / fully static memory / no malloc / no sprintf
 */

#ifndef ANBO_FSM_H
#define ANBO_FSM_H

#include <stdint.h>
#include "anbo_config.h"
#include "anbo_ebus.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================== */
/*  Forward Declarations                                               */
/* ================================================================== */

typedef struct Anbo_FSM Anbo_FSM;

/* ================================================================== */
/*  State Callback Prototypes                                          */
/* ================================================================== */

/**
 * @typedef Anbo_StateOnEntry
 * @brief   Called upon entering a state (initialize resources, start timers, etc.).
 * @param   fsm  The owning FSM instance.
 */
typedef void (*Anbo_StateOnEntry)(Anbo_FSM *fsm);

/**
 * @typedef Anbo_StateOnExit
 * @brief   Called upon leaving a state (clean up resources, stop timers, etc.).
 * @param   fsm  The owning FSM instance.
 */
typedef void (*Anbo_StateOnExit)(Anbo_FSM *fsm);

/**
 * @typedef Anbo_StateOnEvent
 * @brief   Current state event handler.
 * @param   fsm  The owning FSM instance.
 * @param   evt  The received event.
 *
 * May call Anbo_FSM_Transfer() internally for state transitions.
 */
typedef void (*Anbo_StateOnEvent)(Anbo_FSM *fsm, const Anbo_Event *evt);

/* ================================================================== */
/*  State Descriptor                                                   */
/* ================================================================== */

/**
 * @struct Anbo_State
 * @brief  Describes a single state in the FSM.
 *
 * Users declare as const global variables, one instance per state.
 * Any callback field may be NULL (meaning no action at that phase).
 */
typedef struct {
    const char        *name;       /**< State name (for debugging, may be NULL) */
    Anbo_StateOnEntry  on_entry;   /**< Entry callback */
    Anbo_StateOnExit   on_exit;    /**< Exit callback */
    Anbo_StateOnEvent  on_event;   /**< Event handler callback */
} Anbo_State;

/* ================================================================== */
/*  FSM Instance                                                       */
/* ================================================================== */

/**
 * @struct Anbo_FSM
 * @brief  Finite state machine instance (statically allocated by user).
 *
 * Embeds an Anbo_Subscriber array so one FSM can subscribe to multiple signals.
 */

/** Maximum number of signals a single FSM can subscribe to simultaneously */
#ifndef ANBO_CONF_FSM_MAX_SUBS
#define ANBO_CONF_FSM_MAX_SUBS      8
#endif

struct Anbo_FSM {
    const char          *name;      /**< FSM name (for debugging) */
    const Anbo_State    *current;   /**< Current state pointer */
    Anbo_Subscriber      subs[ANBO_CONF_FSM_MAX_SUBS]; /**< Subscriber node pool */
    uint8_t              sub_count; /**< Number of subscribers in use */
    void                *user_data; /**< User-defined context */
};

/* ================================================================== */
/*  API                                                                */
/* ================================================================== */

/**
 * @brief Initialize an FSM instance and enter the initial state.
 * @param fsm        FSM instance pointer (statically allocated).
 * @param name       FSM name (for debugging, may be NULL).
 * @param initial    Initial state. initial->on_entry is called immediately.
 * @param user_data  User context (may be NULL).
 */
void Anbo_FSM_Init(Anbo_FSM         *fsm,
                    const char       *name,
                    const Anbo_State *initial,
                    void             *user_data);

/**
 * @brief Subscribe an FSM to an event bus signal.
 * @param fsm  FSM instance.
 * @param sig  Signal to subscribe to.
 * @retval 0   Success.
 * @retval -1  Subscription pool is full.
 *
 * When the event bus publishes sig, it is automatically routed to
 * the FSM's current state on_event callback.
 */
int Anbo_FSM_Subscribe(Anbo_FSM *fsm, uint16_t sig);

/**
 * @brief Unsubscribe the FSM from all event bus signals.
 */
void Anbo_FSM_UnsubscribeAll(Anbo_FSM *fsm);

/**
 * @brief Non-blocking state transition.
 * @param fsm    FSM instance.
 * @param target Target state.
 *
 * Execution order:
 *   1. current->on_exit(fsm)     (if not NULL)
 *   2. fsm->current = target
 *   3. target->on_entry(fsm)     (if not NULL)
 *
 * May be called inside on_event / on_entry callbacks (not recursion-safe;
 * only one transition per event handling cycle).
 */
void Anbo_FSM_Transfer(Anbo_FSM *fsm, const Anbo_State *target);

/**
 * @brief Dispatch an event directly to the FSM (bypassing the event bus).
 * @param fsm  FSM instance.
 * @param evt  Event.
 *
 * Used for ISR or internal module direct FSM driving.
 */
void Anbo_FSM_Dispatch(Anbo_FSM *fsm, const Anbo_Event *evt);

/**
 * @brief Get the current state name.
 * @return State name string; returns "?" if current or name is NULL.
 */
const char *Anbo_FSM_StateName(const Anbo_FSM *fsm);

#ifdef __cplusplus
}
#endif

#endif /* ANBO_FSM_H */
