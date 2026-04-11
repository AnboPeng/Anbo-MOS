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
 * @file  b_l4s5i_hw.c
 * @brief B-L4S5I-IOT01A board-level hardware initialisation.
 *
 * Implements:
 *   - System clock: MSI(4 MHz) -> PLL -> 120 MHz SYSCLK, APB1/APB2
 *   - FPU enable (Cortex-M4F)
 *   - SysTick 1 ms
 *   - GPIO: PB14 (LED2), PC13 (User Button)
 *   - USART1 (PB6-TX / PB7-RX) 115200-8N1 (interrupt-driven, ST-LINK VCP)
 *
 * This file depends on STM32CubeL4 HAL drivers.
 */

#include "b_l4s5i_hw.h"
#include "b_l4s5i_uart_drv.h"
#include "anbo_config.h"

#include "stm32l4xx_hal.h"

/* ================================================================== */
/*  Static handles                                                     */
/* ================================================================== */

static UART_HandleTypeDef  huart1;
static IWDG_HandleTypeDef  hiwdg;
static LPTIM_HandleTypeDef hlptim1;

/* ================================================================== */
/*  Forward declarations (private)                                     */
/* ================================================================== */

static void SystemClock_Config(void);
static void FPU_Enable(void);
static void GPIO_Init(void);
static void USART1_Init(void);

/* ================================================================== */
/*  Public API                                                         */
/* ================================================================== */

void BSP_Init(void)
{
    /* HAL internal init + NVIC 4-bit preemption priority */
    HAL_Init();

    /* Clock tree: 120 MHz */
    SystemClock_Config();

    /* Enable FPU for Cortex-M4F (always on for this board) */
    FPU_Enable();

    /* GPIO: LED2, User Button */
    GPIO_Init();

    /* USART1: async log channel (interrupt-driven, ST-LINK VCP) */
    USART1_Init();
}

/* ---- LED2 -------------------------------------------------------- */

void BSP_LED2_Toggle(void)
{
    HAL_GPIO_TogglePin(BSP_LED2_PORT, BSP_LED2_PIN);
}

void BSP_LED2_Set(int on)
{
    HAL_GPIO_WritePin(BSP_LED2_PORT, BSP_LED2_PIN,
                      on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/* ---- User Button ------------------------------------------------- */

int BSP_BTN_IsPressed(void)
{
    return (HAL_GPIO_ReadPin(BSP_BTN_PORT, BSP_BTN_PIN) == GPIO_PIN_RESET);
}

/* ---- UART helpers ------------------------------------------------ */

void *BSP_GetLogUartHandle(void)
{
    return (void *)&huart1;
}

int BSP_USART1_Transmit_DMA(const uint8_t *data, uint32_t len)
{
    /*
     * Legacy DMA path kept for Anbo_Arch_UART_Transmit_DMA() compatibility.
     * In the new ISR-driven design, prefer BSP_USART1_GetDevice() + AsyncWrite.
     * This function falls back to interrupt-driven send via the device driver.
     */
    Anbo_Device *dev = BSP_USART1_GetDevice();
    if (dev == NULL || data == NULL || len == 0u) {
        return -1;
    }
    uint32_t written = Anbo_Dev_AsyncWrite(dev, data, len);
    return (written > 0u) ? 0 : -1;
}

/**
 * @brief Weak RX byte callback — override in application to handle RX.
 */
__attribute__((weak))
void BSP_USART1_RxByteCallback(uint8_t byte)
{
    (void)byte;
}

/* ================================================================== */
/*  System Clock Configuration                                         */
/*  MSI 4 MHz -> PLL(30) -> PLLCLK 120 MHz                            */
/*  APB1 = 120 MHz, APB2 = 120 MHz                                    */
/*  Flash: 5 wait states @ 120 MHz / 1.2 V                            */
/* ================================================================== */

static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    /*
     * Enable PWR clock and set voltage scaling to Range 1 (high perf).
     * Required for 120 MHz operation on STM32L4S5.
     */
    __HAL_RCC_PWR_CLK_ENABLE();
    if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK) {
        while (1) { /* Clock config error */ }
    }

    /* MSI 4 MHz as PLL source */
    osc.OscillatorType  = RCC_OSCILLATORTYPE_MSI;
    osc.MSIState        = RCC_MSI_ON;
    osc.MSICalibrationValue = RCC_MSICALIBRATION_DEFAULT;
    osc.MSIClockRange   = RCC_MSIRANGE_6;          /* 4 MHz */
    osc.PLL.PLLState    = RCC_PLL_ON;
    osc.PLL.PLLSource   = RCC_PLLSOURCE_MSI;
    osc.PLL.PLLM        = 1;                        /* /1 -> 4 MHz */
    osc.PLL.PLLN        = 60;                       /* x60 -> 240 MHz VCO */
    osc.PLL.PLLR        = RCC_PLLR_DIV2;            /* /2 -> 120 MHz SYSCLK */
    osc.PLL.PLLP        = RCC_PLLP_DIV7;
    osc.PLL.PLLQ        = RCC_PLLQ_DIV4;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
        while (1) { /* PLL config error */ }
    }

    /* Select PLL as SYSCLK, configure AHB / APB prescalers */
    clk.ClockType       = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                          RCC_CLOCKTYPE_PCLK1  | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource    = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider   = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider  = RCC_HCLK_DIV1;
    clk.APB2CLKDivider  = RCC_HCLK_DIV1;
    /* 5 wait states for 120 MHz @ Range 1 */
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_5) != HAL_OK) {
        while (1) { /* Clock switch error */ }
    }
}

