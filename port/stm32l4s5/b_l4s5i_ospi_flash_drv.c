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
 * @file  b_l4s5i_ospi_flash_drv.c
 * @brief B-L4S5I-IOT01A — External NOR Flash NVM logic layer
 *
 * NVM area : configurable sectors on external OSPI NOR Flash
 * Algorithm: log-structured append + multi-sector rotation
 *
 * This file contains ONLY the NVM record management logic.
 * All OSPI bus / Flash chip operations are delegated to
 * b_l4s5i_ospi_drv.c via the BSP_OSPI_* API.
 */

#include "app_config.h"

#if APP_CONF_PARAM_FLASH && (APP_CONF_PARAM_FLASH_USE_EXT == 1)

#include "b_l4s5i_ospi_flash_drv.h"
#include "b_l4s5i_ospi_drv.h"

/* ================================================================== */
/*  NVM area geometry                                                  */
/* ================================================================== */

#define NVM_SECTOR_ADDR      APP_CONF_PARAM_FLASH_EXT_ADDR
#define NVM_SECTOR_SIZE      APP_CONF_PARAM_FLASH_EXT_SIZE

/* ================================================================== */
/*  Init — delegates to OSPI bus driver                                */
/* ================================================================== */

bool BSP_OSPI_Flash_Init(void)
{
    return BSP_OSPI_Init();
}

/* ================================================================== */
/*  Slot helpers                                                       */
/* ================================================================== */

static bool slot_is_empty(uint32_t addr)
{
    uint8_t hdr[8];
    if (!BSP_OSPI_Read(addr, hdr, 8u)) {
        return true;    /* read error — treat as empty */
    }
    for (int i = 0; i < 8; i++) {
        if (hdr[i] != 0xFFu) { return false; }
    }
    return true;
}

/* ================================================================== */
/*  Multi-sector rotation helpers                                      */
/* ================================================================== */

#define NVM_NUM_SECTORS  APP_CONF_PARAM_FLASH_EXT_PAGES

/** Return the base address of logical sector @p s (0 = highest, grows down). */
static uint32_t sector_addr(uint32_t s)
{
    return NVM_SECTOR_ADDR - s * NVM_SECTOR_SIZE;
}

/** Return true when the first slot of sector @p s is erased. */
static bool sector_is_empty(uint32_t s, uint32_t rec_size)
{
    return slot_is_empty(sector_addr(s));
}

/**
 * Find the active sector — last sector (in rotation order) that
 * contains at least one written slot.  Returns -1 if all empty.
 */
static int find_active_sector(uint32_t rec_size)
{
    int active = -1;
    for (uint32_t s = 0u; s < NVM_NUM_SECTORS; s++) {
        if (!sector_is_empty(s, rec_size)) {
            active = (int)s;
        }
    }
    return active;
}

/* ================================================================== */
/*  Public NVM API  (same signature as internal_flash driver)          */
/* ================================================================== */

bool BSP_Flash_ReadLatest(void *buf, uint32_t size)
{
    int active = find_active_sector(size);
    if (active < 0) {
        return false;
    }

    /* Backward scan within the active sector */
    uint32_t base = sector_addr((uint32_t)active);
    uint32_t max_slots = NVM_SECTOR_SIZE / size;

    for (int i = (int)max_slots - 1; i >= 0; i--) {
        uint32_t addr = base + (uint32_t)i * size;
        if (!slot_is_empty(addr)) {
            return BSP_OSPI_Read(addr, (uint8_t *)buf, size);
        }
    }
    return false;
}

bool BSP_Flash_ReadValidated(void *buf, uint32_t size,
                             bool (*validator)(const void *rec, uint32_t len))
{
    int active = find_active_sector(size);
    if (active < 0) {
        return false;
    }

    uint32_t max_slots = NVM_SECTOR_SIZE / size;

    /* Scan sectors starting from active, going backward in rotation order */
    for (uint32_t s = 0u; s < NVM_NUM_SECTORS; s++) {
        uint32_t sec = ((uint32_t)active + NVM_NUM_SECTORS - s) % NVM_NUM_SECTORS;

        if ((s > 0u) && sector_is_empty(sec, size)) {
            continue;
        }

        uint32_t base = sector_addr(sec);

        /* Backward scan within this sector */
        for (int i = (int)max_slots - 1; i >= 0; i--) {
            uint32_t addr = base + (uint32_t)i * size;
            if (!slot_is_empty(addr)) {
                if (BSP_OSPI_Read(addr, (uint8_t *)buf, size)) {
                    if ((validator == NULL) || validator(buf, size)) {
                        return true;
                    }
                }
                /* Record failed validation — try previous slot */
            }
        }
    }

    return false;
}

bool BSP_Flash_WriteAppend(const void *buf, uint32_t size)
{
    uint32_t max_slots = NVM_SECTOR_SIZE / size;
    int active = find_active_sector(size);
    uint32_t sec;
    int target = -1;

    if (active < 0) {
        /* All sectors empty — use sector 0, slot 0 */
        sec = 0u;
        target = 0;
    } else {
        sec = (uint32_t)active;
        uint32_t base = sector_addr(sec);

        /* Find first empty slot in active sector */
        for (uint32_t i = 0u; i < max_slots; i++) {
            uint32_t addr = base + i * size;
            if (slot_is_empty(addr)) {
                target = (int)i;
                break;
            }
        }

        /* Active sector full — rotate to next sector */
        if (target < 0) {
            sec = (sec + 1u) % NVM_NUM_SECTORS;

            if (!sector_is_empty(sec, size)) {
                if (!BSP_OSPI_SectorErase(sector_addr(sec))) {
                    return false;
                }
            }
            target = 0;
        }
    }

    /* Write data (may span multiple 256-byte NOR pages) */
    {
        uint32_t addr = sector_addr(sec) + (uint32_t)target * size;
        const uint8_t *src = (const uint8_t *)buf;
        uint32_t remaining = size;

        while (remaining > 0u) {
            uint32_t page_off = addr & (BSP_OSPI_PAGE_SIZE - 1u);
            uint32_t chunk = BSP_OSPI_PAGE_SIZE - page_off;
            if (chunk > remaining) { chunk = remaining; }

            if (!BSP_OSPI_PageProgram(addr, src, chunk)) {
                return false;
            }
            addr      += chunk;
            src       += chunk;
            remaining -= chunk;
        }
    }
    return true;
}

#else
typedef int b_l4s5i_ospi_flash_drv_empty_tu;
#endif /* APP_CONF_PARAM_FLASH && BACKEND==1 */
