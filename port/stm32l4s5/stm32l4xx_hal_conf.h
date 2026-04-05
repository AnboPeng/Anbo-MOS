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
 * @file  stm32l4xx_hal_conf.h
 * @brief STM32L4 HAL module selection for B-L4S5I-IOT01A / Anbo project.
 *
 * Only the modules actually used by the port layer are enabled.
 * Keeping unused modules disabled reduces compile time and code size.
 */

#ifndef STM32L4xx_HAL_CONF_H
#define STM32L4xx_HAL_CONF_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================== */
/*  Module Selection                                                   */
/* ================================================================== */

#define HAL_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
#define HAL_DMA_MODULE_ENABLED
#define HAL_FLASH_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_PWR_MODULE_ENABLED
#define HAL_PWR_EX_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_RCC_EX_MODULE_ENABLED
#define HAL_UART_MODULE_ENABLED

/* Uncomment to enable additional modules as needed */
#define HAL_ADC_MODULE_ENABLED
/* #define HAL_CAN_MODULE_ENABLED        */
/* #define HAL_COMP_MODULE_ENABLED       */
/* #define HAL_CRC_MODULE_ENABLED        */
/* #define HAL_CRYP_MODULE_ENABLED       */
/* #define HAL_DAC_MODULE_ENABLED        */
#define HAL_EXTI_MODULE_ENABLED
/* #define HAL_I2C_MODULE_ENABLED        */
/* #define HAL_IRDA_MODULE_ENABLED       */
#define HAL_IWDG_MODULE_ENABLED
#define HAL_LPTIM_MODULE_ENABLED
/* #define HAL_OPAMP_MODULE_ENABLED      */
#define HAL_OSPI_MODULE_ENABLED
/* #define HAL_PCD_MODULE_ENABLED        */
/* #define HAL_QSPI_MODULE_ENABLED       */
/* #define HAL_RNG_MODULE_ENABLED        */
#define HAL_RTC_MODULE_ENABLED
/* #define HAL_SAI_MODULE_ENABLED        */
/* #define HAL_SD_MODULE_ENABLED         */
/* #define HAL_SMARTCARD_MODULE_ENABLED  */
/* #define HAL_SPI_MODULE_ENABLED        */
/* #define HAL_TIM_MODULE_ENABLED        */
/* #define HAL_TSC_MODULE_ENABLED        */
/* #define HAL_USART_MODULE_ENABLED      */
/* #define HAL_WWDG_MODULE_ENABLED       */

/* ================================================================== */
/*  Oscillator Values (adapt to board crystal / config)                */
/* ================================================================== */

/** HSE crystal frequency (B-L4S5I-IOT01A does NOT mount HSE by default) */
#if !defined(HSE_VALUE)
#define HSE_VALUE               8000000U        /* 8 MHz placeholder */
#endif

#if !defined(HSE_STARTUP_TIMEOUT)
#define HSE_STARTUP_TIMEOUT     100U            /* ms */
#endif

/** MSI default range after reset */
#if !defined(MSI_VALUE)
#define MSI_VALUE               4000000U        /* 4 MHz (Range 6) */
#endif

/** HSI (16 MHz internal RC) */
#if !defined(HSI_VALUE)
#define HSI_VALUE               16000000U
#endif

/** HSI48 for USB / RNG */
#if !defined(HSI48_VALUE)
#define HSI48_VALUE             48000000U
#endif

/** LSE (32.768 kHz) */
#if !defined(LSE_VALUE)
#define LSE_VALUE               32768U
#endif

#if !defined(LSE_STARTUP_TIMEOUT)
#define LSE_STARTUP_TIMEOUT     5000U           /* ms */
#endif

/** LSI (~32 kHz internal RC) */
#if !defined(LSI_VALUE)
#define LSI_VALUE               32000U
#endif

/** External clock for SAI1 */
#if !defined(EXTERNAL_SAI1_CLOCK_VALUE)
#define EXTERNAL_SAI1_CLOCK_VALUE   48000U
#endif

/** External clock for SAI2 */
#if !defined(EXTERNAL_SAI2_CLOCK_VALUE)
#define EXTERNAL_SAI2_CLOCK_VALUE   48000U
#endif

/* ================================================================== */
/*  System Configuration                                               */
/* ================================================================== */