/* ================================================================== */
/*  FPU Enable (Cortex-M4F)                                            */
/* ================================================================== */

static void FPU_Enable(void)
{
#if (__FPU_PRESENT == 1) && (__FPU_USED == 1)
    /* CP10 / CP11 full access */
    SCB->CPACR |= ((3UL << 20U) | (3UL << 22U));
    __DSB();
    __ISB();
#endif
}

/* ================================================================== */
/*  GPIO Initialisation                                                */
/* ================================================================== */

static void GPIO_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    /* Enable clocks for GPIOB, GPIOC */
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    /* LED2 — PB14 push-pull, no pull, low speed, default off */
    HAL_GPIO_WritePin(BSP_LED2_PORT, BSP_LED2_PIN, GPIO_PIN_RESET);
    gpio.Pin   = BSP_LED2_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(BSP_LED2_PORT, &gpio);

    /* User Button — PC13 falling-edge EXTI interrupt */
    gpio.Pin   = BSP_BTN_PIN;
    gpio.Mode  = GPIO_MODE_IT_FALLING;
    gpio.Pull  = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(BSP_BTN_PORT, &gpio);

    /* Enable EXTI15_10 interrupt for PC13 */
    HAL_NVIC_SetPriority(EXTI15_10_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
}

/* ================================================================== */
/*  USART1 Initialisation — 115200-8N1 (interrupt-driven, VCP)         */
/* ================================================================== */

