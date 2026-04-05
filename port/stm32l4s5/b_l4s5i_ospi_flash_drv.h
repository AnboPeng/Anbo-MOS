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
 * @file  b_l4s5i_ospi_flash_drv.h
 * @brief B-L4S5I-IOT01A — External NOR Flash NVM logic layer
 *
 * Log-structured append on one or more 4 KB sectors of external OSPI
 * NOR Flash. Multi-sector rotation: when active sector is full,
 * rotate to next.
 *
 * This layer is Flash-chip agnostic.  All bus / chip operations are
 * delegated to b_l4s5i_ospi_drv.h (BSP_OSPI_* API).  To support a
 * different NOR Flash chip, replace that driver only.
 *
 * Slot size   : caller-defined (must be 8-byte aligned)
 * Endurance   : ≥ 100 000 erase cycles × (4096 / slot_size) × N_PAGES writes
 */

#ifndef B_L4S5I_OSPI_FLASH_DRV_H
#define B_L4S5I_OSPI_FLASH_DRV_H

#include "app_config.h"

#if APP_CONF_PARAM_FLASH && (APP_CONF_PARAM_FLASH_USE_EXT == 1)

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise OCTOSPI1 peripheral and MX25R6435F communication.
 *
 * Configures GPIO (PE10-PE15), OCTOSPI1 clock, and verifies the
 * JEDEC ID (C2 28 17) of the MX25R6435F.  Must be called before
 * any Flash read/write operation.
 *
 * @return true  if OSPI init and JEDEC ID check passed.
 * @return false on init or ID mismatch error.
 */
bool BSP_OSPI_Flash_Init(void);

/**
 * @brief Read the most recent valid record from the NVM sector.
 * @param buf   Destination buffer (>= @p size bytes).
 * @param size  Record size in bytes (8-byte aligned, <= 4096).
 * @return true  if a non-empty record was copied, false if sector empty.
 */
bool BSP_Flash_ReadLatest(void *buf, uint32_t size);

/**
 * @brief Append-write a record to the next free slot in the NVM sector.
 * @param buf   Source data (>= @p size bytes).
 * @param size  Record size in bytes (8-byte aligned, <= 4096).
 * @return true  on success, false on write/erase error.
 */
bool BSP_Flash_WriteAppend(const void *buf, uint32_t size);

/**
 * @brief Read the most recent record that passes a caller-supplied validator.
 *
 * Provides power-fail safety: scans backward through all slots in all
 * NVM sectors.  For each non-empty slot the data is copied to @p buf and
 * @p validator is called.  Returns true on the first record that passes.
 *
 * @param buf       Destination buffer (must be >= @p size bytes).
 * @param size      Record size in bytes (must be 8-byte aligned).
 * @param validator Callback: return true if the record is valid.
 *                  If NULL, behaves like BSP_Flash_ReadLatest().
 * @return true  if a valid record was found, false if none exist.
 */
bool BSP_Flash_ReadValidated(void *buf, uint32_t size,
                             bool (*validator)(const void *rec, uint32_t len));

#ifdef __cplusplus
}
#endif

#endif /* APP_CONF_PARAM_FLASH && BACKEND==1 */
#endif /* B_L4S5I_OSPI_FLASH_DRV_H */
