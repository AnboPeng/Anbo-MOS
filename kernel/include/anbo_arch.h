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
 * @file  anbo_arch.h
 * @brief Anbo Kernel — Hardware Abstraction Layer interface (Arch HAL)
 *
 * This file only declares function prototypes and macro interfaces.
 * Actual implementations are provided by each platform's porting layer (port/).
 * Constraints: C99 / fully static memory / no malloc / no sprintf
 */

#ifndef ANBO_ARCH_H
#define ANBO_ARCH_H

#include <stdint.h>
#include "anbo_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================== */
/*  1. Critical Section — Global interrupt lock                        */
/* ================================================================== */

/**
 * @brief Enter critical section (disable global interrupts).
 *
 * Implementation must save current interrupt state to support nesting.
 * Typical Cortex-M implementation: PRIMASK / BASEPRI approach.
 */
void Anbo_Arch_Critical_Enter(void);

/**
 * @brief Exit critical section (restore interrupt state).
 */
void Anbo_Arch_Critical_Exit(void);

/* ================================================================== */
/*  2. System Heartbeat                                                */
/* ================================================================== */

/**
 * @brief Get millisecond-resolution monotonic clock.
 * @return Milliseconds since system startup (32-bit wrap-around safe).
 *
 * The porting layer typically maintains a static counter incremented
 * by a SysTick / general-purpose timer interrupt.
 */
uint32_t Anbo_Arch_GetTick(void);

/* ================================================================== */
/*  3. Watchdog                                                        */
/* ================================================================== */

#if ANBO_CONF_WDT
/**
 * @brief Feed the physical watchdog (IWDG / WWDG).
 *
 * Only needs to be implemented when ANBO_CONF_WDT == 1.
 */
void Anbo_Arch_WDT_Feed(void);
#endif

/* ================================================================== */
/*  4. Low-power Idle                                                  */
/* ================================================================== */

#if ANBO_CONF_IDLE_SLEEP
/**
 * @brief Enter low-power sleep for at most @p ms milliseconds.
 * @param ms  Maximum sleep duration (milliseconds). 0 = return immediately.
 *
 * Typical Cortex-M implementation: configure SysTick then execute WFI / WFE.
 */
void Anbo_Arch_Idle(uint32_t ms);
#endif

/* ================================================================== */
/*  5. UART — Low-level serial output for logging                      */
/* ================================================================== */

/**
 * @brief Blocking single-byte transmit on the log UART.
 * @param c  Character to send.
 *
 * Used as a fallback when no Anbo_Device is bound to the log system,
 * or during early boot before the device model is initialised.
 * Porting layers should implement this via the UART TDR register + busy-wait.
 */
void Anbo_Arch_UART_PutChar(char c);

/**
 * @brief Non-blocking DMA / interrupt-driven bulk transmit.
 * @param buf  Pointer to data buffer (must remain valid until transfer completes).
 * @param len  Number of bytes to send.
 * @return 0 on success, -1 on failure (e.g., DMA channel busy).
 *
 * If the platform does not support DMA, this may fall back to
 * a polled byte-by-byte loop internally.
 */
int Anbo_Arch_UART_Transmit_DMA(const uint8_t *buf, uint32_t len);

/* ================================================================== */
/*  6. CLZ — Count Leading Zeros bit-acceleration macro                */
/* ================================================================== */

/**
 * @def   ANBO_ARCH_CLZ(x)
 * @brief Return the number of leading zero bits in a 32-bit value @p x.
 *
 * - GCC / Clang : __builtin_clz
 * - ARMCC / IAR  : __CLZ (intrinsic)
 * - Generic fallback: software bit-scan
 *
 * Used for quickly locating the highest-priority ready task in a priority bitmap.
 * Note: behavior is undefined when x == 0 (same as __builtin_clz).
 */
#if defined(__GNUC__) || defined(__clang__)
    #define ANBO_ARCH_CLZ(x)    ((uint8_t)__builtin_clz((uint32_t)(x)))
#elif defined(__ARMCC_VERSION)
    #include <arm_compat.h>
    #define ANBO_ARCH_CLZ(x)    ((uint8_t)__clz((uint32_t)(x)))
#elif defined(__ICCARM__)
    #include <intrinsics.h>
    #define ANBO_ARCH_CLZ(x)    ((uint8_t)__CLZ((uint32_t)(x)))
#else
    /* Generic software fallback */
    static inline uint8_t anbo_sw_clz(uint32_t v)
    {
        uint8_t n = 0;
        if (v == 0u) { return 32u; }
        if ((v & 0xFFFF0000u) == 0u) { n += 16u; v <<= 16u; }
        if ((v & 0xFF000000u) == 0u) { n +=  8u; v <<=  8u; }
        if ((v & 0xF0000000u) == 0u) { n +=  4u; v <<=  4u; }
        if ((v & 0xC0000000u) == 0u) { n +=  2u; v <<=  2u; }
        if ((v & 0x80000000u) == 0u) { n +=  1u; }
        return n;
    }
    #define ANBO_ARCH_CLZ(x)    anbo_sw_clz((uint32_t)(x))
#endif

#ifdef __cplusplus
}
#endif

#endif /* ANBO_ARCH_H */