static void USART1_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    /* Enable clocks */
    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /*
     * Enable HSI16 oscillator explicitly.
     * SystemClock_Config only starts MSI+PLL; HSI16 is off by default.
     * We need HSI16 ON so USART1 keeps clocking in Stop 2.
     */
    {
        RCC_OscInitTypeDef osc = {0};
        osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
        osc.HSIState       = RCC_HSI_ON;
        osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
        osc.PLL.PLLState   = RCC_PLL_NONE;  /* don't touch PLL */
        HAL_RCC_OscConfig(&osc);
    }

    /*
     * Switch USART1 clock source from PCLK2 to HSI16.
     * HSI16 stays running in Stop 0/1/2, so USART1 can still
     * detect incoming start bits and wake the CPU from Stop 2.
     * BRR is recalculated by HAL_UART_Init based on this clock.
     */
    RCC_PeriphCLKInitTypeDef pclk = {0};
    pclk.PeriphClockSelection = RCC_PERIPHCLK_USART1;
    pclk.Usart1ClockSelection = RCC_USART1CLKSOURCE_HSI;
    HAL_RCCEx_PeriphCLKConfig(&pclk);

    /* PB6 = USART1_TX, PB7 = USART1_RX,  AF7 */
    gpio.Pin       = GPIO_PIN_6 | GPIO_PIN_7;
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_PULLUP;
    gpio.Speed     = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOB, &gpio);

    /* USART1 parameters (BRR computed from HSI16 = 16 MHz) */
    huart1.Instance          = USART1;
    huart1.Init.BaudRate     = 115200;
    huart1.Init.WordLength   = UART_WORDLENGTH_8B;
    huart1.Init.StopBits     = UART_STOPBITS_1;
    huart1.Init.Parity       = UART_PARITY_NONE;
    huart1.Init.Mode         = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart1) != HAL_OK) {
        while (1) { /* UART init error */ }
    }

    /*
     * Enable USART1 hardware FIFO (8-level RX + 8-level TX).
     * Critical for Stop 2 wakeup: when a UART byte wakes the CPU,
     * the clock recovery takes ~1 ms with interrupts disabled.
     * Without FIFO, subsequent bytes cause overrun (ORE) and are lost.
     * With FIFO, up to 8 bytes buffer in hardware during recovery.
     *
     * RX threshold = 1/8 (1 byte) → RXFNE fires per byte, same as
     * non-FIFO RXNE behaviour.  TX threshold = 7/8 → TX interrupt
     * fires when almost empty, maximising ISR efficiency.
     */
    HAL_UARTEx_EnableFifoMode(&huart1);
    HAL_UARTEx_SetRxFifoThreshold(&huart1, UART_RXFIFO_THRESHOLD_1_8);
    HAL_UARTEx_SetTxFifoThreshold(&huart1, UART_TXFIFO_THRESHOLD_7_8);

    /*
     * Configure USART1 wakeup from Stop 2.
     * WUS = 11 (RXNE flag) — wake on any received byte.
     * WUFIE enables the wakeup-from-Stop interrupt.
     * EXTI line 26 maps to USART1 wakeup on STM32L4.
     */
    {
        UART_WakeUpTypeDef wkup;
        wkup.WakeUpEvent = UART_WAKEUP_ON_READDATA_NONEMPTY;
        wkup.AddressLength = 0;
        wkup.Address = 0;
        HAL_UARTEx_StopModeWakeUpSourceConfig(&huart1, wkup);
    }
    HAL_UARTEx_EnableStopMode(&huart1);
    EXTI->IMR1 |= (1u << 26);   /* EXTI line 26 = USART1 wakeup */

    /* Enable USART1 global IRQ */
    HAL_NVIC_SetPriority(USART1_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);

    /*
     * RXNE interrupt is enabled later by the device driver open().
     * TXE interrupt is enabled on-demand when data is written.
     */
}

/* ================================================================== */
/*  IRQ Handlers                                                       */
/* ================================================================== */

/**
 * @brief USART1 interrupt — delegate entirely to the ISR-driven driver.
 */
void USART1_IRQHandler(void)
{
    BSP_USART1_IRQ();
}

/* ================================================================== */
/*  HAL Tick — use SysTick (driven by HAL_Init)                        */
/*  HAL_IncTick() is called from SysTick_Handler in the port file.     */
/* ================================================================== */

/* ================================================================== */
/*  IWDG Initialisation — Independent Watchdog                         */
/* ================================================================== */

