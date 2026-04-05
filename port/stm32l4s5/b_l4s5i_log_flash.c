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
 * @file  b_l4s5i_log_flash.c
 * @brief B-L4S5I — Persistent Flash log driver implementation
 *
 * Circular ring of Flash pages dedicated to log storage.
 * Completely independent from the parameter NVM driver.
 *
 * Fixed-length slot format (128 bytes):
 *   Byte [0..3]   : uint32_t seq    — monotonic sequence number
 *                    0xFFFFFFFF = empty (Flash erased state)
 *   Byte [4..123] : char text[120]  — log string, NUL-padded
 *   Byte [124..127]: uint32_t crc32 — CRC32 over bytes [0..124)
 *
 * The CRC32 protects against power-fail during write or erase:
 *   - Half-written slot: CRC mismatch → skipped by find_cursors/Read
 *   - Half-erased page:  random data  → CRC mismatch → page skipped
 *
 * The sequence number solves the "all pages full" problem:
 *   - tail (write position): slot with max seq + 1
 *   - head (oldest entry):   slot with min seq
 *   No ambiguity regardless of how many times pages have been recycled.
 *
 * Slots per page: 4096 / 128 = 32
 * Total with 100 pages: 3200 log entries
 */

#include "app_config.h"

#if APP_CONF_LOG_FLASH

#include "b_l4s5i_log_flash.h"
#include "anbo_log.h"
#include "stm32l4xx_hal.h"
#include <string.h>

#if APP_CONF_LOG_FLASH_USE_EXT == 0
/* ---- Internal Flash backend ---- */
#else
/* ---- External OSPI Flash backend ---- */
#include "b_l4s5i_ospi_drv.h"
#endif

/* ================================================================== */
/*  Geometry                                                           */
/* ================================================================== */

#define LOG_PAGE_ADDR       APP_CONF_LOG_FLASH_ADDR
#define LOG_PAGE_SIZE       APP_CONF_LOG_FLASH_PAGE_SIZE
#define LOG_NUM_PAGES       APP_CONF_LOG_FLASH_PAGES

#if APP_CONF_LOG_FLASH_USE_EXT == 0
#define LOG_PAGE_BANK       (APP_CONF_LOG_FLASH_BANK == 2u ? FLASH_BANK_2 : FLASH_BANK_1)
#define LOG_PAGE_NUM_START  APP_CONF_LOG_FLASH_PAGE_NUM
#endif

/** Fixed slot size — matches max log line length.
 *  Must be a multiple of 8 (STM32 double-word alignment). */
#define LOG_SLOT_SIZE       ((uint32_t)ANBO_CONF_LOG_LINE_MAX)

/** Compile-time check: slot must be 8-byte aligned. */
typedef char log_slot_align_chk[(LOG_SLOT_SIZE % 8u == 0u) ? 1 : -1];

/** Compile-time check: page must hold at least 1 slot. */
typedef char log_slot_fit_chk[(LOG_PAGE_SIZE >= LOG_SLOT_SIZE) ? 1 : -1];

/** Number of slots per page. */
#define LOG_SLOTS_PER_PAGE  (LOG_PAGE_SIZE / LOG_SLOT_SIZE)

/** CRC32 field size. */
#define CRC_SIZE            4u
/** CRC32 field offset (last 4 bytes of slot). */
#define CRC_OFFSET          (LOG_SLOT_SIZE - CRC_SIZE)

/** Sequence number offset within the slot. */
#define SEQ_OFFSET          0u
/** Sequence number that means "empty" (Flash erased state). */
#define SEQ_EMPTY           0xFFFFFFFFu
/** Text payload offset within the slot. */
#define TEXT_OFFSET          4u
/** Text payload capacity. */
#define TEXT_CAPACITY       (CRC_OFFSET - TEXT_OFFSET)

/* ================================================================== */
/*  Static state                                                       */
/* ================================================================== */

static uint32_t s_cur_page;     /**< Current write page (0..N-1) */
static uint32_t s_cur_slot;     /**< Next slot index within the page */
static uint32_t s_seq_counter;  /**< Next sequence number to write */
static uint32_t s_head_page;    /**< Page containing oldest entry */
static uint32_t s_head_slot;    /**< Slot index of oldest entry */
static bool     s_inited;

/* ================================================================== */
/*  Software CRC32 (nibble-based, 16-entry LUT — same as app_config)  */
/* ================================================================== */