/** NVIC preemption priority bits (STM32L4 uses 4 bits) */
#define __NVIC_PRIO_BITS            4U

/** SysTick calibration: use default (processor calib register) */
#define TICK_INT_PRIORITY           ((1U << __NVIC_PRIO_BITS) - 1U)    /* Lowest */

/** Prefetch / caches */
#define PREFETCH_ENABLE             1U
#define INSTRUCTION_CACHE_ENABLE    1U
#define DATA_CACHE_ENABLE           1U

/* ================================================================== */
/*  HAL Peripherals — Timeout defaults                                 */
/* ================================================================== */

#define HAL_MAX_DELAY               0xFFFFFFFFU
#define USE_SPI_CRC                 0U

/* ================================================================== */
/*  Include guards for enabled HAL modules                             */
/* ================================================================== */

#ifdef HAL_RCC_MODULE_ENABLED
  #include "stm32l4xx_hal_rcc.h"
  #include "stm32l4xx_hal_rcc_ex.h"
#endif

#ifdef HAL_GPIO_MODULE_ENABLED
  #include "stm32l4xx_hal_gpio.h"
  #include "stm32l4xx_hal_gpio_ex.h"
#endif

#ifdef HAL_DMA_MODULE_ENABLED
  #include "stm32l4xx_hal_dma.h"
#endif

#ifdef HAL_CORTEX_MODULE_ENABLED
  #include "stm32l4xx_hal_cortex.h"
#endif

#ifdef HAL_FLASH_MODULE_ENABLED
  #include "stm32l4xx_hal_flash.h"
  #include "stm32l4xx_hal_flash_ex.h"
  #include "stm32l4xx_hal_flash_ramfunc.h"
#endif

#ifdef HAL_PWR_MODULE_ENABLED
  #include "stm32l4xx_hal_pwr.h"
  #include "stm32l4xx_hal_pwr_ex.h"
#endif

#ifdef HAL_UART_MODULE_ENABLED
  #include "stm32l4xx_hal_uart.h"
  #include "stm32l4xx_hal_uart_ex.h"
#endif

#ifdef HAL_EXTI_MODULE_ENABLED
  #include "stm32l4xx_hal_exti.h"
#endif

#ifdef HAL_ADC_MODULE_ENABLED
  #include "stm32l4xx_hal_adc.h"
  #include "stm32l4xx_hal_adc_ex.h"
#endif

#ifdef HAL_I2C_MODULE_ENABLED
  #include "stm32l4xx_hal_i2c.h"
  #include "stm32l4xx_hal_i2c_ex.h"
#endif

#ifdef HAL_SPI_MODULE_ENABLED
  #include "stm32l4xx_hal_spi.h"
  #include "stm32l4xx_hal_spi_ex.h"
#endif

#ifdef HAL_TIM_MODULE_ENABLED
  #include "stm32l4xx_hal_tim.h"
  #include "stm32l4xx_hal_tim_ex.h"
#endif

#ifdef HAL_RTC_MODULE_ENABLED
  #include "stm32l4xx_hal_rtc.h"
  #include "stm32l4xx_hal_rtc_ex.h"
#endif

#ifdef HAL_IWDG_MODULE_ENABLED
  #include "stm32l4xx_hal_iwdg.h"
#endif

#ifdef HAL_LPTIM_MODULE_ENABLED
  #include "stm32l4xx_hal_lptim.h"
#endif

#ifdef HAL_OSPI_MODULE_ENABLED
  #include "stm32l4xx_hal_ospi.h"
#endif

#ifdef HAL_RNG_MODULE_ENABLED
  #include "stm32l4xx_hal_rng.h"
#endif

#ifdef HAL_PCD_MODULE_ENABLED
  #include "stm32l4xx_hal_pcd.h"
  #include "stm32l4xx_hal_pcd_ex.h"
#endif

/* ================================================================== */
/*  Assert macro                                                       */
/* ================================================================== */

/* #define USE_FULL_ASSERT  1U */

#ifdef USE_FULL_ASSERT
  #define assert_param(expr) \
      ((expr) ? (void)0 : assert_failed((uint8_t *)__FILE__, __LINE__))
  void assert_failed(uint8_t *file, uint32_t line);
#else
  #define assert_param(expr)  ((void)0U)
#endif

#ifdef __cplusplus
}
#endif

#endif /* STM32L4xx_HAL_CONF_H */