void BSP_IWDG_Init(uint32_t timeout_ms)
{
    /*
     * LSI ≈ 32 kHz.  Prescaler /32 → counter clock = 1 kHz.
     * Reload = timeout_ms (max 4095 → ~4 s).
     */
    uint32_t reload = timeout_ms;
    if (reload > 4095u) {
        reload = 4095u;
    }

    hiwdg.Instance       = IWDG;
    hiwdg.Init.Prescaler = IWDG_PRESCALER_32;
    hiwdg.Init.Reload    = reload;
    hiwdg.Init.Window    = IWDG_WINDOW_DISABLE;
    if (HAL_IWDG_Init(&hiwdg) != HAL_OK) {
        while (1) { /* IWDG init error */ }
    }
}

/* ================================================================== */
/*  Reset Reason — read and clear RCC CSR flags                        */
/* ================================================================== */

const char *BSP_GetResetReason(uint32_t *out_csr)
{
    uint32_t csr = RCC->CSR;
    RCC->CSR |= RCC_CSR_RMVF;          /* clear flags for next boot */

    if (out_csr != NULL) {
        *out_csr = csr;
    }

    if (csr & RCC_CSR_IWDGRSTF)  { return "IWDG"; }
    if (csr & RCC_CSR_WWDGRSTF)  { return "WWDG"; }
    if (csr & RCC_CSR_SFTRSTF)   { return "Software"; }
    if (csr & RCC_CSR_LPWRRSTF)  { return "Low-power"; }
    if (csr & RCC_CSR_PINRSTF)   { return "NRST pin"; }
    if (csr & RCC_CSR_BORRSTF)   { return "BOR"; }
    return "POR";
}

/* ================================================================== */
/*  LPTIM1 Initialisation — Stop 2 wakeup timer                       */
/* ================================================================== */

void BSP_LPTIM_Init(void)
{
    /* Enable LSI (already running for IWDG; ensure it is on) */
    RCC_OscInitTypeDef osc = {0};
    osc.OscillatorType = RCC_OSCILLATORTYPE_LSI;
    osc.LSIState       = RCC_LSI_ON;
    osc.PLL.PLLState   = RCC_PLL_NONE;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
        while (1) { /* LSI error */ }
    }

    /* Select LSI as LPTIM1 clock source */
    RCC_PeriphCLKInitTypeDef pclk = {0};
    pclk.PeriphClockSelection = RCC_PERIPHCLK_LPTIM1;
    pclk.Lptim1ClockSelection = RCC_LPTIM1CLKSOURCE_LSI;
    if (HAL_RCCEx_PeriphCLKConfig(&pclk) != HAL_OK) {
        while (1) { /* LPTIM1 clock error */ }
    }

    __HAL_RCC_LPTIM1_CLK_ENABLE();

    hlptim1.Instance               = LPTIM1;
    hlptim1.Init.Clock.Source      = LPTIM_CLOCKSOURCE_APBCLOCK_LPOSC;
    hlptim1.Init.Clock.Prescaler   = LPTIM_PRESCALER_DIV32;   /* 32 kHz / 32 = 1 kHz */
    hlptim1.Init.Trigger.Source    = LPTIM_TRIGSOURCE_SOFTWARE;
    hlptim1.Init.OutputPolarity    = LPTIM_OUTPUTPOLARITY_HIGH;
    hlptim1.Init.UpdateMode        = LPTIM_UPDATE_IMMEDIATE;
    hlptim1.Init.CounterSource     = LPTIM_COUNTERSOURCE_INTERNAL;
    hlptim1.Init.Input1Source      = LPTIM_INPUT1SOURCE_GPIO;
    hlptim1.Init.Input2Source      = LPTIM_INPUT2SOURCE_GPIO;
    if (HAL_LPTIM_Init(&hlptim1) != HAL_OK) {
        while (1) { /* LPTIM init error */ }
    }

    /*
     * Pre-configure ARRM interrupt while LPTIM is still disabled.
     * IER can ONLY be written when ENABLE = 0 (RM0432 §36.7.4).
     * Doing it once here avoids the HAL's problematic
     * disable → modify IER → re-enable dance on every StartOnce.
     */
    LPTIM1->IER |= LPTIM_IER_ARRMIE;

    /*
     * Enable EXTI line 32 (LPTIM1) in interrupt mode.
     * Required for LPTIM1 to wake CPU from Stop 2.
     * Set once and never cleared — the HAL's Counter_Stop_IT was
     * clearing it on every stop, which could race with Stop 2 entry.
     */
    EXTI->IMR2 |= EXTI_IMR2_IM32;

    /* LPTIM1 interrupt — wakes CPU from Stop 2 */
    HAL_NVIC_SetPriority(LPTIM1_IRQn, 4, 0);
    HAL_NVIC_EnableIRQ(LPTIM1_IRQn);
}

