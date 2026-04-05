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
 * @file  b_l4s5i_uart_drv.h
 * @brief B-L4S5I — Interrupt-driven USART1 Anbo Device driver (ST-LINK VCP).
 *
 * Architecture:
 *   TX: Anbo_Log_Flush() -> Anbo_Dev_AsyncWrite() -> write into tx_rb.
 *       If UART TXE IRQ is idle, kick it.  TXE ISR pulls bytes from
 *       tx_rb one by one until empty, then disables TXE IRQ.
 *   RX: RXNE ISR reads byte -> push into rx_rb -> publish ANBO_SIG_UART_RX.
 *
 * Uses USART1 (PB6/PB7) routed to ST-LINK Virtual COM Port.
 * Strictly interrupt-driven.  No HAL_UART_Transmit / HAL_UART_Receive.
 * No DMA.  Zero blocking in the hot path.
 */

#ifndef B_L4S5I_UART_DRV_H
#define B_L4S5I_UART_DRV_H

#include "anbo_dev.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================== */
/*  Application signal definitions                                     */
/* ================================================================== */

/** Signal published on event bus when a byte is received on USART1 */
#define ANBO_SIG_UART_RX        0x0010u

/** Signal published when a TX chunk completes (tx_rb drained) */
#define ANBO_SIG_UART_TX        0x0011u

/** Signal published when the user button (PC13) is pressed */
#define ANBO_SIG_USER_BUTTON    0x0020u

/* ================================================================== */
/*  API                                                                */
/* ================================================================== */

/**
 * @brief Get the singleton USART1 Anbo_Device instance.
 *
 * The device is fully initialised after BSP_Init() has been called.
 * Bind it to the log system via Anbo_Log_Init(BSP_USART1_GetDevice()).
 *
 * @return Pointer to the static Anbo_Device for USART1 (VCP).
 */
Anbo_Device *BSP_USART1_GetDevice(void);

/**
 * @brief USART1 IRQ entry point.
 *
 * Must be called from the USART1_IRQHandler vector.
 * Handles both TXE (transmit) and RXNE (receive) interrupt flags.
 */
void BSP_USART1_IRQ(void);

#ifdef __cplusplus
}
#endif

#endif /* B_L4S5I_UART_DRV_H */
