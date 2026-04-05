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
 * @file  anbo_log.h
 * @brief Anbo Kernel — Asynchronous Lightweight Logging System
 *
 * Design highlights:
 *   - Zero stdio dependency: hand-rolled %s / %d / %u / %x / %c formatting.
 *   - Async non-blocking: formatted strings are pushed into an Anbo_RB ring buffer;
 *     a bound Anbo_Device sends them in the background (interrupt / DMA).
 *   - Main loop calls Anbo_Log_Flush() to trigger background transmission.
 *   - Log-level filtering (ERROR / WARN / INFO / DEBUG).
 *   - Fully static memory; log levels can be trimmed at compile time.
 *
 * Constraints: C99 / fully static memory / no malloc / no sprintf
 */

#ifndef ANBO_LOG_H
#define ANBO_LOG_H

#include <stdint.h>
#include "anbo_config.h"
#include "anbo_rb.h"
#include "anbo_dev.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================== */
/*  Configuration                                                      */
/* ================================================================== */

/** Log ring buffer order (capacity = 2^order bytes) */
#ifndef ANBO_CONF_LOG_RB_ORDER
#define ANBO_CONF_LOG_RB_ORDER      10      /* 1024 bytes */
#endif

/** Compile-time maximum log level (levels below this are not compiled) */
#ifndef ANBO_CONF_LOG_LEVEL
#define ANBO_CONF_LOG_LEVEL         3       /* Default: DEBUG */
#endif

/** Maximum length of a single log line (stack-local temporary buffer) */
#ifndef ANBO_CONF_LOG_LINE_MAX
#define ANBO_CONF_LOG_LINE_MAX      128
#endif

/* ================================================================== */
/*  Log Sink Bitmask                                                   */
/* ================================================================== */

/** @brief Output to UART device (default, existing behaviour). */
#define ANBO_LOG_SINK_UART          (1u << 0)
/** @brief Output to Flash log area (persistent, survives reboot). */
#define ANBO_LOG_SINK_FLASH         (1u << 1)

/** @brief Default sink mask — UART only. Override per-level or globally. */
#ifndef ANBO_CONF_LOG_SINK_DEFAULT
#define ANBO_CONF_LOG_SINK_DEFAULT  ANBO_LOG_SINK_UART
#endif

/* ================================================================== */
/*  Log Levels                                                         */
/* ================================================================== */

#define ANBO_LOG_LVL_ERROR      0
#define ANBO_LOG_LVL_WARN       1
#define ANBO_LOG_LVL_INFO       2
#define ANBO_LOG_LVL_DEBUG      3

/* ================================================================== */
/*  API                                                                */
/* ================================================================== */

/**
 * @brief Initialize the logging system.
 * @param dev  Log output device (e.g., UART device). May be NULL (buffer only, no output).
 *
 * Internally initializes the static ring buffer.
 */
void Anbo_Log_Init(Anbo_Device *dev);

/**
 * @brief Bind / replace the log output device.
 */
void Anbo_Log_SetDevice(Anbo_Device *dev);

/**
 * @brief Set the output sink mask for a specific log level.
 * @param level  Log level (ANBO_LOG_LVL_ERROR .. ANBO_LOG_LVL_DEBUG).
 * @param mask   Bitmask of ANBO_LOG_SINK_UART / ANBO_LOG_SINK_FLASH.
 *
 * Example: route ERROR+WARN to UART+Flash, INFO+DEBUG to UART only:
 *   Anbo_Log_SetSink(ANBO_LOG_LVL_ERROR, ANBO_LOG_SINK_UART | ANBO_LOG_SINK_FLASH);
 *   Anbo_Log_SetSink(ANBO_LOG_LVL_WARN,  ANBO_LOG_SINK_UART | ANBO_LOG_SINK_FLASH);
 *   Anbo_Log_SetSink(ANBO_LOG_LVL_INFO,  ANBO_LOG_SINK_UART);
 *   Anbo_Log_SetSink(ANBO_LOG_LVL_DEBUG, ANBO_LOG_SINK_UART);
 */
void Anbo_Log_SetSink(uint8_t level, uint8_t mask);

/**
 * @brief Flash write callback type used by the log system.
 *
 * The log module does not know about Flash drivers directly.
 * The application registers a callback that accepts a formatted log
 * line (NUL-terminated) and persists it to Flash however it sees fit.
 *
 * @param data  Formatted log line bytes.
 * @param len   Byte count (excluding NUL terminator).
 * @return Number of bytes actually written.
 */
typedef uint32_t (*Anbo_Log_FlashWrite_t)(const uint8_t *data, uint32_t len);

/**
 * @brief Register the Flash-write callback.
 * @param fn  Callback. NULL disables Flash sink (even if mask includes it).
 */
void Anbo_Log_SetFlashWriter(Anbo_Log_FlashWrite_t fn);

/**
 * @brief Core formatted output.
 * @param level  Log level.
 * @param fmt    Format string (supports %s %d %u %x %c %%).
 * @param ...    Variadic arguments.
 *
 * Formatted result is appended to the internal ring buffer; non-blocking.
 * If the buffer is full, new logs are discarded (oldest data is preserved).
 */
void Anbo_Log_Printf(uint8_t level, const char *fmt, ...);

/**
 * @brief Send pending data from the ring buffer via the device backend.
 *
 * Call periodically from the main loop. Non-blocking — only triggers
 * a single Async_Write; subsequent transmission is completed by
 * interrupt / DMA and re-invoked.
 */
void Anbo_Log_Flush(void);