/**
 * @brief  Read LPTIM1 counter with APB/LSI clock domain sync.
 *
 * The counter register is in the LSI clock domain.  An APB read
 * may sample a metastable value.  RM0432 §36.7.8 recommends:
 *   Read twice; if equal → valid.  Otherwise read a third time.
 */
static uint32_t lptim_read_cnt(void)
{
    uint32_t a = LPTIM1->CNT;
    uint32_t b = LPTIM1->CNT;
    if (a == b) {
        return a;
    }
    return LPTIM1->CNT;
}

void BSP_LPTIM_StartOnce(uint32_t ms)
{
    /*
     * Counter clock = 1 kHz (LSI 32 kHz / prescaler 32).
     * Period = ms counts.  Max representable = 65535 ticks ≈ 65 s.
     *
     * Direct register programming — bypasses HAL_LPTIM_Counter_Start_IT
     * which performs a Disable → Re-enable dance that can corrupt the
     * counter's internal shadow registers, preventing ARRM from firing
     * in Stop 2 (the MCU would never wake up).
     *
     * Sequence (RM0432 §36.4.1):
     *   1. ENABLE  (counter resets to 0, APB registers retained)
     *   2. Clear stale flags via ICR  (requires ENABLE = 1)
     *   3. Write ARR
     *   4. Wait ARROK  (APB → LSI sync complete)
     *   5. Start single-shot  (SNGSTRT)
     */
    uint32_t period = ms;
    if (period == 0u) {
        period = 1u;
    }
    if (period > 0xFFFFu) {
        period = 0xFFFFu;
    }

    LPTIM_TypeDef *lpt = LPTIM1;

    /* 1. Enable peripheral (counter = 0, IER preserved from Init) */
    lpt->CR |= LPTIM_CR_ENABLE;

    /* 2. Clear stale ARRM / ARROK from previous cycle */
    lpt->ICR = LPTIM_ICR_ARRMCF | LPTIM_ICR_ARROKCF;

    /* 3. Write ARR — triggers APB → LSI synchronisation */
    lpt->ARR = period - 1u;

    /* 4. Wait for ARROK — sync to LSI domain complete.
     *    Typically < 100 µs (3 LSI cycles at 32 kHz).
     *    MUST complete before Stop 2 entry, otherwise the APB bus
     *    dies mid-transfer and the counter never loads the correct
     *    ARR value → ARRM never fires → infinite sleep.
     */
    while (!(lpt->ISR & LPTIM_ISR_ARROK)) {
        /* spin */
    }
    lpt->ICR = LPTIM_ICR_ARROKCF;

    /* 5. Start single-shot: counter counts 0 → ARR, fires ARRM,
     *    then stops.  SNGSTRT is auto-cleared by hardware. */
    lpt->CR |= LPTIM_CR_SNGSTRT;
}

uint32_t BSP_LPTIM_StopAndRead(void)
{
    /*
     * Direct register programming — bypasses HAL_LPTIM_Counter_Stop_IT
     * which clears the EXTI line and performs a disable sequence.
     *
     * Read elapsed time before disabling.  Two cases:
     *  a) ARRM set (normal timeout) → full period, elapsed = ARR + 1.
     *  b) ARRM clear (early wakeup) → elapsed = CNT.
     */
    LPTIM_TypeDef *lpt = LPTIM1;

    /* Sample state while LPTIM is still enabled + running */
    uint32_t cnt  = lptim_read_cnt();
    uint32_t arr  = lpt->ARR;
    int      arrm = (lpt->ISR & LPTIM_ISR_ARRM) != 0;

    /* Clear ARRM flag (ICR requires ENABLE = 1) */
    if (arrm) {
        lpt->ICR = LPTIM_ICR_ARRMCF;
    }

    /* Disable LPTIM — counter resets, IER preserved */
    lpt->CR &= ~LPTIM_CR_ENABLE;

    if (arrm) {
        return arr + 1u;    /* full period */
    }
    return cnt;             /* early wakeup */
}

