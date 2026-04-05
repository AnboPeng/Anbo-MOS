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
 * @file  anbo_dev.h
 * @brief Anbo Kernel — Asynchronous Device Abstraction
 *
 * Design highlights:
 *   - Each peripheral is described by Anbo_Device, with a unified ops vtable.
 *   - Write operations are non-blocking async: Async_Write submits data into
 *     the driver's internal buffer; the actual transfer is completed in the
 *     background by interrupt / DMA, triggering tx_done callback on completion.
 *   - Read operations are also non-blocking: data is pushed into the rx ring
 *     buffer during ISR; the upper layer retrieves it via Read.
 *   - Bottom-half template: ISR calls Anbo_Dev_ISR_Post() to publish a signal
 *     on the event bus, waking the bound FSM state machine.
 *   - Fully static memory, zero hardware dependency (hardware interaction is
 *     only through ops callbacks).
 *
 * Constraints: C99 / fully static memory / no malloc / no sprintf
 */

#ifndef ANBO_DEV_H
#define ANBO_DEV_H

#include <stdint.h>
#include <stddef.h>
#include "anbo_config.h"
#include "anbo_rb.h"
#include "anbo_ebus.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================== */
/*  Forward Declarations                                               */
/* ================================================================== */

typedef struct Anbo_Device Anbo_Device;

/* ================================================================== */
/*  Callbacks & Operation Function Table                               */
/* ================================================================== */

/**
 * @typedef Anbo_Dev_TxDone
 * @brief   Transmit-complete callback (called by driver after DMA / interrupt completion).
 * @param   dev     Device instance.
 * @param   nbytes  Number of bytes actually transmitted this time.
 */
typedef void (*Anbo_Dev_TxDone)(Anbo_Device *dev, uint32_t nbytes);

/**
 * @typedef Anbo_Dev_RxReady
 * @brief   Receive-data-ready callback (called in ISR when new data enters rx buffer).
 * @param   dev     Device instance.
 * @param   nbytes  Number of newly arrived bytes this time.
 */
typedef void (*Anbo_Dev_RxReady)(Anbo_Device *dev, uint32_t nbytes);

/**
 * @struct Anbo_DevOps
 * @brief  Device operation vtable — implemented by each platform driver.
 *
 * All function pointers may be NULL (device does not support that operation).
 */
typedef struct {
    /**
     * @brief Initialize hardware peripheral (GPIO, clocks, DMA channels, etc.).
     * @retval 0  Success.
     */
    int  (*open)(Anbo_Device *dev);

    /**
     * @brief Close / de-initialize the hardware peripheral.
     */
    void (*close)(Anbo_Device *dev);

    /**
     * @brief Non-blocking async write.
     * @param data  Data to send (caller ensures validity until tx_done,
     *              or the driver internally copies into the tx ring buffer).
     * @param len   Byte count.
     * @return Number of bytes actually accepted (may be less than len,
     *         depending on remaining tx buffer space).
     *
     * Driver must call dev->tx_done after background (interrupt / DMA) transfer completes.
     */
    uint32_t (*write)(Anbo_Device *dev, const uint8_t *data, uint32_t len);

    /**
     * @brief Non-blocking read.
     * @param buf   Receive buffer.
     * @param len   Maximum bytes to read.
     * @return Number of bytes actually read.
     *
     * Retrieves data from dev->rx_rb ring buffer that the ISR has stored.
     */
    uint32_t (*read)(Anbo_Device *dev, uint8_t *buf, uint32_t len);

    /**
     * @brief Device control (ioctl semantics).
     * @param cmd   Control command code.
     * @param arg   Command argument.
     * @retval 0    Success.
     */
    int  (*ioctl)(Anbo_Device *dev, uint32_t cmd, void *arg);

} Anbo_DevOps;

/* ================================================================== */
/*  Device Instance                                                    */
/* ================================================================== */

/**
 * @struct Anbo_Device
 * @brief  Unified device descriptor (statically allocated by user).
 */
struct Anbo_Device {
    const char          *name;      /**< Device name (for debugging) */
    const Anbo_DevOps   *ops;       /**< Operation function table (provided by driver) */

    /* ---- Async buffers ---- */
    Anbo_RB             *tx_rb;     /**< Transmit ring buffer (may be NULL) */
    Anbo_RB             *rx_rb;     /**< Receive ring buffer (may be NULL) */

    /* ---- Callbacks ---- */
    Anbo_Dev_TxDone      tx_done;   /**< Transmit-complete callback */
    Anbo_Dev_RxReady     rx_ready;  /**< Receive-ready callback */

    /* ---- Event bus integration ---- */
    uint16_t             sig_tx;    /**< Signal published on event bus when tx completes (0 = disabled) */
    uint16_t             sig_rx;    /**< Signal published on event bus when rx is ready (0 = disabled) */

    /* ---- Driver private ---- */
    void                *priv;      /**< Driver platform private data (e.g., DMA handle) */
    uint8_t              flags;     /**< Device status flags */
};

/** Device status flag bits */
#define ANBO_DEV_FLAG_OPENED    (1u << 0)   /**< Opened */
#define ANBO_DEV_FLAG_TX_BUSY   (1u << 1)   /**< Transmitting */

/* ================================================================== */
/*  Generic Device API (driver-independent)                            */
/* ================================================================== */

/**
 * @brief Open a device.
 * @retval 0  Success; -1 failure (ops->open not implemented or returned error).
 */
