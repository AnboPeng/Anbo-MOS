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
 * @file  anbo_log.c
 * @brief Anbo Kernel — Async logging system implementation
 *
 * Core: hand-rolled %s/%d/%u/%x/%c formatting -> push into static RingBuffer
 * -> device sends in the background.
 * stdio.h dependency is strictly forbidden (sprintf / printf / snprintf etc.).
 *
 * Constraints: C99 / fully static memory / no malloc / no sprintf
 */

#include "anbo_log.h"
#include "anbo_arch.h"
#include <stdarg.h>
#include <stddef.h>   /* NULL */

/* ================================================================== */
/*  Internal Static Data                                               */
/* ================================================================== */

/** Log ring buffer */
ANBO_RB_DEF(s_log_rb, ANBO_CONF_LOG_RB_ORDER);

/** Bound output device */
static Anbo_Device *s_log_dev;

/** Per-level sink masks (indexed by ANBO_LOG_LVL_*) */
static uint8_t s_sink_mask[4] = {
    ANBO_CONF_LOG_SINK_DEFAULT,     /* ERROR */
    ANBO_CONF_LOG_SINK_DEFAULT,     /* WARN  */
    ANBO_CONF_LOG_SINK_DEFAULT,     /* INFO  */
    ANBO_CONF_LOG_SINK_DEFAULT,     /* DEBUG */
};

/** Flash-write callback (NULL = disabled) */
static Anbo_Log_FlashWrite_t s_flash_writer;

/* ================================================================== */
/*  Minimal Formatting Engine                                          */
/* ================================================================== */

/** Unsigned integer to decimal string (reverse fill), returns char count written */
static uint32_t u32_to_dec(char *buf, uint32_t size, uint32_t val)
{
    char tmp[10];   /* uint32 max 4294967295 = 10 digits */
    uint32_t i = 0u;
    uint32_t j;

    if (val == 0u) {
        if (size > 0u) { buf[0] = '0'; }
        return 1u;
    }
    while (val != 0u) {
        tmp[i] = (char)('0' + (val % 10u));
        val /= 10u;
        i++;
    }
    /* Reverse into buf */
    for (j = 0u; j < i && j < size; j++) {
        buf[j] = tmp[i - 1u - j];
    }
    return j;
}

/** Signed integer to decimal string */
static uint32_t i32_to_dec(char *buf, uint32_t size, int32_t val)
{
    uint32_t off = 0u;
    if (val < 0) {
        if (size > 0u) { buf[0] = '-'; off = 1u; }
        /* Safe negation: handle INT32_MIN */
        val = -(val + 1);
        off += u32_to_dec(buf + off, size - off, (uint32_t)val + 1u);
    } else {
        off = u32_to_dec(buf, size, (uint32_t)val);
    }
    return off;
}

/** Unsigned integer to hexadecimal string (lowercase, pure bit-ops)
 *
 * Hex-to-ASCII conversion via bit operations (no lookup table):
 *   nibble = val & 0xF            -- extract lowest 4 bits
 *   ascii  = nibble + '0'         -- works for 0..9
 *   if nibble > 9:  ascii += ('a' - '0' - 10)  -- shift into 'a'..'f'
 *
 * The conditional add can also be expressed branchlessly:
 *   mask  = ((nibble - 10) >> 31) - 1   -- 0x00000000 if <10, 0xFFFFFFFF if >=10
 *   ascii = nibble + '0' + (mask & ('a' - '0' - 10))
 */
static inline char nibble_to_hex(uint8_t n)
{
    n &= 0x0Fu;
    /*
     * Branchless hex-to-ASCII using pure bit operations:
     *   Step 1: (n - 10) triggers unsigned underflow when n < 10
     *           -> bit 31 = 1  -> right-shift 31 -> 1
     *           When n >= 10: bit 31 = 0 -> shift -> 0
     *   Step 2: mask = 0 when n < 10, 0xFFFFFFFF when n >= 10
     *   Step 3: add ('a' - '0' - 10) = 39 only for a..f range
     */
    uint32_t mask = (((uint32_t)(n - 10u)) >> 31) - 1u;  /* 0 if n<10, all-1s if n>=10 */
    return (char)(n + '0' + (mask & (uint32_t)('a' - '0' - 10)));
}

static uint32_t u32_to_hex(char *buf, uint32_t size, uint32_t val)
{
    char tmp[8];    /* uint32 max 8 hex digits */
    uint32_t i = 0u;
    uint32_t j;

    if (val == 0u) {
        if (size > 0u) { buf[0] = '0'; }
        return 1u;
    }
    while (val != 0u) {
        tmp[i] = nibble_to_hex((uint8_t)val);  /* val & 0xF inside */
        val >>= 4u;
        i++;
    }
    for (j = 0u; j < i && j < size; j++) {
        buf[j] = tmp[i - 1u - j];
    }
    return j;
}

/** String length (no string.h dependency) */
static uint32_t str_len(const char *s)
{
    uint32_t n = 0u;
    while (s[n] != '\0') { n++; }
    return n;
}