static const uint32_t s_crc32_lut[16] = {
    0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu,
    0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
    0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu,
    0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu
};

static uint32_t crc32_calc(const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0u; i < len; i++) {
        crc = s_crc32_lut[(crc ^ p[i]) & 0x0Fu] ^ (crc >> 4u);
        crc = s_crc32_lut[(crc ^ (p[i] >> 4u)) & 0x0Fu] ^ (crc >> 4u);
    }
    return crc ^ 0xFFFFFFFFu;
}

/* ================================================================== */
/*  Helpers                                                            */
/* ================================================================== */

/** Return the base address of logical page @p pg (page 0 = highest). */
static uint32_t page_addr(uint32_t pg)
{
    return LOG_PAGE_ADDR - pg * LOG_PAGE_SIZE;
}

#if APP_CONF_LOG_FLASH_USE_EXT == 0
/** Return the Flash page number (for HAL erase) of logical page @p pg. */
static uint32_t page_num(uint32_t pg)
{
    return LOG_PAGE_NUM_START - pg;
}
#endif

/** Return the address of slot @p s in page @p pg. */
static uint32_t slot_addr(uint32_t pg, uint32_t s)
{
    return page_addr(pg) + s * LOG_SLOT_SIZE;
}

/** Read the sequence number of a slot. */
static uint32_t slot_seq(uint32_t pg, uint32_t s)
{
#if APP_CONF_LOG_FLASH_USE_EXT == 0
    return *(const volatile uint32_t *)slot_addr(pg, s);
#else
    uint32_t val = SEQ_EMPTY;
    BSP_OSPI_Read(slot_addr(pg, s), (uint8_t *)&val, sizeof(val));
    return val;
#endif
}

/** Return true when a slot is erased. */
static bool slot_is_empty(uint32_t pg, uint32_t s)
{
    return slot_seq(pg, s) == SEQ_EMPTY;
}

/** Return true when the first slot of page @p pg is empty. */
static bool page_is_empty(uint32_t pg)
{
    return slot_is_empty(pg, 0u);
}

/**
 * Read a full slot into a caller-supplied buffer and verify its CRC32.
 * @return true if the slot is non-empty AND CRC matches.
 */
static bool slot_read_valid(uint32_t pg, uint32_t s, uint8_t *buf)
{
    uint32_t addr = slot_addr(pg, s);
#if APP_CONF_LOG_FLASH_USE_EXT == 0
    memcpy(buf, (const void *)addr, LOG_SLOT_SIZE);
#else
    BSP_OSPI_Read(addr, buf, LOG_SLOT_SIZE);
#endif
    /* Check not empty */
    uint32_t seq;
    memcpy(&seq, &buf[SEQ_OFFSET], sizeof(seq));
    if (seq == SEQ_EMPTY) {
        return false;
    }
    /* Verify CRC */
    uint32_t stored_crc;
    memcpy(&stored_crc, &buf[CRC_OFFSET], sizeof(stored_crc));
    uint32_t calc_crc = crc32_calc(buf, CRC_OFFSET);
    return (stored_crc == calc_crc);
}

/**
 * Wraparound-safe "a is newer than b" comparison.
 * Uses signed-difference trick: (int32_t)(a - b) > 0.
 * Correct as long as the gap between any two live seq values
 * is less than 2^31 — easily satisfied (max 3200 live entries).
 */
static bool seq_after(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b) > 0;
}

/**
 * Advance a sequence number by 1, skipping SEQ_EMPTY (0xFFFFFFFF)
 * which is reserved as the Flash-erased sentinel.
 */
static uint32_t seq_next(uint32_t s)
{
    uint32_t n = s + 1u;
    return (n == SEQ_EMPTY) ? 0u : n;
}

/** Erase a single log page. */
static bool erase_page(uint32_t pg)
{
#if APP_CONF_LOG_FLASH_USE_EXT == 0
    FLASH_EraseInitTypeDef erase;
    uint32_t page_err;

    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.Banks     = LOG_PAGE_BANK;
    erase.Page      = page_num(pg);
    erase.NbPages   = 1u;

    HAL_FLASH_Unlock();
    bool ok = (HAL_FLASHEx_Erase(&erase, &page_err) == HAL_OK);
    HAL_FLASH_Lock();
    return ok;
#else
    return BSP_OSPI_SectorErase(page_addr(pg));
#endif
}

