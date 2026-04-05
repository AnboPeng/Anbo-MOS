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
 * @file  b_l4s5i_log_flash.h
 * @brief B-L4S5I — Persistent Flash log driver (circular ring)
 *
 * Stores log lines in a dedicated Flash region, separate from
 * parameter NVM storage.  Uses a simple circular ring of pages:
 * when the current page is full the next page is erased and
 * writing continues at its start.
 *
 * Flash layout (configurable via APP_CONF_LOG_FLASH_* macros):
 *   Page 0 = LOG_FLASH_ADDR,  Page 1 = ADDR - PAGE_SIZE, ...
 *
 * Fixed-length slot format (128 bytes = ANBO_CONF_LOG_LINE_MAX):
 *   Byte [0..3]   : uint32_t seq  — monotonic sequence number
 *                    (0xFFFFFFFF = empty / erased)
 *   Byte [4..123] : char text[120] — log string, NUL-padded
 *   Byte [124..127]: uint32_t crc32 — CRC32 over bytes [0..124)
 *
 * Power-fail safety: corrupt slots (CRC mismatch) are automatically
 * skipped during Init (cursor recovery) and Read.
 *
 * The sequence number determines ordering:
 *   head (oldest) = slot with smallest seq
 *   tail (write)  = slot after largest seq
 *
 * With 128-byte slots and 4 KB pages: 32 slots/page, 128 entries total (4 pages).
 *
 * API:
 *   BSP_LogFlash_Init()   — scan Flash, recover head/tail cursors
 *   BSP_LogFlash_Write()  — append a log line (Anbo_Log callback)
 *   BSP_LogFlash_Read()   — sequential readback from oldest to newest
 *   BSP_LogFlash_GetSeq() — current sequence counter
 *   BSP_LogFlash_Erase()  — erase all log pages
 */

#ifndef B_L4S5I_LOG_FLASH_H
#define B_L4S5I_LOG_FLASH_H

#include "app_config.h"

#if APP_CONF_LOG_FLASH

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the Flash log driver.
 *
 * Scans Flash to locate the current write position (first erased slot
 * in the active page).  Must be called once at boot, after BSP_Init().
 */
void BSP_LogFlash_Init(void);

/**
 * @brief Append a log line to Flash.
 *
 * Designed to be used as the Anbo_Log_FlashWrite_t callback.
 * Each entry is stored as: [uint16_t len][data][pad to 8-byte align].
 *
 * @param data  Log line bytes (not NUL-terminated requirement).
 * @param len   Byte count.
 * @return Number of bytes accepted (0 if Flash error or line too long).
 */
uint32_t BSP_LogFlash_Write(const uint8_t *data, uint32_t len);

/**
 * @brief Read log entries sequentially for dump / export.
 *
 * Call repeatedly; returns one entry per call.  When no more entries
 * remain, returns 0.
 *
 * @param buf       Destination buffer.
 * @param buf_size  Buffer capacity.
 * @param ctx       Opaque iteration context.  Caller must zero-init
 *                  before the first call.
 * @return Number of bytes copied to buf (0 = end of log).
 */
uint32_t BSP_LogFlash_Read(uint8_t *buf, uint32_t buf_size, uint32_t *ctx);

/**
 * @brief Erase all log pages (factory reset of log area).
 * @return true on success.
 */
bool BSP_LogFlash_Erase(void);

/**
 * @brief Dump all stored log entries to UART.
 *
 * Reads every valid entry from head (oldest) to tail (newest) and
 * outputs each line prefixed with "[R] " via Anbo_Log_WriteRaw
 * (UART ring buffer only — does NOT re-write to Flash).
 *
 * Blocking: busy-waits on Anbo_Log_Flush() between entries to
 * prevent UART ring buffer overflow.  Safe to call from the
 * main loop but will stall other processing until complete.
 */
void BSP_LogFlash_Dump(void);

/**
 * @brief Return the current sequence counter (next seq to be written).
 *
 * Useful for diagnostics — shows total log entries written since
 * last full erase.
 */
uint32_t BSP_LogFlash_GetSeq(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_CONF_LOG_FLASH */
#endif /* B_L4S5I_LOG_FLASH_H */
