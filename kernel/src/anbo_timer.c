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
 * @file  anbo_timer.c
 * @brief Anbo Kernel — Software timer implementation
 *
 * Internally maintains an intrusive doubly-linked list sorted by deadline in ascending order.
 *   - Start : finds insertion position in ascending order O(n), n = active timer count.
 *   - Update: checks from list head; stops at deadline > now -> average O(1).
 *   - Stop  : direct removal O(1).
 *
 * Time comparison uses signed difference to handle 32-bit wrap-around.
 *
 * Constraints: C99 / fully static memory / no malloc / no sprintf
 */

#include "anbo_timer.h"
#include "anbo_arch.h"

/* ================================================================== */
/*  Internal Data                                                      */
/* ================================================================== */

/** Active timer ascending linked list head (sentinel node) */
static Anbo_ListNode s_active_list;

/* ================================================================== */
/*  Internal Helpers                                                   */
/* ================================================================== */

/**
 * @brief Signed time difference: a - b, handles uint32 wrap-around.
 *        Result <= 0 means a is before or equal to b.
 */
static inline int32_t time_diff(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b);
}

/**
 * @brief Insert timer into the active list in ascending deadline order.
 *        Must be called inside a critical section.
 */
static void timer_insert_sorted(Anbo_Timer *tmr)
{
    Anbo_ListNode *pos;

    /* Find the first node with deadline > tmr->deadline and insert before it */
    ANBO_LIST_FOR_EACH(pos, &s_active_list) {
        Anbo_Timer *cur = ANBO_LIST_ENTRY(pos, Anbo_Timer, node);
        if (time_diff(cur->deadline, tmr->deadline) > 0) {
            /* cur expires later than tmr; insert before cur */
            anbo_list_insert_between(&tmr->node, pos->prev, pos);
            return;
        }
    }
    /* No later deadline found; insert at the tail */
    Anbo_List_InsertTail(&s_active_list, &tmr->node);
}

/* ================================================================== */
/*  API Implementation                                                 */
/* ================================================================== */

void Anbo_Timer_Init(void)
{
    Anbo_List_Init(&s_active_list);
}

/* ------------------------------------------------------------------ */

void Anbo_Timer_Create(Anbo_Timer         *tmr,
                        Anbo_TimerMode      mode,
                        uint32_t            period_ms,
                        Anbo_TimerCallback  cb,
                        void               *user_data)
{
    if (tmr == NULL) {
        return;
    }
    Anbo_List_Init(&tmr->node);
    tmr->deadline  = 0u;
    tmr->period    = period_ms;
    tmr->mode      = mode;
    tmr->state     = ANBO_TIMER_STOPPED;
    tmr->callback  = cb;
    tmr->user_data = user_data;
}

/* ------------------------------------------------------------------ */

void Anbo_Timer_Start(Anbo_Timer *tmr)
{
    if ((tmr == NULL) || (tmr->callback == NULL)) {
        return;
    }

    Anbo_Arch_Critical_Enter();

    /* If already in the list, remove first */
    if (tmr->state == ANBO_TIMER_RUNNING) {
        Anbo_List_Remove(&tmr->node);
    }

    tmr->deadline = Anbo_Arch_GetTick() + tmr->period;
    tmr->state    = ANBO_TIMER_RUNNING;
    timer_insert_sorted(tmr);

    Anbo_Arch_Critical_Exit();
}

/* ------------------------------------------------------------------ */

void Anbo_Timer_Stop(Anbo_Timer *tmr)
{
    if (tmr == NULL) {
        return;
    }

    Anbo_Arch_Critical_Enter();

    if (tmr->state == ANBO_TIMER_RUNNING) {
        Anbo_List_Remove(&tmr->node);
        tmr->state = ANBO_TIMER_STOPPED;
    }

    Anbo_Arch_Critical_Exit();
}

/* ------------------------------------------------------------------ */

void Anbo_Timer_SetPeriod(Anbo_Timer *tmr, uint32_t period_ms)
{
    if (tmr != NULL) {
        tmr->period = period_ms;
    }
}

/* ------------------------------------------------------------------ */

int Anbo_Timer_IsRunning(const Anbo_Timer *tmr)
{
    if (tmr == NULL) {
        return 0;
    }
    return (tmr->state == ANBO_TIMER_RUNNING);
}

/* ------------------------------------------------------------------ */

void Anbo_Timer_Update(uint32_t now)
{
    Anbo_ListNode *pos;
    Anbo_ListNode *tmp;
    Anbo_Timer    *tmr;

    Anbo_Arch_Critical_Enter();

    ANBO_LIST_FOR_EACH_SAFE(pos, tmp, &s_active_list) {
        tmr = ANBO_LIST_ENTRY(pos, Anbo_Timer, node);

        /* Ascending list: once deadline > now, all remaining are later; exit */
        if (time_diff(tmr->deadline, now) > 0) {
            break;
        }

        /* Expired — remove from list */
        Anbo_List_Remove(&tmr->node);
        tmr->state = ANBO_TIMER_STOPPED;

        /* Periodic mode: recalculate deadline and re-insert */
        if (tmr->mode == ANBO_TIMER_PERIODIC) {
            tmr->deadline = now + tmr->period;
            tmr->state    = ANBO_TIMER_RUNNING;
            timer_insert_sorted(tmr);
        }

        /* Execute callback outside the critical section */
        Anbo_Arch_Critical_Exit();
        if (tmr->callback != NULL) {
            tmr->callback(tmr);
        }
        Anbo_Arch_Critical_Enter();
    }

    Anbo_Arch_Critical_Exit();
}

/* ------------------------------------------------------------------ */

uint32_t Anbo_Timer_MsToNext(uint32_t now)
{
    Anbo_ListNode *first;
    Anbo_Timer    *tmr;
    int32_t        diff;

    Anbo_Arch_Critical_Enter();

    if (Anbo_List_IsEmpty(&s_active_list)) {
        Anbo_Arch_Critical_Exit();
        return UINT32_MAX;
    }

    first = s_active_list.next;
    tmr   = ANBO_LIST_ENTRY(first, Anbo_Timer, node);
    diff  = time_diff(tmr->deadline, now);

    Anbo_Arch_Critical_Exit();

    return (diff > 0) ? (uint32_t)diff : 0u;
}