/**
 * Optimized two-phase cursor discovery.
 *
 * Phase 1 — page-level scan (N_pages reads):
 *   Read only the first slot's seq of each page.
 *   The page whose slot-0 seq is highest must be the tail (current
 *   write) page, because pages are filled sequentially and each new
 *   page starts with a higher seq than all previous pages.
 *   The page whose slot-0 seq is lowest is the head (oldest) page.
 *
 * Phase 2 — intra-page binary search (log2(slots_per_page) reads):
 *   Within the tail page, binary-search for the first empty slot to
 *   find the exact write position, then read the last written slot's
 *   seq to set `s_seq_counter`.
 *
 * Complexity: N_pages + log2(slots_per_page) + 1
 *   e.g. 100 pages, 32 slots/page → ~107 reads  (vs. 3200 brute-force)
 *
 * Wraparound-safe: uses seq_after() for all comparisons, so the
 * algorithm works correctly even when the 32-bit seq counter wraps
 * past 0xFFFFFFFF → 0x00000000.
 */
static void find_cursors(void)
{
    uint32_t min_seq  = SEQ_EMPTY;
    uint32_t max_seq  = 0u;
    uint32_t min_page = 0u;
    uint32_t max_page = 0u;
    bool     found    = false;

    /* ---- Phase 1: read & validate slot-0 of every page ---- */
    uint8_t tmp[LOG_SLOT_SIZE];
    for (uint32_t pg = 0u; pg < LOG_NUM_PAGES; pg++) {
        if (!slot_read_valid(pg, 0u, tmp)) {
            continue;  /* empty, half-written, or half-erased page */
        }
        uint32_t seq;
        memcpy(&seq, &tmp[SEQ_OFFSET], sizeof(seq));
        if (!found || seq_after(min_seq, seq)) {
            min_seq  = seq;
            min_page = pg;
        }
        if (!found || seq_after(seq, max_seq)) {
            max_seq  = seq;
            max_page = pg;
        }
        found = true;
    }

    if (!found) {
        /* All pages empty */
        s_cur_page    = 0u;
        s_cur_slot    = 0u;
        s_seq_counter = 0u;
        s_head_page   = 0u;
        s_head_slot   = 0u;
        return;
    }

    /* Head = first slot of the page with the lowest seq */
    s_head_page = min_page;
    s_head_slot = 0u;

    /* ---- Phase 2: binary search within tail page ---- */
    uint32_t lo = 0u, hi = LOG_SLOTS_PER_PAGE;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        if (slot_is_empty(max_page, mid)) {
            hi = mid;
        } else {
            lo = mid + 1u;
        }
    }
    /* lo = first empty slot index (= number of written slots) */

    if (lo == 0u) {
        /* Shouldn't happen (slot 0 was non-empty), but handle gracefully */
        s_cur_page    = max_page;
        s_cur_slot    = 0u;
        s_seq_counter = max_seq;
        return;
    }

    /* Scan backward from lo-1 to find the last CRC-valid slot.
     * A corrupt tail slot (half-written during power-fail) is skipped.
     *
     * IMPORTANT: s_cur_slot must be set to `lo` (= first truly empty
     * slot), NOT to the slot right after the last valid one.
     * Reason: a half-written slot already has some bits = 0; NOR Flash
     * cannot flip them back to 1, so re-programming that slot would
     * AND new data with the residual bits → corrupt the new write too.
     * By writing to `lo` we skip all corrupt slots safely. */
    uint32_t real_max_seq = max_seq;
    for (uint32_t i = lo; i > 0u; i--) {
        if (slot_read_valid(max_page, i - 1u, tmp)) {
            memcpy(&real_max_seq, &tmp[SEQ_OFFSET], sizeof(real_max_seq));
            break;
        }
    }
    s_seq_counter = seq_next(real_max_seq);
    s_cur_page    = max_page;
    s_cur_slot    = lo;   /* always past any corrupt/half-written slots */
}

/* ================================================================== */
/*  Public API                                                         */
/* ================================================================== */

void BSP_LogFlash_Init(void)
{
    find_cursors();
    s_inited = true;
}