/**
 * @brief Blocking drain — flush ALL pending log data until transmitted.
 *
 * Repeatedly calls Flush() and waits for the UART ISR to drain the
 * device's tx_rb.  Returns only when both the log ring buffer and
 * the device's transmit buffer are empty (or a guard timeout fires).
 *
 * Typical uses:
 *   - Boot: ensure banner and module-init logs are fully printed
 *     before subsequent output overwrites the ring buffer.
 *   - Pre-sleep: guarantee logs are visible before entering Stop 2.
 *   - Pre-fault: ensure diagnostic output before halt/reset.
 *
 * @note Blocks the caller; interrupts must remain enabled.
 */
void Anbo_Log_DrainAll(void);

/**
 * @brief Write raw bytes directly to the log buffer (no formatting).
 * @param data  Data pointer.
 * @param len   Byte count.
 * @return Number of bytes actually written.
 */
uint32_t Anbo_Log_WriteRaw(const uint8_t *data, uint32_t len);

/**
 * @brief Query the number of bytes pending in the log buffer.
 */
uint32_t Anbo_Log_Pending(void);

/* ================================================================== */
/*  Minimal Formatting Utilities (public, reusable by other modules)   */
/* ================================================================== */

/**
 * @brief Minimal vformat: write formatted result into buf.
 * @param buf   Output buffer.
 * @param size  Buffer size.
 * @param fmt   Format string (%s %d %u %x %c %%).
 * @param ap    va_list argument list.
 * @return Number of bytes written (excluding '\0').
 *
 * Does not depend on stdio.h.
 */
#include <stdarg.h>
uint32_t Anbo_Format_V(char *buf, uint32_t size, const char *fmt, va_list ap);

/**
 * @brief Minimal format (non-variadic wrapper).
 */
uint32_t Anbo_Format(char *buf, uint32_t size, const char *fmt, ...);

/* ================================================================== */
/*  Convenience Macros (compile-time level filtering)                  */
/* ================================================================== */

#if ANBO_CONF_LOG_LEVEL >= ANBO_LOG_LVL_ERROR
#define ANBO_LOGE(fmt, ...) \
    Anbo_Log_Printf(ANBO_LOG_LVL_ERROR, "[E] " fmt "\r\n", ##__VA_ARGS__)
#else
#define ANBO_LOGE(fmt, ...) ((void)0)
#endif

#if ANBO_CONF_LOG_LEVEL >= ANBO_LOG_LVL_WARN
#define ANBO_LOGW(fmt, ...) \
    Anbo_Log_Printf(ANBO_LOG_LVL_WARN,  "[W] " fmt "\r\n", ##__VA_ARGS__)
#else
#define ANBO_LOGW(fmt, ...) ((void)0)
#endif

#if ANBO_CONF_LOG_LEVEL >= ANBO_LOG_LVL_INFO
#define ANBO_LOGI(fmt, ...) \
    Anbo_Log_Printf(ANBO_LOG_LVL_INFO,  "[I] " fmt "\r\n", ##__VA_ARGS__)
#else
#define ANBO_LOGI(fmt, ...) ((void)0)
#endif

#if ANBO_CONF_LOG_LEVEL >= ANBO_LOG_LVL_DEBUG
#define ANBO_LOGD(fmt, ...) \
    Anbo_Log_Printf(ANBO_LOG_LVL_DEBUG, "[D] " fmt "\r\n", ##__VA_ARGS__)
#else
#define ANBO_LOGD(fmt, ...) ((void)0)
#endif

/* ================================================================== */
/*  Kernel Trace Macros (compile-time controlled by ANBO_CONF_TRACE)  */
/* ================================================================== */

#if ANBO_CONF_TRACE

/**
 * @def   ANBO_TRACE_PUBLISH(sig)
 * @brief Log when an event is published on the EBus.
 */
#define ANBO_TRACE_PUBLISH(sig) \
    Anbo_Log_Printf(ANBO_LOG_LVL_DEBUG, \
        "[T] PUB sig=0x%04x t=%u\r\n", (unsigned)(sig), Anbo_Arch_GetTick())

/**
 * @def   ANBO_TRACE_FSM_TRANSFER(fsm_name, old_name, new_name)
 * @brief Log when an FSM transitions between states.
 */
#define ANBO_TRACE_FSM_TRANSFER(fsm_name, old_name, new_name) \
    Anbo_Log_Printf(ANBO_LOG_LVL_DEBUG, \
        "[T] FSM %s: %s -> %s t=%u\r\n", \
        (fsm_name), (old_name), (new_name), Anbo_Arch_GetTick())

/**
 * @def   ANBO_TRACE_FSM_DISPATCH(fsm_name, sig)
 * @brief Log when an event is dispatched to an FSM.
 */
#define ANBO_TRACE_FSM_DISPATCH(fsm_name, sig) \
    Anbo_Log_Printf(ANBO_LOG_LVL_DEBUG, \
        "[T] DSP %s sig=0x%04x t=%u\r\n", \
        (fsm_name), (unsigned)(sig), Anbo_Arch_GetTick())

#else /* ANBO_CONF_TRACE == 0 */

#define ANBO_TRACE_PUBLISH(sig)                              ((void)0)
#define ANBO_TRACE_FSM_TRANSFER(fsm_name, old_name, new_name) ((void)0)
#define ANBO_TRACE_FSM_DISPATCH(fsm_name, sig)               ((void)0)

#endif /* ANBO_CONF_TRACE */

#ifdef __cplusplus
}
#endif

#endif /* ANBO_LOG_H */
