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
 * @file  b_l4s5i_uart_drv.c
 * @brief B-L4S5I — Pure interrupt-driven USART1 Anbo_Device driver (VCP).
 *
 * TX path (zero-copy from ring buffer):
 *   1. Anbo_Dev_AsyncWrite() -> usart1_write() -> push bytes into tx_rb.
 *   2. If TXE IRQ is not already active, enable it (kick).
 *   3. TXE ISR: pull one byte from tx_rb -> write to TDR.
 *      When tx_rb is empty, disable TXE IRQ and publish ANBO_SIG_UART_TX.
 *
 * RX path:
 *   1. RXNE ISR: read RDR -> push into rx_rb.
 *   2. Publish ANBO_SIG_UART_RX on event bus (param = &s_usart1_dev).
 *   3. Upper layer reads via Anbo_Dev_Read() -> usart1_read() -> pull from rx_rb.
 *
 * No HAL_UART_Transmit.  No HAL_UART_Receive.  No DMA.
 * Direct register access for the ISR hot path.
 */

#include "b_l4s5i_uart_drv.h"
#include "b_l4s5i_hw.h"
#include "anbo_rb.h"
#include "anbo_ebus.h"

#include "stm32l4xx_hal.h"

/* ================================================================== */
/*  USART register-bit compatibility                                   */
/* ================================================================== */
/*
 * STM32L4S5 has FIFO-capable USART, so the CMSIS headers define the
 * register bits with _TXFNF / _RXFNE suffixes.  Map the traditional
 * short names so the driver code stays readable.
 */
#ifndef USART_ISR_TXE
#define USART_ISR_TXE       USART_ISR_TXE_TXFNF
#endif
#ifndef USART_ISR_RXNE
#define USART_ISR_RXNE      USART_ISR_RXNE_RXFNE
#endif
#ifndef USART_CR1_TXEIE
#define USART_CR1_TXEIE     USART_CR1_TXEIE_TXFNFIE
#endif
#ifndef USART_CR1_RXNEIE
#define USART_CR1_RXNEIE    USART_CR1_RXNEIE_RXFNEIE
#endif

/* ================================================================== */
/*  Static ring buffers (TX: 512 B, RX: 256 B)                        */
/* ================================================================== */

ANBO_RB_DEF(s_usart1_tx_rb, 9);     /* 512 bytes */
ANBO_RB_DEF(s_usart1_rx_rb, 8);     /* 256 bytes */

/* ================================================================== */
/*  Driver ops — forward declarations                                  */
/* ================================================================== */

static int      usart1_open(Anbo_Device *dev);
static void     usart1_close(Anbo_Device *dev);
static uint32_t usart1_write(Anbo_Device *dev, const uint8_t *data, uint32_t len);
static uint32_t usart1_read(Anbo_Device *dev, uint8_t *buf, uint32_t len);

/* ================================================================== */
/*  Device ops vtable                                                  */
/* ================================================================== */

static const Anbo_DevOps s_usart1_ops = {
    .open   = usart1_open,
    .close  = usart1_close,
    .write  = usart1_write,
    .read   = usart1_read,
    .ioctl  = NULL,
};

/* ================================================================== */
/*  Singleton device instance                                          */
/* ================================================================== */

static Anbo_Device s_usart1_dev = {
    .name     = "usart1",
    .ops      = &s_usart1_ops,
    .tx_rb    = &s_usart1_tx_rb,
    .rx_rb    = &s_usart1_rx_rb,
    .tx_done  = NULL,
    .rx_ready = NULL,
    .sig_tx   = ANBO_SIG_UART_TX,
    .sig_rx   = ANBO_SIG_UART_RX,
    .priv     = NULL,       /* will be set to UART_HandleTypeDef* on open */
    .flags    = 0u,
};

/* ================================================================== */
/*  Public API                                                         */
/* ================================================================== */

Anbo_Device *BSP_USART1_GetDevice(void)
{
    return &s_usart1_dev;
}

/* ================================================================== */
/*  Driver ops implementation                                          */
/* ================================================================== */

static int usart1_open(Anbo_Device *dev)
{
    /*
     * Hardware is already initialised by BSP_Init() -> USART1_Init().
     * Just cache the handle and enable RXNE interrupt.
     */
    UART_HandleTypeDef *h = (UART_HandleTypeDef *)BSP_GetLogUartHandle();
    dev->priv = (void *)h;

    /* Enable RXNE interrupt for byte-level reception */
    USART_TypeDef *uart = h->Instance;
    uart->CR1 |= USART_CR1_RXNEIE;

    return 0;
}

