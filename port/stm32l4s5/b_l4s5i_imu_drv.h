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
 * @file  b_l4s5i_imu_drv.h
 * @brief B-L4S5I — LSM6DSL IMU driver (accelerometer + gyroscope).
 *
 * The B-L4S5I-IOT01A board mounts an ST LSM6DSL 6-axis IMU on the
 * I2C2 bus (PB10-SCL, PB11-SDA) at address 0x6A (SA0 = GND).
 * INT1 is routed to PD11 (active-high, directly connected).
 *
 * This driver provides:
 *   - Device identification (WHO_AM_I check)
 *   - Configurable ODR and full-scale range for accel & gyro
 *   - FIFO continuous mode with configurable watermark threshold
 *   - INT1 routed to FIFO watermark (EXTI on PD11)
 *   - Blocking readout of accel (3-axis, mg) and gyro (3-axis, mdps)
 *   - Bulk FIFO read for efficient data retrieval
 *
 * Data flow:
 *   LSM6DSL samples at ODR → FIFO accumulates accel+gyro pairs
 *   → FIFO level ≥ watermark → INT1 pulses (PD11 EXTI)
 *   → ISR publishes signal → main-loop reads FIFO via I2C burst
 */

#ifndef B_L4S5I_IMU_DRV_H
#define B_L4S5I_IMU_DRV_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================== */
/*  LSM6DSL I2C Address                                                */
/* ================================================================== */

/** 7-bit address when SA0/SDO pin is connected to GND (B-L4S5I board) */
#define LSM6DSL_I2C_ADDR        0x6Au

/** WHO_AM_I register expected value */
#define LSM6DSL_WHO_AM_I_VAL    0x6Au

/* ================================================================== */
/*  Output Data Rate                                                   */
/* ================================================================== */

typedef enum {
    LSM6DSL_ODR_OFF    = 0x00u,
    LSM6DSL_ODR_12_5HZ = 0x01u,
    LSM6DSL_ODR_26HZ   = 0x02u,
    LSM6DSL_ODR_52HZ   = 0x03u,
    LSM6DSL_ODR_104HZ  = 0x04u,
    LSM6DSL_ODR_208HZ  = 0x05u,
    LSM6DSL_ODR_416HZ  = 0x06u,
    LSM6DSL_ODR_833HZ  = 0x07u,
    LSM6DSL_ODR_1660HZ = 0x08u,
} LSM6DSL_ODR;

/* ================================================================== */
/*  Accelerometer Full-Scale                                           */
/* ================================================================== */

typedef enum {
    LSM6DSL_XL_FS_2G   = 0x00u,
    LSM6DSL_XL_FS_4G   = 0x02u,
    LSM6DSL_XL_FS_8G   = 0x03u,
    LSM6DSL_XL_FS_16G  = 0x01u,
} LSM6DSL_XL_FS;

/* ================================================================== */
/*  Gyroscope Full-Scale                                               */
/* ================================================================== */

typedef enum {
    LSM6DSL_GY_FS_250DPS   = 0x00u,
    LSM6DSL_GY_FS_500DPS   = 0x01u,
    LSM6DSL_GY_FS_1000DPS  = 0x02u,
    LSM6DSL_GY_FS_2000DPS  = 0x03u,
} LSM6DSL_GY_FS;

/* ================================================================== */
/*  3-axis raw / scaled data containers                                */
/* ================================================================== */

/** Accelerometer reading in milli-g (1 g = 1000 mg) */
typedef struct {
    int32_t x;      /**< X-axis [mg] */
    int32_t y;      /**< Y-axis [mg] */
    int32_t z;      /**< Z-axis [mg] */
} LSM6DSL_AccelData;

/** Gyroscope reading in milli-degrees-per-second */
typedef struct {
    int32_t x;      /**< X-axis [mdps] */
    int32_t y;      /**< Y-axis [mdps] */
    int32_t z;      /**< Z-axis [mdps] */
} LSM6DSL_GyroData;

/** Combined accel + gyro sample (one FIFO set) */
typedef struct {
    LSM6DSL_AccelData accel;
    LSM6DSL_GyroData  gyro;
} LSM6DSL_Sample;

/* ================================================================== */
/*  API — Initialisation                                               */
/* ================================================================== */