/* ================================================================== */
/*  EXTI15_10 IRQ — User Button (PC13)                                 */
/* ================================================================== */

void EXTI15_10_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(BSP_BTN_PIN);
}

/* ================================================================== */
/*  LPTIM1 IRQ — Stop 2 wakeup                                        */
/* ================================================================== */

void LPTIM1_IRQHandler(void)
{
    HAL_LPTIM_IRQHandler(&hlptim1);
}

/* ================================================================== */
/*  RTC Wakeup Timer — LSI clock, wakeup-only (no calendar)            */
/* ================================================================== */

static RTC_HandleTypeDef hrtc;

void BSP_RTC_Init(void)
{
    /*
     * Backup domain write protection (DBP) must be disabled BEFORE
     * writing to RCC_BDCR (RTCEN, RTCSEL) or RTC registers.
     * HAL_RCCEx_PeriphCLKConfig() enables DBP internally, so call
     * it FIRST to select the RTC clock source, THEN enable RTC.
     */

    /* 1. Select LSI as RTC clock source (also enables backup domain access) */
    RCC_PeriphCLKInitTypeDef pclk = {0};
    pclk.PeriphClockSelection = RCC_PERIPHCLK_RTC;
    pclk.RTCClockSelection    = RCC_RTCCLKSOURCE_LSI;
    HAL_RCCEx_PeriphCLKConfig(&pclk);

    /* 2. Now that DBP is set, enable RTC peripheral clock */
    __HAL_RCC_RTC_ENABLE();

    hrtc.Instance            = RTC;
    hrtc.Init.HourFormat     = RTC_HOURFORMAT_24;
    hrtc.Init.AsynchPrediv   = 127u;         /* LSI/128 */
    hrtc.Init.SynchPrediv    = 249u;         /* /250 → 1 Hz */
    hrtc.Init.OutPut         = RTC_OUTPUT_DISABLE;
    HAL_RTC_Init(&hrtc);

    /* NVIC for RTC wakeup (EXTI line 20 on STM32L4) */
    HAL_NVIC_SetPriority(RTC_WKUP_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(RTC_WKUP_IRQn);

    /* EXTI line 20 = RTC wakeup — must be unmasked for Stop 2 wakeup */
    EXTI->IMR1 |= EXTI_IMR1_IM20;
}

void BSP_RTC_SetWakeup(uint32_t seconds)
{
    if (seconds == 0u) { return; }
    if (seconds > 65535u) { seconds = 65535u; }

    /* Wakeup clock = 1 Hz (RTCCLK/16 with ck_spre) */
    HAL_RTCEx_SetWakeUpTimer_IT(&hrtc,
                                (uint32_t)(seconds - 1u),
                                RTC_WAKEUPCLOCK_CK_SPRE_16BITS);
}

void BSP_RTC_StopWakeup(void)
{
    HAL_RTCEx_DeactivateWakeUpTimer(&hrtc);
}

int BSP_RTC_WakeupTriggered(void)
{
    return (__HAL_RTC_WAKEUPTIMER_GET_FLAG(&hrtc, RTC_FLAG_WUTF) != 0u) ? 1 : 0;
}

volatile uint8_t g_rtc_fired = 0;

void RTC_WKUP_IRQHandler(void)
{
    g_rtc_fired = 1;
    HAL_RTCEx_WakeUpTimerIRQHandler(&hrtc);
}
