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
 * @file  b_l4s5i_flash_drv.c
 * @brief B-L4S5I-IOT01A — Log-structured NVM driver implementation
 *
 * Hardware parameters (STM32L4S5 dual-bank):
 *   NVM pages : Bank 2, configurable page count (default 1)
 *   Program   : double-word (64-bit)
 *   Endurance : ≥ 10 000 erase cycles per page
 *
 * Multi-page rotation algorithm:
 *   Pages are numbered 0..N-1.  Page 0 is at NVM_PAGE_ADDR, page 1
 *   is NVM_PAGE_ADDR - PAGE_SIZE, etc. (laid out downward).
 *
 *   Write — scan pages in order 0..N-1 to find the "active" page
 *           (has at least one written slot).  The active page is the
 *           last page that contains data.  Append to first empty slot
 *           in the active page.  If full, advance to next page
 *           (circular), erase it, and write to slot 0.
 *
 *   Read  — find the active page, then backward-scan its slots for
 *           the most recent record.
 *
 * An "empty" slot is detected by checking if its first 8 bytes == 0xFF..FF
 * (the erased state of Flash).
 */

#include "app_config.h"

#if APP_CONF_PARAM_FLASH && (APP_CONF_PARAM_FLASH_USE_EXT == 0)

#include "b_l4s5i_flash_drv.h"
#include "stm32l4xx_hal.h"
#include <string.h>

/* ================================================================== */
/*  STM32L4S5 Flash geometry                                           */
/* ================================================================== */

#define NVM_PAGE_ADDR   APP_CONF_PARAM_FLASH_INT_ADDR
#define NVM_PAGE_SIZE   APP_CONF_PARAM_FLASH_INT_SIZE
#define NVM_PAGE_BANK   (APP_CONF_PARAM_FLASH_INT_BANK == 2u ? FLASH_BANK_2 : FLASH_BANK_1)
#define NVM_PAGE_NUM    APP_CONF_PARAM_FLASH_INT_PAGE
#define NVM_NUM_PAGES   APP_CONF_PARAM_FLASH_INT_PAGES

/* ================================================================== */
/*  Helpers                                                            */
/* ================================================================== */

/** Return the base address of page @p pg (0 = highest addr, grows down). */
static uint32_t page_addr(uint32_t pg)
{
    return NVM_PAGE_ADDR - pg * NVM_PAGE_SIZE;
}

/** Return the Flash page number (for erase) of logical page @p pg. */
static uint32_t page_num(uint32_t pg)
{
    return NVM_PAGE_NUM - pg;
}

/** Return true when the 8-byte aligned slot at @p addr is erased. */
static bool slot_is_empty(uint32_t addr)
{
    const volatile uint64_t *p = (const volatile uint64_t *)addr;
    return *p == 0xFFFFFFFFFFFFFFFFULL;
}

/** Return true when the first slot of page @p pg is erased (page empty). */
static bool page_is_empty(uint32_t pg, uint32_t rec_size)
{
    return slot_is_empty(page_addr(pg));
}

/**
 * Find the "active" page — the last page (in rotation order) that
 * contains at least one written slot.  Returns -1 if all pages empty.
 */
static int find_active_page(uint32_t rec_size)
{
    int active = -1;
    for (uint32_t pg = 0u; pg < NVM_NUM_PAGES; pg++) {
        if (!page_is_empty(pg, rec_size)) {
            active = (int)pg;
        }
    }
    return active;
}

/** Erase a single Flash page. */
static bool erase_page(uint32_t pg)
{
    FLASH_EraseInitTypeDef erase;
    uint32_t page_err;

    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.Banks     = NVM_PAGE_BANK;
    erase.Page      = page_num(pg);
    erase.NbPages   = 1u;

    return HAL_FLASHEx_Erase(&erase, &page_err) == HAL_OK;
}

/* ================================================================== */
/*  Public API                                                         */
/* ================================================================== */

bool BSP_Flash_ReadLatest(void *buf, uint32_t size)
{
    int active = find_active_page(size);
    if (active < 0) {
        return false;   /* all pages empty */
    }

    /* Backward scan within the active page */
    uint32_t base = page_addr((uint32_t)active);
    uint32_t max_slots = NVM_PAGE_SIZE / size;

    for (int i = (int)max_slots - 1; i >= 0; i--) {
        uint32_t addr = base + (uint32_t)i * size;
        if (!slot_is_empty(addr)) {
            memcpy(buf, (const void *)addr, size);
            return true;
        }
    }
    return false;   /* active page has no valid record (shouldn't happen) */
}

bool BSP_Flash_ReadValidated(void *buf, uint32_t size,
                             bool (*validator)(const void *rec, uint32_t len))
{
    int active = find_active_page(size);
    if (active < 0) {
        return false;   /* all pages empty */
    }

    uint32_t max_slots = NVM_PAGE_SIZE / size;

    /* Scan pages starting from active, going backward in rotation order */
    for (uint32_t p = 0u; p < NVM_NUM_PAGES; p++) {
        uint32_t pg = ((uint32_t)active + NVM_NUM_PAGES - p) % NVM_NUM_PAGES;

        /* Skip empty pages (beyond active) */
        if ((p > 0u) && page_is_empty(pg, size)) {
            continue;
        }

        uint32_t base = page_addr(pg);

        /* Backward scan within this page */
        for (int i = (int)max_slots - 1; i >= 0; i--) {
            uint32_t addr = base + (uint32_t)i * size;
            if (!slot_is_empty(addr)) {
                memcpy(buf, (const void *)addr, size);
                if ((validator == NULL) || validator(buf, size)) {
                    return true;
                }
                /* Record failed validation — try previous slot */
            }
        }
    }

    return false;   /* no valid record found in any page */
}

bool BSP_Flash_WriteAppend(const void *buf, uint32_t size)
{
    uint32_t max_slots = NVM_PAGE_SIZE / size;
    int active = find_active_page(size);
    uint32_t pg;
    int target = -1;

    HAL_FLASH_Unlock();

    if (active < 0) {
        /* All pages empty — use page 0, slot 0 */
        pg = 0u;
        target = 0;
    } else {
        pg = (uint32_t)active;
        uint32_t base = page_addr(pg);

        /* Find first empty slot in active page */
        for (uint32_t i = 0u; i < max_slots; i++) {
            uint32_t addr = base + i * size;
            if (slot_is_empty(addr)) {
                target = (int)i;
                break;
            }
        }

        /* Active page full — rotate to next page */
        if (target < 0) {
            pg = (pg + 1u) % NVM_NUM_PAGES;

            if (!page_is_empty(pg, size)) {
                if (!erase_page(pg)) {
                    HAL_FLASH_Lock();
                    return false;
                }
            }
            target = 0;
        }
    }

    /* Program double-word by double-word */
    {
        uint32_t addr = page_addr(pg) + (uint32_t)target * size;
        const uint64_t *src = (const uint64_t *)buf;
        uint32_t dwords = size / 8u;

        for (uint32_t i = 0u; i < dwords; i++) {
            if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD,
                                  addr + i * 8u,
                                  src[i]) != HAL_OK) {
                HAL_FLASH_Lock();
                return false;
            }
        }
    }

    HAL_FLASH_Lock();
    return true;
}

#else
/* ISO C forbids an empty translation unit — provide a harmless typedef */
typedef int b_l4s5i_flash_drv_empty_tu;
#endif /* APP_CONF_PARAM_FLASH && BACKEND==0 */