/* ------------------------------------------------------------------ */

uint32_t Anbo_Format_V(char *buf, uint32_t size, const char *fmt, va_list ap)
{
    uint32_t pos = 0u;
    const char *p;

    if ((buf == NULL) || (size == 0u) || (fmt == NULL)) {
        return 0u;
    }

    /* Reserve 1 byte for '\0' */
    size -= 1u;

    for (p = fmt; *p != '\0' && pos < size; p++) {
        if (*p != '%') {
            buf[pos++] = *p;
            continue;
        }

        p++;    /* Skip '%' */

        /* Parse optional zero-pad flag and field width: %[0][width]spec */
        {
            char     pad_ch = ' ';
            uint32_t width  = 0u;

            if (*p == '0') {
                pad_ch = '0';
                p++;
            }
            while (*p >= '1' && *p <= '9') {
                width = width * 10u + (uint32_t)(*p - '0');
                p++;
            }

            switch (*p) {
            case 'd': {
                char tmp_f[12];
                int32_t  v = va_arg(ap, int32_t);
                uint32_t n = i32_to_dec(tmp_f, (uint32_t)sizeof(tmp_f), v);
                uint32_t pad_n = (width > n) ? (width - n) : 0u;
                uint32_t k;
                for (k = 0u; k < pad_n && pos < size; k++) {
                    buf[pos++] = pad_ch;
                }
                for (k = 0u; k < n && pos < size; k++) {
                    buf[pos++] = tmp_f[k];
                }
                break;
            }
            case 'u': {
                char tmp_f[10];
                uint32_t v = va_arg(ap, uint32_t);
                uint32_t n = u32_to_dec(tmp_f, (uint32_t)sizeof(tmp_f), v);
                uint32_t pad_n = (width > n) ? (width - n) : 0u;
                uint32_t k;
                for (k = 0u; k < pad_n && pos < size; k++) {
                    buf[pos++] = pad_ch;
                }
                for (k = 0u; k < n && pos < size; k++) {
                    buf[pos++] = tmp_f[k];
                }
                break;
            }
            case 'x': {
                char tmp_f[8];
                uint32_t v = va_arg(ap, uint32_t);
                uint32_t n = u32_to_hex(tmp_f, (uint32_t)sizeof(tmp_f), v);
                uint32_t pad_n = (width > n) ? (width - n) : 0u;
                uint32_t k;
                for (k = 0u; k < pad_n && pos < size; k++) {
                    buf[pos++] = pad_ch;
                }
                for (k = 0u; k < n && pos < size; k++) {
                    buf[pos++] = tmp_f[k];
                }
                break;
            }
            case 's': {
                const char *s = va_arg(ap, const char *);
                if (s == NULL) { s = "(null)"; }
                {
                    uint32_t slen = str_len(s);
                    uint32_t i;
                    for (i = 0u; i < slen && pos < size; i++) {
                        buf[pos++] = s[i];
                    }
                }
                break;
            }
            case 'c': {
                int c = va_arg(ap, int);    /* char promoted to int */
                buf[pos++] = (char)c;
                break;
            }
            case '%':
                buf[pos++] = '%';
                break;
            case '\0':
                /* fmt ends with '%' — step back so outer loop terminates correctly */
                p--;
                break;
            default:
                /* Unknown format specifier: output %X as-is */
                if (pos < size) { buf[pos++] = '%'; }
                if (pos < size) { buf[pos++] = *p; }
                break;
            }
        }
    }

    buf[pos] = '\0';
    return pos;
}

/* ------------------------------------------------------------------ */

