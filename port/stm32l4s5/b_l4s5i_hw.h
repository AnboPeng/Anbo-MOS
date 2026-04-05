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
 * @file  b_l4s5i_hw.h
 * @brief B-L4S5I-IOT01A board hardware definitions and init API.
 *
 * Pin mapping (B-L4S5I-IOT01A):
 *   LED2      : PB14 (active high)
 *   User Btn  : PC13 (active low, EXTI)
 *   USART1 TX : PB6  (AF7)  — ST-LINK VCP
 *   USART1 RX : PB7  (AF7)  — ST-LINK VCP
 *
 * System clock: 120 MHz (MSI PLL -> SYSCLK)
 */

#ifndef B_L4S5I_HW_H
#define B_L4S5I_HW_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================== */
/*  GPIO Definitions                                                   */
/* ================================================================== */

#define BSP_LED2_PORT       GPIOB
#define BSP_LED2_PIN        GPIO_PIN_14

#define BSP_BTN_PORT        GPIOC
#define BSP_BTN_PIN         GPIO_PIN_13

/* ================================================================== */
/*  Board-level Init API                                               */
/* ================================================================== */

/**
 * @brief One-shot board initialisation.
 *
 * Configures, in order:
 *   1. HAL / NVIC priority grouping
 *   2. System clock tree (MSI -> PLL -> 120 MHz SYSCLK)
 *   3. SysTick (1 ms)
 *   4. FPU (if ANBO_CONF_USE_FPU)
 *   5. GPIO  — LED2 (PB14), User Button (PC13)
 *   6. USART1  — 115200-8N1 on PB6/PB7 (ST-LINK VCP)
 */
void BSP_Init(void);

/* ================================================================== */
/*  Peripheral Access Helpers                                          */
/* ================================================================== */

/** Toggle LED2 */
void BSP_LED2_Toggle(void);
/** Set LED2 on / off */
void BSP_LED2_Set(int on);

/** Return non-zero when User Button is pressed (PC13 low) */
int  BSP_BTN_IsPressed(void);

/**
 * @brief Get the HAL UART handle used for log output.
 * @return Pointer to the static UART_HandleTypeDef for USART1 (VCP).
 */
void *BSP_GetLogUartHandle(void);

/**
 * @brief USART1 DMA transmit (non-blocking).
 * @param data  Source buffer.
 * @param len   Byte count (max 65535).
 * @return 0 on success, -1 on failure.
 */
int BSP_USART1_Transmit_DMA(const uint8_t *data, uint32_t len);

/**
 * @brief USART1 byte-receive callback (called from IRQ).
 *
 * Weak symbol — override to feed data into an Anbo_RB.
 */
void BSP_USART1_RxByteCallback(uint8_t byte);

/* ================================================================== */
/*  IWDG Watchdog                                                      */
/* ================================================================== */

/**
 * @brief Start IWDG with the given timeout.
 * @param timeout_ms Desired timeout in milliseconds (approximate).
 *
 * Uses LSI (~32 kHz).  Once started the IWDG cannot be stopped.
 * Feed with Anbo_Arch_WDT_Feed() (IWDG->KR = 0xAAAA).
 */
void BSP_IWDG_Init(uint32_t timeout_ms);

/* ================================================================== */
/*  Reset Reason                                                       */
/* ================================================================== */

/**
 * @brief  Read the reset cause from RCC->CSR and clear the flags.
 * @param  out_csr  If non-NULL, receives the raw CSR value.
 * @return Human-readable string: "IWDG", "WWDG", "Software",
 *         "Low-power", "NRST pin", "BOR", or "POR".
 * @note   Call once early in main(); flags are cleared after reading.
 */
const char *BSP_GetResetReason(uint32_t *out_csr);

/* ================================================================== */
/*  LPTIM1 Low-Power Timer                                             */
/* ================================================================== */

/**
 * @brief Initialise LPTIM1 clocked by LSI for Stop 2 wakeup.
 *
 * Call once during system startup.  After init, BSP_LPTIM_StartOnce()
 * can be used to arm a single-shot wakeup before entering Stop 2.
 */
void BSP_LPTIM_Init(void);

/**
 * @brief Arm LPTIM1 for a single-shot wakeup after @p ms milliseconds.
 * @param ms  Timeout in milliseconds (max ~2000 with /32 prescaler at 32 kHz).
 */
void BSP_LPTIM_StartOnce(uint32_t ms);

/**
 * @brief Read the elapsed count since last BSP_LPTIM_StartOnce() and stop.
 * @return Elapsed time in milliseconds (approximate).
 */
uint32_t BSP_LPTIM_StopAndRead(void);

/* ================================================================== */
/*  RTC Wakeup Timer                                                   */
/* ================================================================== */

/**
 * @brief Initialise the RTC with LSI clock for wakeup-timer use.
 *
 * Does NOT configure calendar — only the periodic wakeup timer.
 * Call once during system startup.
 */
void BSP_RTC_Init(void);

/**
 * @brief Arm the RTC wakeup timer for a single wakeup after @p seconds.
 * @param seconds  Wakeup delay in seconds (1–65535).
 *
 * The RTC wakeup event can wake the MCU from Stop 2.
 * Call BSP_RTC_StopWakeup() after waking to disarm.
 */
void BSP_RTC_SetWakeup(uint32_t seconds);

/**
 * @brief Disarm the RTC wakeup timer.
 */
void BSP_RTC_StopWakeup(void);

/**
 * @brief Check whether the RTC wakeup flag is set.
 * @retval 1  RTC wakeup triggered.
 * @retval 0  Not triggered.
 */
int BSP_RTC_WakeupTriggered(void);

#ifdef __cplusplus
}
#endif

#endif /* B_L4S5I_HW_H */
