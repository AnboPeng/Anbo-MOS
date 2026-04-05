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
 * @file  anbo_rb.c
 * @brief Anbo Kernel — Ring buffer implementation
 *
 * All index advancement uses bitwise AND mask (& mask) instead of modulo (% size).
 * Constraints: C99 / fully static memory / no malloc / no sprintf
 */

#include "anbo_rb.h"

/* ------------------------------------------------------------------ */
/*  Internal helper                                                    */
/* ------------------------------------------------------------------ */

/** Check whether v is a power of two (v > 0) */
static inline int is_power_of_two(uint32_t v)
{
    return (v != 0u) && ((v & (v - 1u)) == 0u);
}

/* ================================================================== */
/*  API Implementation                                                 */
/* ================================================================== */

int Anbo_RB_Init(Anbo_RB *rb, uint8_t *buf, uint32_t size)
{
    if ((rb == NULL) || (buf == NULL) || !is_power_of_two(size)) {
        return -1;
    }
    rb->buf  = buf;
    rb->size = size;
    rb->mask = size - 1u;
    rb->head = 0u;
    rb->tail = 0u;
    return 0;
}

void Anbo_RB_Reset(Anbo_RB *rb)
{
    rb->head = 0u;
    rb->tail = 0u;
}

uint32_t Anbo_RB_Count(const Anbo_RB *rb)
{
    return (rb->head - rb->tail);            /* Unsigned subtraction handles wrap-around naturally */
}

uint32_t Anbo_RB_Free(const Anbo_RB *rb)
{
    return rb->size - Anbo_RB_Count(rb);
}

int Anbo_RB_IsEmpty(const Anbo_RB *rb)
{
    return (rb->head == rb->tail);
}

int Anbo_RB_IsFull(const Anbo_RB *rb)
{
    return (Anbo_RB_Count(rb) == rb->size);
}

/* ------------------------------------------------------------------ */

int Anbo_RB_PutByte(Anbo_RB *rb, uint8_t byte)
{
    if (Anbo_RB_IsFull(rb)) {
        return -1;
    }
    rb->buf[rb->head & rb->mask] = byte;
    rb->head++;
    return 0;
}

int Anbo_RB_GetByte(Anbo_RB *rb, uint8_t *byte)
{
    if (Anbo_RB_IsEmpty(rb)) {
        return -1;
    }
    *byte = rb->buf[rb->tail & rb->mask];
    rb->tail++;
    return 0;
}

/* ------------------------------------------------------------------ */

uint32_t Anbo_RB_Write(Anbo_RB *rb, const uint8_t *data, uint32_t len)
{
    uint32_t free = Anbo_RB_Free(rb);
    uint32_t i;

    if (len > free) {
        len = free;
    }
    for (i = 0u; i < len; i++) {
        rb->buf[rb->head & rb->mask] = data[i];
        rb->head++;
    }
    return len;
}

uint32_t Anbo_RB_Read(Anbo_RB *rb, uint8_t *data, uint32_t len)
{
    uint32_t count = Anbo_RB_Count(rb);
    uint32_t i;

    if (len > count) {
        len = count;
    }
    for (i = 0u; i < len; i++) {
        data[i] = rb->buf[rb->tail & rb->mask];
        rb->tail++;
    }
    return len;
}

uint32_t Anbo_RB_Peek(const Anbo_RB *rb, uint8_t *data, uint32_t len)
{
    uint32_t count = Anbo_RB_Count(rb);
    uint32_t idx   = rb->tail;
    uint32_t i;

    if (len > count) {
        len = count;
    }
    for (i = 0u; i < len; i++) {
        data[i] = rb->buf[idx & rb->mask];
        idx++;
    }
    return len;
}

uint32_t Anbo_RB_Skip(Anbo_RB *rb, uint32_t len)
{
    uint32_t count = Anbo_RB_Count(rb);
    if (len > count) {
        len = count;
    }
    rb->tail += len;
    return len;
}
