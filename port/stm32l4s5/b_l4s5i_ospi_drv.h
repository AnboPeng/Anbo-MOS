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
 * @file  b_l4s5i_ospi_drv.h
 * @brief B-L4S5I-IOT01A — OCTOSPI1 bus + MX25R6435F low-level driver
 *
 * This layer owns:
 *   - OCTOSPI1 peripheral + GPIO init
 *   - MX25R6435F JEDEC ID verification
 *   - Primitive Flash operations: read / page-program / sector-erase
 *
 * To port to a different NOR Flash chip, replace this file pair only.
 *
 * Pin mapping (B-L4S5I-IOT01A schematics):
 *   OCTOSPI1_CLK : PE10  (AF10)
 *   OCTOSPI1_NCS : PE11  (AF10)
 *   OCTOSPI1_IO0 : PE12  (AF10)
 *   OCTOSPI1_IO1 : PE13  (AF10)
 *   OCTOSPI1_IO2 : PE14  (AF10)
 *   OCTOSPI1_IO3 : PE15  (AF10)
 */

#ifndef B_L4S5I_OSPI_DRV_H
#define B_L4S5I_OSPI_DRV_H

#include "app_config.h"

/* Build this driver when any subsystem needs the OSPI Flash bus:
 *   - Parameter NVM on external Flash, OR
 *   - Log storage on external Flash.
 */
#define BSP_OSPI_NEEDED  ((APP_CONF_PARAM_FLASH && (APP_CONF_PARAM_FLASH_USE_EXT == 1)) || \
                          (APP_CONF_LOG_FLASH && (APP_CONF_LOG_FLASH_USE_EXT == 1)))

#if BSP_OSPI_NEEDED

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise OCTOSPI1 peripheral and verify MX25R6435F JEDEC ID.
 * @return true  on success, false on init or ID mismatch error.
 */
bool BSP_OSPI_Init(void);

/**
 * @brief Read data from Flash.
 * @param addr  Absolute Flash address.
 * @param buf   Destination buffer.
 * @param len   Number of bytes to read.
 * @return true on success.
 */
bool BSP_OSPI_Read(uint32_t addr, uint8_t *buf, uint32_t len);

/**
 * @brief Program up to one NOR page (256 B) of data.
 *
 * Caller must ensure @p len does not cross a 256-byte page boundary.
 * Internally issues WREN + PP + wait-ready.
 *
 * @param addr  Absolute Flash address (page-aligned recommended).
 * @param data  Source data.
 * @param len   Bytes to program (1..256, must not cross page boundary).
 * @return true on success.
 */
bool BSP_OSPI_PageProgram(uint32_t addr, const uint8_t *data, uint32_t len);

/**
 * @brief Erase one 4 KB sector.
 * @param addr  Any address within the target sector.
 * @return true on success.
 */
bool BSP_OSPI_SectorErase(uint32_t addr);

/** NOR Flash page size (for page-program boundary calculations). */
#define BSP_OSPI_PAGE_SIZE   256u

#ifdef __cplusplus
}
#endif

#endif /* BSP_OSPI_NEEDED */
#endif /* B_L4S5I_OSPI_DRV_H */