static inline int Anbo_Dev_Open(Anbo_Device *dev)
{
    if ((dev == NULL) || (dev->ops == NULL) || (dev->ops->open == NULL)) {
        return -1;
    }
    if (dev->flags & ANBO_DEV_FLAG_OPENED) {
        return 0;   /* Already opened */
    }
    if (dev->ops->open(dev) == 0) {
        dev->flags |= ANBO_DEV_FLAG_OPENED;
        return 0;
    }
    return -1;
}

/**
 * @brief Close a device.
 */
static inline void Anbo_Dev_Close(Anbo_Device *dev)
{
    if ((dev == NULL) || (dev->ops == NULL)) {
        return;
    }
    if (dev->ops->close != NULL) {
        dev->ops->close(dev);
    }
    dev->flags = 0u;
}

/**
 * @brief Non-blocking async write.
 * @return Number of bytes actually submitted.
 */
static inline uint32_t Anbo_Dev_AsyncWrite(Anbo_Device *dev,
                                            const uint8_t *data,
                                            uint32_t len)
{
    if ((dev == NULL) || (dev->ops == NULL) || (dev->ops->write == NULL)) {
        return 0u;
    }
    return dev->ops->write(dev, data, len);
}

/**
 * @brief Non-blocking read.
 * @return Number of bytes actually read.
 */
static inline uint32_t Anbo_Dev_Read(Anbo_Device *dev,
                                      uint8_t *buf,
                                      uint32_t len)
{
    if ((dev == NULL) || (dev->ops == NULL) || (dev->ops->read == NULL)) {
        return 0u;
    }
    return dev->ops->read(dev, buf, len);
}

/**
 * @brief Device control.
 */
static inline int Anbo_Dev_Ioctl(Anbo_Device *dev, uint32_t cmd, void *arg)
{
    if ((dev == NULL) || (dev->ops == NULL) || (dev->ops->ioctl == NULL)) {
        return -1;
    }
    return dev->ops->ioctl(dev, cmd, arg);
}

/* ================================================================== */
/*  Bottom-half template: ISR -> Event Bus -> FSM                      */
/* ================================================================== */

/**
 * @brief  ISR bottom-half — publish a device signal on the event bus.
 *
 * Called within an interrupt service routine to "post" an event to the
 * main-loop FSM.
 *
 * @param sig    Signal identifier (e.g., device's sig_rx / sig_tx).
 * @param param  Additional parameter (e.g., Anbo_Device* itself, or NULL).
 *
 * @note  This function is called in ISR context; callback chains should be
 *        as short as possible. Event bus Publish internally enters/exits
 *        a critical section to protect the linked list.
 *
 * Typical usage:
 * @code
 * // --- UART RX interrupt handler ---
 * void USART2_IRQHandler(void)
 * {
 *     uint8_t byte = USART2->RDR;
 *     Anbo_RB_PutByte(uart_dev.rx_rb, byte);
 *     Anbo_Dev_ISR_Post(uart_dev.sig_rx, &uart_dev);
 * }
 *
 * // --- EXTI button interrupt handler ---
 * void EXTI15_10_IRQHandler(void)
 * {
 *     // Clear interrupt flag ...
 *     Anbo_Dev_ISR_Post(SIG_BTN_PRESSED, NULL);
 * }
 * @endcode
 *
 * The corresponding FSM handles it in on_event:
 * @code
 * static void idle_on_event(Anbo_FSM *fsm, const Anbo_Event *evt)
 * {
 *     switch (evt->sig) {
 *     case SIG_BTN_PRESSED:
 *         Anbo_FSM_Transfer(fsm, &state_active);
 *         break;
 *     case SIG_UART_RX:
 *         // Read data from rx_rb ...
 *         break;
 *     }
 * }
 * @endcode
 */
static inline void Anbo_Dev_ISR_Post(uint16_t sig, void *param)
{
    if (sig != 0u) {
        Anbo_EBus_PublishSig(sig, param);
    }
}

/**
 * @brief  Transmit-complete bottom-half — called by driver after DMA/interrupt finishes.
 *
 * Clears TX_BUSY flag -> calls tx_done callback -> publishes sig_tx signal.
 *
 * @param dev     Device instance.
 * @param nbytes  Number of bytes transmitted this time.
 */
static inline void Anbo_Dev_TxComplete(Anbo_Device *dev, uint32_t nbytes)
{
    if (dev == NULL) {
        return;
    }
    dev->flags &= (uint8_t)~ANBO_DEV_FLAG_TX_BUSY;

    if (dev->tx_done != NULL) {
        dev->tx_done(dev, nbytes);
    }
    if (dev->sig_tx != 0u) {
        Anbo_EBus_PublishSig(dev->sig_tx, dev);
    }
}

/**
 * @brief  Receive-ready bottom-half — called by driver in ISR after data enters rx_rb.
 *
 * Calls rx_ready callback -> publishes sig_rx signal.
 *
 * @param dev     Device instance.
 * @param nbytes  Number of bytes received this time.
 */
static inline void Anbo_Dev_RxNotify(Anbo_Device *dev, uint32_t nbytes)
{
    if (dev == NULL) {
        return;
    }
    if (dev->rx_ready != NULL) {
        dev->rx_ready(dev, nbytes);
    }
    if (dev->sig_rx != 0u) {
        Anbo_EBus_PublishSig(dev->sig_rx, dev);
    }
}

#ifdef __cplusplus
}
#endif

#endif /* ANBO_DEV_H */
