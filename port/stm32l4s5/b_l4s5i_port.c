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
 * @file  b_l4s5i_port.c
 * @brief B-L4S5I-IOT01A — Anbo Arch HAL implementation.
 *
 * Implements every function / callback declared in anbo_arch.h:
 *   - Critical section   : PRIMASK save/restore (nesting safe)
 *   - GetTick            : SysTick-based 1 ms counter
 *   - WDT_Feed           : IWDG reload (when ANBO_CONF_WDT == 1)
 *   - Idle               : STM32L4 Stop 2 low-power mode with LPTIM1 wakeup
 *   - UART_PutChar       : Blocking single-byte transmit (USART1 VCP)
 *   - UART_Transmit_DMA  : Non-blocking ISR-driven bulk transmit (USART1)
 *
 * Also provides:
 *   - SysTick_Handler for both HAL and Anbo
 *   - HAL_GPIO_EXTI_Callback that publishes ANBO_SIG_USER_BUTTON
 */

#include "anbo_arch.h"
#include "anbo_config.h"
#include "anbo_ebus.h"
#include "anbo_log.h"
#include "b_l4s5i_hw.h"
#include "b_l4s5i_uart_drv.h"

#include "stm32l4xx_hal.h"

/* USART FIFO-capable bit name compatibility (STM32L4S5) */
#ifndef USART_ISR_TXE
#define USART_ISR_TXE   USART_ISR_TXE_TXFNF
#endif

/* ================================================================== */
/*  1. Critical Section — PRIMASK nesting                              */
/* ================================================================== */

/*
 * Use a simple nesting counter + saved PRIMASK approach.
 * Anbo is single-core cooperative, so PRIMASK is sufficient.
 */
static volatile uint32_t s_crit_nesting = 0u;
static volatile uint32_t s_primask_save = 0u;

void Anbo_Arch_Critical_Enter(void)
{
    uint32_t pm = __get_PRIMASK();
    __disable_irq();

    if (s_crit_nesting == 0u) {
        s_primask_save = pm;
    }
    ++s_crit_nesting;
}

void Anbo_Arch_Critical_Exit(void)
{
    if (s_crit_nesting == 0u) {
        return;  /* Mismatched exit — defensive guard */
    }
    --s_crit_nesting;
    if (s_crit_nesting == 0u) {
        __set_PRIMASK(s_primask_save);
    }
}

/* ================================================================== */
/*  2. System Tick — 1 ms monotonic counter                            */
/* ================================================================== */

volatile uint32_t s_tick_ms = 0u;

uint32_t Anbo_Arch_GetTick(void)
{
    return s_tick_ms;
}

/**
 * @brief SysTick interrupt handler.
 *
 * Increments both the Anbo tick and the HAL tick so that HAL_Delay()
 * and other HAL timing functions continue to work.
 */
void SysTick_Handler(void)
{
    s_tick_ms++;
    HAL_IncTick();
}

/* ================================================================== */
/*  3. Watchdog Feed — IWDG                                            */
/* ================================================================== */

#if ANBO_CONF_WDT

/*
 * The IWDG must be started once during early init (not handled here;
 * call BSP_IWDG_Init() from main if watchdog is needed).
 * This function only reloads the down-counter.
 */
void Anbo_Arch_WDT_Feed(void)
{
    IWDG->KR = 0xAAAAu;   /* Reload IWDG counter */
}

#endif /* ANBO_CONF_WDT */

/* ================================================================== */
/*  4. UART — Log serial output                                        */
/* ================================================================== */

/**
 * @brief Blocking single-byte transmit on USART1 (VCP).
 *
 * Busy-waits on the TXE flag, then writes to TDR.
 * Used as fallback when the device model or DMA is not available.
 */
void Anbo_Arch_UART_PutChar(char c)
{
    UART_HandleTypeDef *h = (UART_HandleTypeDef *)BSP_GetLogUartHandle();
    USART_TypeDef *uart   = h->Instance;

    /* Wait until TX data register is empty */
    while ((uart->ISR & USART_ISR_TXE_TXFNF) == 0u) {
        /* busy wait */
    }
    uart->TDR = (uint32_t)(uint8_t)c;
}

/**
 * @brief Non-blocking bulk transmit on USART1 via ISR-driven device driver.
 *
 * Pushes data into the device's tx_rb and kicks the TXE interrupt.
 * Returns -1 if nothing could be queued (buffer full).
 */
int Anbo_Arch_UART_Transmit_DMA(const uint8_t *buf, uint32_t len)
{
    Anbo_Device *dev = BSP_USART1_GetDevice();
    if (dev == NULL || buf == NULL || len == 0u) {
        return -1;
    }
    uint32_t written = Anbo_Dev_AsyncWrite(dev, buf, len);
    return (written > 0u) ? 0 : -1;
}

/* ================================================================== */
/*  5. Low-Power Idle — Stop 2 Mode with LPTIM1 wakeup                */
/* ================================================================== */

/* Always declare — used by both Idle and App_Sleep deep-sleep */
void SystemClock_Recovery(void);

#if ANBO_CONF_IDLE_SLEEP

