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
 * @file  anbo_rb.h
 * @brief Anbo Kernel — Static Ring Buffer
 *
 * Single-producer / single-consumer lock-free safe (between ISR and main loop).
 * Capacity must be a power of two; index advancement uses bitmask instead of modulo.
 *
 * Constraints: C99 / fully static memory / no malloc / no sprintf
 */

#ifndef ANBO_RB_H
#define ANBO_RB_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================== */
/*  Data Structures                                                    */
/* ================================================================== */

/**
 * @struct Anbo_RB
 * @brief  Ring buffer descriptor.
 *
 * @note   size must be a power of two; mask = size - 1.
 *         head: next write position (modified only by producer).
 *         tail: next read position (modified only by consumer).
 */
typedef struct {
    uint8_t  *buf;      /**< Pointer to user-provided static buffer */
    uint32_t  size;     /**< Total buffer size in bytes (must be 2^N) */
    uint32_t  mask;     /**< size - 1, used for bitwise AND instead of modulo */
    volatile uint32_t head;  /**< Write index */
    volatile uint32_t tail;  /**< Read index */
} Anbo_RB;

/* ================================================================== */
/*  Compile-time Static Declaration Macro                              */
/* ================================================================== */

/**
 * @def   ANBO_RB_DEF(name, order)
 * @brief Declare a ring buffer with capacity of 2^order bytes at compile time.
 *
 * Example: ANBO_RB_DEF(uart_rx_rb, 8);  // 256 bytes
 */
#define ANBO_RB_DEF(name, order)                                \
    static uint8_t ANBO_POOL_SECTION name##_storage[1u << (order)]; \
    static Anbo_RB name = {                                     \
        .buf  = name##_storage,                                 \
        .size = (1u << (order)),                                \
        .mask = (1u << (order)) - 1u,                           \
        .head = 0u,                                             \
        .tail = 0u                                              \
    }

/* ================================================================== */
/*  API                                                                */
/* ================================================================== */

/**
 * @brief Runtime initialization of a ring buffer.
 * @param rb    Buffer descriptor pointer.
 * @param buf   User-provided static storage area.
 * @param size  Storage area size in bytes (must be a power of two).
 * @retval 0    Success.
 * @retval -1   Invalid parameter (size is not a power of two, or pointer is NULL).
 */
int Anbo_RB_Init(Anbo_RB *rb, uint8_t *buf, uint32_t size);

/**
 * @brief Reset the buffer (clear data without erasing storage contents).
 */
void Anbo_RB_Reset(Anbo_RB *rb);

/**
 * @brief Number of valid data bytes currently enqueued.
 */
uint32_t Anbo_RB_Count(const Anbo_RB *rb);

/**
 * @brief Remaining writable byte count.
 */
uint32_t Anbo_RB_Free(const Anbo_RB *rb);

/**
 * @brief Check whether the buffer is empty.
 */
int Anbo_RB_IsEmpty(const Anbo_RB *rb);

/**
 * @brief Check whether the buffer is full.
 */
int Anbo_RB_IsFull(const Anbo_RB *rb);

/**
 * @brief Write a single byte.
 * @retval 0   Success.
 * @retval -1  Buffer is full.
 */
int Anbo_RB_PutByte(Anbo_RB *rb, uint8_t byte);

/**
 * @brief Read a single byte.
 * @param[out] byte  Receives the read byte.
 * @retval 0   Success.
 * @retval -1  Buffer is empty.
 */
int Anbo_RB_GetByte(Anbo_RB *rb, uint8_t *byte);

/**
 * @brief Bulk write.
 * @return Number of bytes actually written.
 */
uint32_t Anbo_RB_Write(Anbo_RB *rb, const uint8_t *data, uint32_t len);

/**
 * @brief Bulk read.
 * @return Number of bytes actually read.
 */
uint32_t Anbo_RB_Read(Anbo_RB *rb, uint8_t *data, uint32_t len);

/**
 * @brief Peek (read without advancing the read pointer).
 * @return Number of bytes actually peeked.
 */
uint32_t Anbo_RB_Peek(const Anbo_RB *rb, uint8_t *data, uint32_t len);

/**
 * @brief Skip (advance read pointer without reading data).
 *
 * Companion to Anbo_RB_Peek(): first Peek into a local buffer,
 * hand the data to a consumer, then Skip only the bytes that
 * were actually accepted.  This prevents data loss when the
 * downstream buffer is full.
 *
 * @param rb   Ring buffer.
 * @param len  Number of bytes to skip (clamped to available count).
 * @return Number of bytes actually skipped.
 */
uint32_t Anbo_RB_Skip(Anbo_RB *rb, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* ANBO_RB_H */
