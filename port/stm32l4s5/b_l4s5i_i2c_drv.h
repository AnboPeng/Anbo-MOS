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
 * @file  b_l4s5i_i2c_drv.h
 * @brief B-L4S5I — I2C2 blocking driver for on-board sensors.
 *
 * B-L4S5I-IOT01A I2C2 bus (shared by LSM6DSL, LIS3MDL, HTS221, LPS22HB):
 *   SCL : PB10  (AF4)
 *   SDA : PB11  (AF4)
 *
 * This is a thin blocking wrapper around HAL_I2C.  Sensor drivers
 * (e.g. LSM6DSL) call BSP_I2C2_ReadReg / BSP_I2C2_WriteReg to
 * access device registers.  All I/O is polled — suitable for
 * infrequent, short transfers typical of MEMS sensor configuration
 * and data readout.
 */

#ifndef B_L4S5I_I2C_DRV_H
#define B_L4S5I_I2C_DRV_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================== */
/*  API                                                                */
/* ================================================================== */

/**
 * @brief  Initialise I2C2 peripheral (PB10-SCL, PB11-SDA, 400 kHz).
 * @retval true   Success.
 * @retval false  HAL init failed.
 */
bool BSP_I2C2_Init(void);

/**
 * @brief  Write one or more bytes to a device register (blocking).
 * @param  dev_addr  7-bit I2C slave address (NOT left-shifted).
 * @param  reg       Register address (8-bit).
 * @param  data      Source buffer.
 * @param  len       Number of bytes to write.
 * @retval true      Success.
 * @retval false     HAL error or timeout.
 */
bool BSP_I2C2_WriteReg(uint8_t dev_addr, uint8_t reg,
                       const uint8_t *data, uint16_t len);

/**
 * @brief  Read one or more bytes from a device register (blocking).
 * @param  dev_addr  7-bit I2C slave address (NOT left-shifted).
 * @param  reg       Register address (8-bit).
 * @param  buf       Destination buffer.
 * @param  len       Number of bytes to read.
 * @retval true      Success.
 * @retval false     HAL error or timeout.
 */
bool BSP_I2C2_ReadReg(uint8_t dev_addr, uint8_t reg,
                      uint8_t *buf, uint16_t len);

/**
 * @brief  Check whether a device acknowledges on the bus.
 * @param  dev_addr  7-bit I2C slave address.
 * @retval true      Device responded with ACK.
 * @retval false     No ACK (device absent or bus error).
 */
bool BSP_I2C2_IsReady(uint8_t dev_addr);

#ifdef __cplusplus
}
#endif

#endif /* B_L4S5I_I2C_DRV_H */
