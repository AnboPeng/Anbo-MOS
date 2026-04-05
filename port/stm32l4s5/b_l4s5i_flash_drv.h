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
 * @file  b_l4s5i_flash_drv.h
 * @brief B-L4S5I-IOT01A — Log-structured NVM driver (internal Flash)
 *
 * Uses one or more 4 KB pages of Bank 2 as a wear-levelled parameter
 * store.  Log-structured append within each page; when a page is full
 * the driver rotates to the next page (circular).  Pages are laid out
 * downward from INT_ADDR.
 *
 * STM32L4S5 Flash geometry (dual-bank mode):
 *   Page size  : 4096 bytes
 *   Programming: double-word (64-bit)
 *
 * Slot size is caller-defined but must be a multiple of 8.
 * With 32-byte config, 1 page:  4096 / 32 = 128 slots.
 * Endurance: 10 000 × 128 × N_PAGES writes minimum.
 */

#ifndef B_L4S5I_FLASH_DRV_H
#define B_L4S5I_FLASH_DRV_H

#include "app_config.h"

#if APP_CONF_PARAM_FLASH && (APP_CONF_PARAM_FLASH_USE_EXT == 0)

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Read the most recent valid record from the NVM page.
 *
 * Scans backward from the last slot; returns the first non-empty entry.
 * The caller is responsible for validating magic/CRC after the call.
 *
 * @param buf   Destination buffer (must be >= @p size bytes).
 * @param size  Record size in bytes (must be 8-byte aligned, > 0,
 *              and <= page size).
 * @return true  if a non-empty record was found and copied to @p buf.
 * @return false if the entire page is empty (virgin / just erased).
 */
bool BSP_Flash_ReadLatest(void *buf, uint32_t size);

/**
 * @brief Append-write a record to the next free slot in the NVM page.
 *
 * If no free slot is available the page is erased first, then the record
 * is written to slot 0.  All writes use 64-bit double-word programming.
 *
 * @param buf   Source data (must be >= @p size bytes, 8-byte aligned
 *              content recommended).
 * @param size  Record size in bytes (must be 8-byte aligned, > 0,
 *              and <= page size).
 * @return true  on success.
 * @return false on HAL erase/program error.
 */
bool BSP_Flash_WriteAppend(const void *buf, uint32_t size);

/**
 * @brief Read the most recent record that passes a caller-supplied validator.
 *
 * Provides power-fail safety: scans backward through all slots in all
 * NVM pages.  For each non-empty slot the data is copied to @p buf and
 * @p validator is called.  Returns true on the first record that passes.
 *
 * If the most recent slot was only partially written (e.g. power lost
 * mid-program), its CRC / magic will be wrong and the validator will
 * reject it.  The scan then continues to the previous intact record.
 *
 * @param buf       Destination buffer (must be >= @p size bytes).
 * @param size      Record size in bytes (must be 8-byte aligned).
 * @param validator Callback: return true if the record in @p buf is
 *                  valid.  If NULL, behaves like BSP_Flash_ReadLatest().
 * @return true  if a valid record was found and copied to @p buf.
 * @return false if no valid record exists in any page.
 */
bool BSP_Flash_ReadValidated(void *buf, uint32_t size,
                             bool (*validator)(const void *rec, uint32_t len));

#ifdef __cplusplus
}
#endif

#endif /* APP_CONF_PARAM_FLASH && BACKEND==0 */
#endif /* B_L4S5I_FLASH_DRV_H */