uint32_t BSP_LogFlash_Write(const uint8_t *data, uint32_t len)
{
    if (!s_inited || data == NULL || len == 0u) {
        return 0u;
    }

    /* Strip trailing \r\n — Flash slots are fixed-size, no delimiter needed */
    if (len >= 2u && data[len - 2u] == '\r' && data[len - 1u] == '\n') {
        len -= 2u;
    } else if (len >= 1u && data[len - 1u] == '\n') {
        len -= 1u;
    }
    if (len == 0u) {
        return 0u;
    }

    /* Clamp to text payload capacity */
    if (len > TEXT_CAPACITY) {
        len = TEXT_CAPACITY;
    }

    /* If current page is full, rotate to the next page */
    if (s_cur_slot >= LOG_SLOTS_PER_PAGE) {
        uint32_t next = (s_cur_page + 1u) % LOG_NUM_PAGES;

        if (!page_is_empty(next)) {
            if (!erase_page(next)) {
                return 0u;
            }
            /* If head was on the erased page, advance head to next non-empty page */
            if (s_head_page == next) {
                s_head_page = (next + 1u) % LOG_NUM_PAGES;
                s_head_slot = 0u;
            }
        }

        s_cur_page = next;
        s_cur_slot = 0u;
    }

    /* Build fixed-size slot in stack buffer */
    uint8_t slot_buf[LOG_SLOT_SIZE];
    memset(slot_buf, 0, LOG_SLOT_SIZE);

    /* Write sequence number (little-endian uint32 at offset 0) */
    uint32_t seq = s_seq_counter;
    slot_buf[0] = (uint8_t)(seq & 0xFFu);
    slot_buf[1] = (uint8_t)((seq >> 8u) & 0xFFu);
    slot_buf[2] = (uint8_t)((seq >> 16u) & 0xFFu);
    slot_buf[3] = (uint8_t)((seq >> 24u) & 0xFFu);

    /* Text payload */
    memcpy(&slot_buf[TEXT_OFFSET], data, len);

    /* CRC32 over [0 .. CRC_OFFSET) = seq + text */
    uint32_t crc = crc32_calc(slot_buf, CRC_OFFSET);
    slot_buf[CRC_OFFSET + 0u] = (uint8_t)(crc & 0xFFu);
    slot_buf[CRC_OFFSET + 1u] = (uint8_t)((crc >> 8u) & 0xFFu);
    slot_buf[CRC_OFFSET + 2u] = (uint8_t)((crc >> 16u) & 0xFFu);
    slot_buf[CRC_OFFSET + 3u] = (uint8_t)((crc >> 24u) & 0xFFu);

    /* Program the slot into Flash */
    uint32_t addr = slot_addr(s_cur_page, s_cur_slot);

#if APP_CONF_LOG_FLASH_USE_EXT == 0
    /* Internal Flash: program double-word by double-word */
    const uint64_t *src = (const uint64_t *)slot_buf;
    uint32_t dwords = LOG_SLOT_SIZE / 8u;

    HAL_FLASH_Unlock();
    for (uint32_t i = 0u; i < dwords; i++) {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD,
                              addr + i * 8u,
                              src[i]) != HAL_OK) {
            HAL_FLASH_Lock();
            return 0u;
        }
    }
    HAL_FLASH_Lock();
#else
    /* External OSPI Flash: page-program (slot ≤ 256 B fits in one NOR page
     * as long as it doesn't straddle a 256-byte boundary).
     * The slot size is 128 B and slots are naturally aligned, so a single
     * BSP_OSPI_PageProgram call is sufficient. */
    if (!BSP_OSPI_PageProgram(addr, slot_buf, LOG_SLOT_SIZE)) {
        return 0u;
    }
#endif

    s_seq_counter = seq_next(s_seq_counter);
    s_cur_slot++;
    return len;
}

