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
 * @file  anbo_wdt.c
 * @brief Anbo Kernel — Software watchdog monitor implementation
 *
 * Fully static slot pool; iterates to check for timeouts;
 * feeds the physical watchdog only when all slots are healthy.
 *
 * Constraints: C99 / fully static memory / no malloc / no sprintf
 */

#include "anbo_wdt.h"

#if ANBO_CONF_WDT

#include "anbo_arch.h"
#include <stddef.h>   /* NULL */

/* ================================================================== */
/*  Internal Data Structures                                           */
/* ================================================================== */

typedef enum {
    SLOT_FREE      = 0,     /**< Unallocated */
    SLOT_ACTIVE    = 1,     /**< Actively monitored */
    SLOT_SUSPENDED = 2      /**< Monitoring suspended */
} SlotState;

typedef struct {
    const char *name;           /**< Task name (for debugging) */
    uint32_t    timeout_ms;     /**< Timeout threshold */
    uint32_t    last_checkin;   /**< Most recent check-in timestamp (ms) */
    SlotState   state;          /**< Current state */
} WDT_Slot;

/* ================================================================== */
/*  Static Pool                                                        */
/* ================================================================== */

static WDT_Slot  ANBO_POOL_SECTION s_slots[ANBO_CONF_WDT_MAX_SLOTS];
static uint8_t   s_slot_count;  /**< Number of allocated slots */

/* ================================================================== */
/*  Internal Helpers                                                   */
/* ================================================================== */

/**
 * @brief Signed time difference: a - b, handles uint32 wrap-around.
 */
static inline int32_t time_diff(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b);
}

/** Validate a slot handle */
static inline int slot_valid(Anbo_WDT_Slot slot)
{
    return (slot >= 0) && ((uint8_t)slot < s_slot_count);
}

/* ================================================================== */
/*  API Implementation                                                 */
/* ================================================================== */

void Anbo_WDT_Init(void)
{
    uint8_t i;
    for (i = 0u; i < ANBO_CONF_WDT_MAX_SLOTS; i++) {
        s_slots[i].name         = NULL;
        s_slots[i].timeout_ms   = 0u;
        s_slots[i].last_checkin = 0u;
        s_slots[i].state        = SLOT_FREE;
    }
    s_slot_count = 0u;
}

/* ------------------------------------------------------------------ */

Anbo_WDT_Slot Anbo_WDT_Register(const char *name, uint32_t timeout_ms)
{
    Anbo_WDT_Slot slot;

    if (s_slot_count >= ANBO_CONF_WDT_MAX_SLOTS) {
        return ANBO_WDT_SLOT_INVALID;
    }

    slot = (Anbo_WDT_Slot)s_slot_count;
    s_slots[slot].name         = name;
    s_slots[slot].timeout_ms   = timeout_ms;
    s_slots[slot].last_checkin = Anbo_Arch_GetTick();
    s_slots[slot].state        = SLOT_ACTIVE;
    s_slot_count++;

    return slot;
}

/* ------------------------------------------------------------------ */

void Anbo_WDT_Checkin(Anbo_WDT_Slot slot)
{
    if (!slot_valid(slot)) {
        return;
    }
    /* Only refresh timestamp when in ACTIVE state */
    if (s_slots[slot].state == SLOT_ACTIVE) {
        s_slots[slot].last_checkin = Anbo_Arch_GetTick();
    }
}

/* ------------------------------------------------------------------ */

void Anbo_WDT_Suspend(Anbo_WDT_Slot slot)
{
    if (!slot_valid(slot)) {
        return;
    }
    s_slots[slot].state = SLOT_SUSPENDED;
}

/* ------------------------------------------------------------------ */

void Anbo_WDT_Resume(Anbo_WDT_Slot slot)
{
    if (!slot_valid(slot)) {
        return;
    }
    s_slots[slot].last_checkin = Anbo_Arch_GetTick();
    s_slots[slot].state = SLOT_ACTIVE;
}

/* ------------------------------------------------------------------ */

int Anbo_WDT_Monitor(uint32_t now)
{
    uint8_t i;

    for (i = 0u; i < s_slot_count; i++) {
        if (s_slots[i].state != SLOT_ACTIVE) {
            continue;   /* Skip unallocated & suspended */
        }
        if (time_diff(now, s_slots[i].last_checkin) >
            (int32_t)s_slots[i].timeout_ms) {
            /* Timeout detected — do NOT feed the watchdog */
            return 0;
        }
    }

    /* All healthy — feed the physical watchdog */
    Anbo_Arch_WDT_Feed();
    return 1;
}

/* ------------------------------------------------------------------ */

const char *Anbo_WDT_FirstTimeout(uint32_t now)
{
    uint8_t i;

    for (i = 0u; i < s_slot_count; i++) {
        if (s_slots[i].state != SLOT_ACTIVE) {
            continue;
        }
        if (time_diff(now, s_slots[i].last_checkin) >
            (int32_t)s_slots[i].timeout_ms) {
            return s_slots[i].name;
        }
    }
    return NULL;
}

#else  /* !ANBO_CONF_WDT */
typedef int anbo_wdt_empty_tu; /* ISO C forbids empty translation unit */
#endif /* ANBO_CONF_WDT */
