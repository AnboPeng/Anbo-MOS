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
 * @file  b_l4s5i_i2c_drv.c
 * @brief B-L4S5I — I2C2 blocking driver (sensor bus).
 *
 * I2C2 on B-L4S5I-IOT01A:
 *   PB10 = SCL  (AF4)
 *   PB11 = SDA  (AF4)
 *   Speed: 400 kHz (Fast Mode)
 *   Devices: LSM6DSL (0x6A), LIS3MDL (0x1E), HTS221 (0x5F), LPS22HB (0x5D)
 *
 * Blocking HAL calls (timeout 100 ms).  Suitable for low-frequency
 * sensor register access.
 */

#include "b_l4s5i_i2c_drv.h"
#include "stm32l4xx_hal.h"

/* ================================================================== */
/*  Static handle                                                      */
/* ================================================================== */

static I2C_HandleTypeDef s_hi2c2;

/* ================================================================== */
/*  I2C timeout for blocking calls (ms)                                */
/* ================================================================== */

#define BSP_I2C2_TIMEOUT    100u

/* ================================================================== */
/*  Public API                                                         */
/* ================================================================== */

bool BSP_I2C2_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    /* Enable peripheral clocks */
    __HAL_RCC_I2C2_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* PB10 = I2C2_SCL, PB11 = I2C2_SDA — AF4, open-drain */
    gpio.Pin       = GPIO_PIN_10 | GPIO_PIN_11;
    gpio.Mode      = GPIO_MODE_AF_OD;
    gpio.Pull      = GPIO_NOPULL;       /* external pull-ups on board */
    gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF4_I2C2;
    HAL_GPIO_Init(GPIOB, &gpio);

    /* I2C2 configuration: 400 kHz Fast Mode */
    s_hi2c2.Instance              = I2C2;
    s_hi2c2.Init.Timing           = 0x00702991u;  /* 400 kHz @ 120 MHz PCLK1 */
    s_hi2c2.Init.OwnAddress1      = 0x00u;
    s_hi2c2.Init.AddressingMode   = I2C_ADDRESSINGMODE_7BIT;
    s_hi2c2.Init.DualAddressMode  = I2C_DUALADDRESS_DISABLE;
    s_hi2c2.Init.OwnAddress2      = 0x00u;
    s_hi2c2.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
    s_hi2c2.Init.GeneralCallMode  = I2C_GENERALCALL_DISABLE;
    s_hi2c2.Init.NoStretchMode    = I2C_NOSTRETCH_DISABLE;

    if (HAL_I2C_Init(&s_hi2c2) != HAL_OK) {
        return false;
    }

    /* Enable analog filter, disable digital filter */
    HAL_I2CEx_ConfigAnalogFilter(&s_hi2c2, I2C_ANALOGFILTER_ENABLE);
    HAL_I2CEx_ConfigDigitalFilter(&s_hi2c2, 0u);

    return true;
}

bool BSP_I2C2_WriteReg(uint8_t dev_addr, uint8_t reg,
                       const uint8_t *data, uint16_t len)
{
    uint16_t addr = (uint16_t)(dev_addr << 1u);
    HAL_StatusTypeDef rc;

    rc = HAL_I2C_Mem_Write(&s_hi2c2, addr, (uint16_t)reg,
                           I2C_MEMADD_SIZE_8BIT,
                           (uint8_t *)data, len,
                           BSP_I2C2_TIMEOUT);
    return (rc == HAL_OK);
}

bool BSP_I2C2_ReadReg(uint8_t dev_addr, uint8_t reg,
                      uint8_t *buf, uint16_t len)
{
    uint16_t addr = (uint16_t)(dev_addr << 1u);
    HAL_StatusTypeDef rc;

    rc = HAL_I2C_Mem_Read(&s_hi2c2, addr, (uint16_t)reg,
                          I2C_MEMADD_SIZE_8BIT,
                          buf, len,
                          BSP_I2C2_TIMEOUT);
    return (rc == HAL_OK);
}

bool BSP_I2C2_IsReady(uint8_t dev_addr)
{
    uint16_t addr = (uint16_t)(dev_addr << 1u);
    HAL_StatusTypeDef rc;

    rc = HAL_I2C_IsDeviceReady(&s_hi2c2, addr, 3u, BSP_I2C2_TIMEOUT);
    return (rc == HAL_OK);
}