/**
 * @brief Enter STM32L4 Stop 2 mode for up to @p ms milliseconds.
 *
 * Strategy:
 *   1. ms == 0       → return immediately.
 *   2. ms <= 2       → WFI (SysTick still running, minimal overhead).
 *   3. ms > 2        → arm LPTIM1 single-shot, enter Stop 2.
 *      On wake-up:   restore 120 MHz clock, read LPTIM elapsed count,
 *                    compensate the Anbo tick counter accurately.
 */
void Anbo_Arch_Idle(uint32_t ms)
{
    if (ms == 0u) {
        return;
    }

    if (ms <= 2u) {
        /* Short sleep: just WFI with SysTick running. */
        __WFI();
        return;
    }

    /* ---- Stop 2 entry with LPTIM1 wakeup ---- */

    /* Cap sleep to stay well within 2 s IWDG timeout */
    if (ms > 1500u) {
        ms = 1500u;
    }

    /* Arm LPTIM1 to wake us after ms milliseconds */
    BSP_LPTIM_StartOnce(ms);

    /* Suspend SysTick interrupt */
    SysTick->CTRL &= ~SysTick_CTRL_TICKINT_Msk;
    HAL_SuspendTick();

    /* Clear wakeup flag */
    __HAL_PWR_CLEAR_FLAG(PWR_FLAG_WU);

    /*
     * Feed IWDG right before entering Stop 2 so the hardware
     * countdown starts from its full period (2 s).  This gives
     * maximum margin: 1500 ms sleep + ~500 ms for wakeup and
     * clock recovery before the next feed in WDT_Monitor.
     */
#if ANBO_CONF_WDT
    IWDG->KR = 0xAAAAu;
#endif

    /*
     * Disable interrupts before entering Stop 2.
     * This prevents a race where an external interrupt (button, UART)
     * arrives between "decide to sleep" and actual WFI, and also
     * ensures that after wakeup we can safely compensate the tick
     * counter before any ISR runs.
     */
    __disable_irq();

    /* Enter Stop 2 — LPTIM1 interrupt will wake us */
    HAL_PWREx_EnterSTOP2Mode(PWR_STOPENTRY_WFI);

    /* ---- Wakeup path (interrupts still disabled) ---- */

    /*
     * Feed IWDG immediately after wakeup, BEFORE SystemClock_Recovery.
     * IWDG is LSI-clocked and kept counting during Stop 2.  The
     * recovery path reconfigures PLL via HAL functions that rely on
     * HAL_GetTick() — but SysTick is still suspended and interrupts
     * are masked, so HAL_GetTick() is frozen.  If PLL lock is slow,
     * the HAL internal timeout never fires and the function could
     * block.  This feed buys us a fresh 2 s window to complete
     * recovery without IWDG firing.
     */
#if ANBO_CONF_WDT
    IWDG->KR = 0xAAAAu;
#endif

    /* Read LPTIM elapsed time before stopping it */
    uint32_t elapsed = BSP_LPTIM_StopAndRead();

    /* Restore 120 MHz clock tree */
    SystemClock_Recovery();

    /*
     * Compensate tick counter with actual elapsed time.
     * This is the SOLE authoritative time adjustment point.
     */
    s_tick_ms += elapsed;

    /* Diagnostic: log if elapsed looks suspicious (> requested or 0) */
    if (elapsed == 0u || elapsed > ms + 10u) {
        ANBO_LOGW("IDLE: req=%u elapsed=%u", ms, elapsed);
    }

    /*
     * CRITICAL: Clear LPTIM1 NVIC pending flag BEFORE re-enabling interrupts.
     *
     * If LPTIM1 expired at the same moment as the external wakeup source,
     * its interrupt pending bit is already set.  Without clearing it here,
     * re-enabling interrupts would cause LPTIM1_IRQHandler to fire and
     * potentially double-count the elapsed time.
     *
     * We've already accounted for the full elapsed duration above,
     * so the LPTIM1 ISR has nothing useful left to do — discard it.
     */
    NVIC_ClearPendingIRQ(LPTIM1_IRQn);

    /* Safe to re-enable interrupts now — the tick ledger is balanced */
    __enable_irq();

    /* Re-enable SysTick interrupt */
    HAL_ResumeTick();
    SysTick->CTRL |= SysTick_CTRL_TICKINT_Msk;
}

#endif /* ANBO_CONF_IDLE_SLEEP */

/**
 * @brief Restore 120 MHz clock tree after Stop 2 wake-up.
 *
 * Used by both Anbo_Arch_Idle() and App_Sleep deep-sleep loop,
 * so it lives outside the ANBO_CONF_IDLE_SLEEP guard.
 */