uint32_t Anbo_Format(char *buf, uint32_t size, const char *fmt, ...)
{
    uint32_t n;
    va_list ap;
    va_start(ap, fmt);
    n = Anbo_Format_V(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

/* ================================================================== */
/*  Log API Implementation                                             */
/* ================================================================== */

void Anbo_Log_Init(Anbo_Device *dev)
{
    Anbo_RB_Reset(&s_log_rb);
    s_log_dev = dev;
}

/* ------------------------------------------------------------------ */

void Anbo_Log_SetDevice(Anbo_Device *dev)
{
    s_log_dev = dev;
}

/* ------------------------------------------------------------------ */

void Anbo_Log_SetSink(uint8_t level, uint8_t mask)
{
    if (level <= ANBO_LOG_LVL_DEBUG) {
        s_sink_mask[level] = mask;
    }
}

/* ------------------------------------------------------------------ */

void Anbo_Log_SetFlashWriter(Anbo_Log_FlashWrite_t fn)
{
    s_flash_writer = fn;
}

/* ------------------------------------------------------------------ */

void Anbo_Log_Printf(uint8_t level, const char *fmt, ...)
{
    char line[ANBO_CONF_LOG_LINE_MAX];
    uint32_t len;
    va_list ap;
    uint8_t mask;

    /* Runtime level filter (compile-time already trimmed by macros; extra safety check) */
    if (level > ANBO_CONF_LOG_LEVEL) {
        return;
    }

    mask = s_sink_mask[level];

    va_start(ap, fmt);
    len = Anbo_Format_V(line, (uint32_t)sizeof(line), fmt, ap);
    va_end(ap);

    /* UART sink: push into ring buffer for Flush() to drain */
    if ((mask & ANBO_LOG_SINK_UART) != 0u) {
        Anbo_Arch_Critical_Enter();
        Anbo_RB_Write(&s_log_rb, (const uint8_t *)line, len);
        Anbo_Arch_Critical_Exit();
    }

    /* Flash sink: immediate write via registered callback */
    if (((mask & ANBO_LOG_SINK_FLASH) != 0u) && (s_flash_writer != NULL)) {
        s_flash_writer((const uint8_t *)line, len);
    }
}

/* ------------------------------------------------------------------ */

void Anbo_Log_Flush(void)
{
    uint8_t  chunk[64];
    uint32_t avail;
    uint32_t peeked;
    uint32_t accepted;

    /*
     * Peek-Write-Skip pattern:
     *   1. Peek data from log_rb (non-destructive).
     *   2. Hand data to the device / UART backend.
     *   3. Skip only the bytes that were actually accepted.
     *
     * This prevents data loss when the downstream tx_rb is full:
     * unaccepted bytes remain in log_rb for the next Flush() call.
     */

    Anbo_Arch_Critical_Enter();
    avail = Anbo_RB_Count(&s_log_rb);
    Anbo_Arch_Critical_Exit();

    if (avail == 0u) {
        return;
    }

    if (avail > (uint32_t)sizeof(chunk)) {
        avail = (uint32_t)sizeof(chunk);
    }

    /* 1. Peek — read pointer stays unchanged */
    Anbo_Arch_Critical_Enter();
    peeked = Anbo_RB_Peek(&s_log_rb, chunk, avail);
    Anbo_Arch_Critical_Exit();

    if (peeked == 0u) {
        return;
    }

    /* 2. Submit to downstream */
    accepted = 0u;
    if (s_log_dev != NULL) {
        accepted = Anbo_Dev_AsyncWrite(s_log_dev, chunk, peeked);
    } else if (Anbo_Arch_UART_Transmit_DMA(chunk, peeked) == 0) {
        accepted = peeked;
    } else {
        /* Byte-by-byte fallback — always succeeds (blocking) */
        uint32_t i;
        for (i = 0u; i < peeked; i++) {
            Anbo_Arch_UART_PutChar((char)chunk[i]);
        }
        accepted = peeked;
    }

    /* 3. Skip only what was accepted — rest stays for next call */
    if (accepted > 0u) {
        Anbo_Arch_Critical_Enter();
        Anbo_RB_Skip(&s_log_rb, accepted);
        Anbo_Arch_Critical_Exit();
    }
}

/* ------------------------------------------------------------------ */

void Anbo_Log_DrainAll(void)
{
    /*
     * Blocking drain of all pending log data.
     *
     * Loop: Flush one chunk from log_rb → tx_rb, then spin-wait
     * until the device's tx_rb has space again (ISR is actively
     * transmitting).  Repeat until log_rb is empty, then wait for
     * the final tx_rb contents to be sent.
     *
     * Guard timeout prevents infinite hang if the UART ISR is broken.
     * At 115200-8N1 (≈11520 B/s) and max 1024+512 bytes buffered,
     * the total drain time is ≈ 133 ms.  Use 500 ms as a safe cap.
     */
    uint32_t guard       = 0u;
    const uint32_t limit = 500000u;  /* ~500 ms at 1 µs granularity */

    /* Phase 1: drain log_rb → tx_rb → wire */
    while (Anbo_Log_Pending() > 0u && guard < limit) {
        Anbo_Log_Flush();

        /* Yield briefly so the UART ISR can transmit */
        volatile uint32_t spin;
        for (spin = 0u; spin < 200u; spin++) {
            /* empty — compiler cannot optimise away (volatile) */
        }
        guard += 200u;
    }

    /* Phase 2: wait for tx_rb to drain (ISR finishes transmission) */
    if (s_log_dev != NULL && s_log_dev->tx_rb != NULL) {
        while (!Anbo_RB_IsEmpty(s_log_dev->tx_rb) && guard < limit) {
            volatile uint32_t spin;
            for (spin = 0u; spin < 200u; spin++) {
                /* empty */
            }
            guard += 200u;
        }
    }
}

/* ------------------------------------------------------------------ */

uint32_t Anbo_Log_WriteRaw(const uint8_t *data, uint32_t len)
{
    uint32_t written;

    Anbo_Arch_Critical_Enter();
    written = Anbo_RB_Write(&s_log_rb, data, len);
    Anbo_Arch_Critical_Exit();

    return written;
}

/* ------------------------------------------------------------------ */

uint32_t Anbo_Log_Pending(void)
{
    uint32_t count;

    Anbo_Arch_Critical_Enter();
    count = Anbo_RB_Count(&s_log_rb);
    Anbo_Arch_Critical_Exit();

    return count;
}