/**
 * @brief  Initialise the LSM6DSL IMU with FIFO + INT1.
 *
 * Verifies WHO_AM_I, performs software reset, then configures:
 *   - Block Data Update (BDU) enabled
 *   - Accelerometer:  specified ODR and full-scale
 *   - Gyroscope:      specified ODR and full-scale
 *   - FIFO: continuous mode, accel+gyro decimation = none
 *   - FIFO watermark threshold
 *   - INT1 → FIFO watermark (PD11 EXTI, active-high)
 *
 * @param  xl_odr       Accelerometer output data rate.
 * @param  xl_fs        Accelerometer full-scale.
 * @param  gy_odr       Gyroscope output data rate.
 * @param  gy_fs        Gyroscope full-scale.
 * @param  fifo_wtm     FIFO watermark threshold in sample-sets.
 *                      Each set = 6 words (3 gyro + 3 accel).
 *                      0 = disable FIFO (register-readout mode).
 * @retval true         Init OK, WHO_AM_I verified.
 * @retval false        Device not found or init error.
 */
bool BSP_IMU_Init(LSM6DSL_ODR  xl_odr, LSM6DSL_XL_FS xl_fs,
                  LSM6DSL_ODR  gy_odr, LSM6DSL_GY_FS gy_fs,
                  uint16_t     fifo_wtm);

/**
 * @brief  Initialise PD11 as EXTI rising-edge interrupt for INT1.
 *
 * Call AFTER BSP_IMU_Init() has configured INT1 routing inside
 * the LSM6DSL.  This function only configures the STM32 GPIO + EXTI.
 * PD11 shares EXTI15_10_IRQn with User Button (PC13).
 */
void BSP_IMU_INT1_Init(void);

/* ================================================================== */
/*  API — Register-level readout (single sample)                       */
/* ================================================================== */

/**
 * @brief  Read accelerometer data from output registers (blocking).
 * @param  out  Receives X/Y/Z in milli-g.
 * @retval true  Read OK.
 * @retval false I2C error.
 */
bool BSP_IMU_ReadAccel(LSM6DSL_AccelData *out);

/**
 * @brief  Read gyroscope data from output registers (blocking).
 * @param  out  Receives X/Y/Z in milli-degrees-per-second.
 * @retval true  Read OK.
 * @retval false I2C error.
 */
bool BSP_IMU_ReadGyro(LSM6DSL_GyroData *out);

/* ================================================================== */
/*  API — FIFO readout                                                 */
/* ================================================================== */

/**
 * @brief  Get the current number of unread 16-bit words in the FIFO.
 *
 * One accel+gyro sample-set = 6 words (3 gyro + 3 accel).
 * Divide the return value by 6 to get the number of complete sets.
 *
 * @return Number of unread words (0–4096), or 0 on I2C error.
 */
uint16_t BSP_IMU_FIFO_GetLevel(void);

/**
 * @brief  Read complete accel+gyro sample-sets from the FIFO.
 *
 * Reads up to @p max_sets complete sample-sets.  Each set is
 * 12 bytes of raw data (gyro XYZ + accel XYZ interleaved in FIFO
 * pattern) converted to physical units (mg / mdps).
 *
 * @param  out       Array of LSM6DSL_Sample to receive data.
 * @param  max_sets  Maximum number of sets to read.
 * @return Number of sample-sets actually read (0 on error).
 */
uint16_t BSP_IMU_FIFO_Read(LSM6DSL_Sample *out, uint16_t max_sets);

/* ================================================================== */
/*  API — Misc                                                         */
/* ================================================================== */

/**
 * @brief  Read raw temperature from LSM6DSL.
 * @return Temperature in 0.1 °C units (e.g. 250 = 25.0 °C).
 *         Returns INT32_MIN on I2C error.
 */
int32_t BSP_IMU_ReadTemp(void);

/**
 * @brief  Power-down both accelerometer and gyroscope.
 */
void BSP_IMU_PowerDown(void);

/**
 * @brief  Configure LSM6DSL for low-power wake-up detection.
 *
 * Gyro off, FIFO off, accel at 12.5 Hz ±4 g.  The LSM6DSL wake-up
 * function triggers INT1 when acceleration exceeds the threshold.
 * INT1 (PD11 EXTI) can then wake the STM32 from Stop 2.
 *
 * @param  thresh_6bit  Wake-up threshold [0..63].
 *                      1 LSB ≈ 62.5 mg at ±4 g full-scale.
 *                      E.g. 1 = 62 mg, 2 = 125 mg.
 */
void BSP_IMU_ConfigWakeup(uint8_t thresh_6bit);

/**
 * @brief  Read and clear the LSM6DSL WAKE_UP_SRC register.
 *
 * Reading this register clears the latched INT1 wake-up signal.
 * Bit 3 = WU_IA (wake-up event detected).
 *
 * @param  src  Receives the raw register value.
 * @retval true  Read OK.
 * @retval false I2C error.
 */
bool BSP_IMU_ReadWakeUpSrc(uint8_t *src);

#ifdef __cplusplus
}
#endif

#endif /* B_L4S5I_IMU_DRV_H */