uint32_t BSP_LogFlash_Read(uint8_t *buf, uint32_t buf_size, uint32_t *ctx)
{
    if (buf == NULL || buf_size == 0u || ctx == NULL) {
        return 0u;
    }

    /*
     * ctx encoding: bits [31:16] = page index, bits [15:0] = slot index.
     * 0xFFFFFFFF = iteration complete.
     * 0x00000000 = start from head.
     */
    if (*ctx == 0xFFFFFFFFu) {
        return 0u;
    }

    uint32_t pg, si;
    if (*ctx == 0u) {
        /* Start iteration from head (oldest entry) */
        pg = s_head_page;
        si = s_head_slot;
    } else {
        pg = (*ctx >> 16u) & 0xFFFFu;
        si = *ctx & 0xFFFFu;
    }

    /* Walk slots in sequence-number order (page by page from head) */
    uint8_t rd_buf[LOG_SLOT_SIZE];
    uint32_t pages_visited = 0u;
    while (pages_visited < LOG_NUM_PAGES) {
        while (si < LOG_SLOTS_PER_PAGE) {
            if (slot_is_empty(pg, si)) {
                break;  /* rest of this page is empty */
            }

            /* Read full slot and validate CRC */
            if (!slot_read_valid(pg, si, rd_buf)) {
                /* Corrupt slot — skip it */
                si++;
                continue;
            }

            /* Copy text payload to caller's buffer */
            uint32_t copy = (TEXT_CAPACITY <= buf_size) ? TEXT_CAPACITY : buf_size;
            memcpy(buf, &rd_buf[TEXT_OFFSET], copy);

            /* Find actual string length */
            uint32_t slen = 0u;
            while (slen < copy && buf[slen] != '\0') { slen++; }

            /* Advance ctx to next slot */
            si++;
            if (si >= LOG_SLOTS_PER_PAGE) {
                pg = (pg + 1u) % LOG_NUM_PAGES;
                si = 0u;
                pages_visited++;
            }
            /* Encode the next position */
            *ctx = ((pg & 0xFFFFu) << 16u) | (si & 0xFFFFu);
            /* Special case: if next position is empty or back at write cursor, mark done */
            if ((pg == s_cur_page && si == s_cur_slot) ||
                (si < LOG_SLOTS_PER_PAGE && slot_is_empty(pg, si))) {
                *ctx = 0xFFFFFFFFu;
            }
            return slen;
        }

        pg = (pg + 1u) % LOG_NUM_PAGES;
        si = 0u;
        pages_visited++;
    }

    *ctx = 0xFFFFFFFFu;
    return 0u;
}

void BSP_LogFlash_Dump(void)
{
    if (!s_inited) {
        return;
    }

    uint32_t ctx  = 0u;
    uint8_t  text[TEXT_CAPACITY + 1u];
    uint32_t count = 0u;

    /* Helper: drain UART ring buffer completely.
     * Feed IWDG on every spin iteration — the busy-wait can last
     * tens of milliseconds per entry when tx_rb is nearly full. */
    #define DRAIN_UART() do { \
        while (Anbo_Log_Pending() > 0u) { \
            Anbo_Log_Flush(); \
            IWDG->KR = 0xAAAAu; \
        } \
        IWDG->KR = 0xAAAAu; \
    } while (0)

    /* Absolute upper bound: cannot have more entries than physical slots */
    const uint32_t max_entries = LOG_SLOTS_PER_PAGE * LOG_NUM_PAGES;

    /* Header */
    static const char hdr[] = "[R] === Flash log dump start ===\r\n";
    Anbo_Log_WriteRaw((const uint8_t *)hdr, sizeof(hdr) - 1u);
    DRAIN_UART();

    for (;;) {
        uint32_t len = BSP_LogFlash_Read(text, TEXT_CAPACITY, &ctx);
        if (len == 0u || count >= max_entries) {
            break;
        }

        /* Build "[R] <original text>\r\n" */
        char line[TEXT_CAPACITY + 8u];
        uint32_t pos = 0u;
        line[pos++] = '[';
        line[pos++] = 'R';
        line[pos++] = ']';
        line[pos++] = ' ';
        memcpy(&line[pos], text, len);
        pos += len;
        line[pos++] = '\r';
        line[pos++] = '\n';

        Anbo_Log_WriteRaw((const uint8_t *)line, pos);
        count++;

        /* Drain after every entry to prevent ring-buffer overflow */
        DRAIN_UART();
    }

    /* Footer with entry count */
    char footer[64];
    uint32_t flen = Anbo_Format(footer, sizeof(footer),
                                "[R] === dump end (%u entries) ===\r\n", count);
    Anbo_Log_WriteRaw((const uint8_t *)footer, flen);
    DRAIN_UART();

    #undef DRAIN_UART
}

uint32_t BSP_LogFlash_GetSeq(void)
{
    return s_seq_counter;
}

bool BSP_LogFlash_Erase(void)
{
    for (uint32_t pg = 0u; pg < LOG_NUM_PAGES; pg++) {
        if (!page_is_empty(pg)) {
            if (!erase_page(pg)) {
                return false;
            }
        }
    }
    s_cur_page    = 0u;
    s_cur_slot    = 0u;
    s_seq_counter = 0u;
    s_head_page   = 0u;
    s_head_slot   = 0u;
    return true;
}

#else
typedef int b_l4s5i_log_flash_empty_tu;
#endif /* APP_CONF_LOG_FLASH */
