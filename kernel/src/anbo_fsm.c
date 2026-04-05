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
 * @file  anbo_fsm.c
 * @brief Anbo Kernel — Object-oriented FSM implementation
 *
 * The FSM carries its own pointer via Anbo_Subscriber.ctx, so the event bus
 * callback can directly retrieve the FSM instance — zero extra lookup overhead.
 *
 * Constraints: C99 / fully static memory / no malloc / no sprintf
 */

#include "anbo_fsm.h"
#include "anbo_arch.h"
#include "anbo_log.h"
#include <stddef.h>   /* NULL */
#include <string.h>   /* memset */

/* ================================================================== */
/*  Internal: Event bus unified callback                               */
/* ================================================================== */

/**
 * @brief Event bus callback bridge.
 *
 * ctx points to the Anbo_FSM instance (set during Subscribe).
 * Delegates the event to the FSM's current state on_event handler.
 */
static void fsm_ebus_handler(const Anbo_Event *evt, void *ctx)
{
    Anbo_FSM *fsm = (Anbo_FSM *)ctx;

    if ((fsm == NULL) || (fsm->current == NULL) ||
        (fsm->current->on_event == NULL)) {
        return;
    }

    ANBO_TRACE_FSM_DISPATCH(
        (fsm->name != NULL) ? fsm->name : "?",
        evt->sig);

    fsm->current->on_event(fsm, evt);
}

/* ================================================================== */
/*  API Implementation                                                 */
/* ================================================================== */

void Anbo_FSM_Init(Anbo_FSM         *fsm,
                    const char       *name,
                    const Anbo_State *initial,
                    void             *user_data)
{
    if (fsm == NULL) {
        return;
    }

    memset(fsm->subs, 0, sizeof(fsm->subs));
    fsm->name      = name;
    fsm->current   = NULL;
    fsm->sub_count = 0u;
    fsm->user_data = user_data;

    /* Enter the initial state */
    if (initial != NULL) {
        fsm->current = initial;
        if (initial->on_entry != NULL) {
            initial->on_entry(fsm);
        }
    }
}

/* ------------------------------------------------------------------ */

int Anbo_FSM_Subscribe(Anbo_FSM *fsm, uint16_t sig)
{
    Anbo_Subscriber *sub;

    if ((fsm == NULL) || (fsm->sub_count >= ANBO_CONF_FSM_MAX_SUBS)) {
        return -1;
    }

    sub = &fsm->subs[fsm->sub_count];

    /* Register with event bus; ctx = the FSM itself */
    Anbo_EBus_Subscribe(sub, sig, fsm_ebus_handler, fsm);

    fsm->sub_count++;
    return 0;
}

/* ------------------------------------------------------------------ */

void Anbo_FSM_UnsubscribeAll(Anbo_FSM *fsm)
{
    uint8_t i;

    if (fsm == NULL) {
        return;
    }

    for (i = 0u; i < fsm->sub_count; i++) {
        Anbo_EBus_Unsubscribe(&fsm->subs[i]);
    }
    fsm->sub_count = 0u;
}

/* ------------------------------------------------------------------ */

void Anbo_FSM_Transfer(Anbo_FSM *fsm, const Anbo_State *target)
{
    if ((fsm == NULL) || (target == NULL)) {
        return;
    }

    ANBO_TRACE_FSM_TRANSFER(
        (fsm->name != NULL) ? fsm->name : "?",
        Anbo_FSM_StateName(fsm),
        (target->name != NULL) ? target->name : "?");

    /* 1. Exit current state */
    if ((fsm->current != NULL) && (fsm->current->on_exit != NULL)) {
        fsm->current->on_exit(fsm);
    }

    /* 2. Switch */
    fsm->current = target;

    /* 3. Enter new state */
    if (target->on_entry != NULL) {
        target->on_entry(fsm);
    }
}

/* ------------------------------------------------------------------ */

void Anbo_FSM_Dispatch(Anbo_FSM *fsm, const Anbo_Event *evt)
{
    if ((fsm == NULL) || (evt == NULL)) {
        return;
    }
    if ((fsm->current != NULL) && (fsm->current->on_event != NULL)) {
        fsm->current->on_event(fsm, evt);
    }
}

/* ------------------------------------------------------------------ */

const char *Anbo_FSM_StateName(const Anbo_FSM *fsm)
{
    if ((fsm == NULL) || (fsm->current == NULL) ||
        (fsm->current->name == NULL)) {
        return "?";
    }
    return fsm->current->name;
}