void SystemClock_Recovery(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    /*
     * Re-enable both MSI (PLL source) AND HSI16 (USART1 clock).
     * Stop 2 kills all oscillators except LSI/LSE.
     * HSI16 must be restarted explicitly so USART1 (clocked from
     * HSI16 for Stop-mode wakeup) resumes TX/RX immediately.
     */
    osc.OscillatorType  = RCC_OSCILLATORTYPE_MSI | RCC_OSCILLATORTYPE_HSI;
    osc.MSIState        = RCC_MSI_ON;
    osc.MSIClockRange   = RCC_MSIRANGE_6;
    osc.MSICalibrationValue = RCC_MSICALIBRATION_DEFAULT;
    osc.HSIState        = RCC_HSI_ON;
    osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    osc.PLL.PLLState    = RCC_PLL_ON;
    osc.PLL.PLLSource   = RCC_PLLSOURCE_MSI;
    osc.PLL.PLLM        = 1;
    osc.PLL.PLLN        = 60;
    osc.PLL.PLLR        = RCC_PLLR_DIV2;
    osc.PLL.PLLP        = RCC_PLLP_DIV7;
    osc.PLL.PLLQ        = RCC_PLLQ_DIV4;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
        while (1) { /* Recovery error */ }
    }

    clk.ClockType       = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                          RCC_CLOCKTYPE_PCLK1  | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource    = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider   = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider  = RCC_HCLK_DIV1;
    clk.APB2CLKDivider  = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_5) != HAL_OK) {
        while (1) { /* Recovery error */ }
    }
}

/* ================================================================== */
/*  7. Fault Handlers — crash diagnostics via blocking UART           */
/* ================================================================== */

/**
 * @brief HardFault C handler — receives the correct stack frame pointer.
 *
 * Called from the naked HardFault_Handler trampoline, which passes the
 * exception-frame pointer in R0 (first argument) BEFORE any compiler
 * prologue can corrupt MSP.
 *
 * Stack frame layout (Cortex-M4):
 *   sp[0]=R0, sp[1]=R1, sp[2]=R2, sp[3]=R3,
 *   sp[4]=R12, sp[5]=LR, sp[6]=PC, sp[7]=xPSR
 */
__attribute__((used))
static void HardFault_Handler_C(uint32_t *sp)
{
    volatile uint32_t cfsr  = SCB->CFSR;
    volatile uint32_t hfsr  = SCB->HFSR;
    volatile uint32_t bfar  = SCB->BFAR;
    volatile uint32_t pc    = sp[6];   /* stacked PC  (faulting instruction) */
    volatile uint32_t lr    = sp[5];   /* stacked LR  (caller return address) */
    volatile uint32_t r0    = sp[0];   /* stacked R0  (first arg at fault) */
    volatile uint32_t r12   = sp[4];   /* stacked R12 */
    volatile uint32_t msp_v = (uint32_t)sp;

    /* Print via blocking UART — no ISR needed */
    static const char msg[] = "\r\n!!! HARDFAULT !!!\r\n";
    for (const char *q = msg; *q; q++) {
        Anbo_Arch_UART_PutChar(*q);
    }

    /* Print registers as hex via blocking putchar */
    static const char hex[] = "0123456789ABCDEF";

    #define FAULT_PRINT_HEX(label, val) do { \
        for (const char *_p = (label); *_p; _p++) Anbo_Arch_UART_PutChar(*_p); \
        Anbo_Arch_UART_PutChar('0'); Anbo_Arch_UART_PutChar('x'); \
        for (int _i = 28; _i >= 0; _i -= 4) \
            Anbo_Arch_UART_PutChar(hex[((val) >> _i) & 0xF]); \
    } while (0)

    FAULT_PRINT_HEX("CFSR=", cfsr);
    FAULT_PRINT_HEX(" HFSR=", hfsr);
    FAULT_PRINT_HEX("\r\n PC=", pc);
    FAULT_PRINT_HEX("  LR=", lr);
    FAULT_PRINT_HEX("\r\n R0=", r0);
    FAULT_PRINT_HEX(" R12=", r12);
    FAULT_PRINT_HEX("\r\nBFAR=", bfar);
    FAULT_PRINT_HEX(" MSP=", msp_v);
    Anbo_Arch_UART_PutChar('\r');
    Anbo_Arch_UART_PutChar('\n');

    #undef FAULT_PRINT_HEX

    for (;;) { __NOP(); }  /* halt */
}

/**
 * @brief HardFault handler — naked trampoline.
 *
 * MUST be naked: no compiler prologue/epilogue, so MSP is untouched.
 * Reads the exception stack pointer and branches to the C handler.
 *
 * Also checks EXC_RETURN (in LR) to determine MSP vs PSP.
 */
__attribute__((naked))
void HardFault_Handler(void)
{
    __asm volatile (
        "TST    LR, #4          \n"   /* bit 2 of EXC_RETURN: 0=MSP, 1=PSP */
        "ITE    EQ              \n"
        "MRSEQ  R0, MSP         \n"
        "MRSNE  R0, PSP         \n"
        "B      HardFault_Handler_C \n"
    );
}

/**
 * @brief HAL EXTI callback — called from EXTI15_10_IRQHandler.
 *
 * Publishes ANBO_SIG_USER_BUTTON on the event bus so any subscriber
 * can react (e.g. toggle LED, change FSM state, log event).
 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == BSP_BTN_PIN) {
        Anbo_Event evt;
        evt.sig   = ANBO_SIG_USER_BUTTON;
        evt.param = NULL;
        Anbo_EBus_Publish(&evt);
    }
}