static void usart1_close(Anbo_Device *dev)
{
    UART_HandleTypeDef *h = (UART_HandleTypeDef *)dev->priv;
    if (h != NULL) {
        USART_TypeDef *uart = h->Instance;
        /* Disable both TXE and RXNE interrupts */
        uart->CR1 &= ~(USART_CR1_TXEIE | USART_CR1_RXNEIE);
    }
}

/**
 * @brief Non-blocking write: push data into tx_rb, kick TXE IRQ.
 * @return Number of bytes actually accepted (may be < len if rb full).
 */
static uint32_t usart1_write(Anbo_Device *dev, const uint8_t *data, uint32_t len)
{
    uint32_t written;
    UART_HandleTypeDef *h = (UART_HandleTypeDef *)dev->priv;

    if ((h == NULL) || (data == NULL) || (len == 0u)) {
        return 0u;
    }

    /* Push into TX ring buffer (lock-free: main-loop is sole producer) */
    written = Anbo_RB_Write(dev->tx_rb, data, len);

    if (written > 0u) {
        /* Kick TXE interrupt — ISR will start pulling bytes */
        USART_TypeDef *uart = h->Instance;
        uart->CR1 |= USART_CR1_TXEIE;
    }

    return written;
}

/**
 * @brief Non-blocking read: pull data from rx_rb.
 * @return Number of bytes actually read.
 */
static uint32_t usart1_read(Anbo_Device *dev, uint8_t *buf, uint32_t len)
{
    if ((buf == NULL) || (len == 0u)) {
        return 0u;
    }
    return Anbo_RB_Read(dev->rx_rb, buf, len);
}

/* ================================================================== */
/*  Interrupt handler — called from USART1_IRQHandler                  */
/* ================================================================== */

void BSP_USART1_IRQ(void)
{
    UART_HandleTypeDef *h = (UART_HandleTypeDef *)s_usart1_dev.priv;
    USART_TypeDef *uart;

    if (h == NULL) {
        return;
    }
    uart = h->Instance;

    /* ---- RX: drain entire FIFO ---- */
    {
        uint32_t rx_count = 0u;
        while ((uart->ISR & USART_ISR_RXNE) != 0u) {
            uint8_t byte = (uint8_t)(uart->RDR & 0xFFu);
            Anbo_RB_PutByte(s_usart1_dev.rx_rb, byte);
            rx_count++;
        }
        if (rx_count > 0u) {
            Anbo_EBus_PublishSig(ANBO_SIG_UART_RX, (void *)&s_usart1_dev);
        }
    }

    /* ---- TX: TXE flag ---- */
    if ((uart->CR1 & USART_CR1_TXEIE) != 0u &&
        (uart->ISR & USART_ISR_TXE)   != 0u) {
        uint8_t byte;
        if (Anbo_RB_GetByte(s_usart1_dev.tx_rb, &byte) == 0) {
            uart->TDR = (uint32_t)byte;     /* clears TXE */
        } else {
            /* tx_rb empty — disable TXE interrupt */
            uart->CR1 &= ~USART_CR1_TXEIE;

            /* Notify: TX batch complete.
             *
             * MUST use PublishSigSilent (not PublishSig) because this
             * UART is the log output path.  Normal Publish would invoke
             * ANBO_TRACE_PUBLISH(), which writes a trace line into the
             * log ring buffer, which triggers more UART TX, which
             * re-enters this ISR on completion — infinite feedback loop:
             *   ISR → Publish → Trace → Log → UART TX → ISR → ...
             * Silent publish delivers the signal to subscribers without
             * generating any trace output, breaking the cycle. */
            if (s_usart1_dev.sig_tx != 0u) {
                Anbo_EBus_PublishSigSilent(s_usart1_dev.sig_tx,
                                           (void *)&s_usart1_dev);
            }
        }
    }

    /* ---- Error flags: ORE / FE / NE / PE — clear by reading ICR ---- */
    if ((uart->ISR & (USART_ISR_ORE | USART_ISR_FE |
                      USART_ISR_NE  | USART_ISR_PE)) != 0u) {
        uart->ICR = USART_ICR_ORECF | USART_ICR_FECF |
                    USART_ICR_NECF  | USART_ICR_PECF;
    }

    /* ---- WUF: Wakeup from Stop flag — clear to prevent ISR re-entry ---- */
    if ((uart->ISR & USART_ISR_WUF) != 0u) {
        uart->ICR = USART_ICR_WUCF;
    }
}
