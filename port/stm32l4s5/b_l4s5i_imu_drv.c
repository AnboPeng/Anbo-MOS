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
 * @file  b_l4s5i_imu_drv.c
 * @brief B-L4S5I — LSM6DSL IMU driver with FIFO + INT1 support.
 *
 * Register map reference: ST AN5040 / LSM6DSL datasheet (DocID029587).
 *
 * Data flow:
 *   1. LSM6DSL samples accel+gyro at the configured ODR.
 *   2. Samples accumulate in the 8 KB FIFO (continuous mode).
 *   3. When FIFO level ≥ watermark, INT1 (PD11) pulses high.
 *   4. STM32 EXTI ISR publishes ANBO_SIG_IMU_INT1 on the event bus.
 *   5. Main-loop subscriber reads FIFO in burst via I2C (blocking).
 *
 * FIFO word layout (continuous mode, gyro + accel both un-decimated):
 *   Word 0 = GX_L | GX_H   (gyro X)
 *   Word 1 = GY_L | GY_H   (gyro Y)
 *   Word 2 = GZ_L | GZ_H   (gyro Z)
 *   Word 3 = AX_L | AX_H   (accel X)
 *   Word 4 = AY_L | AY_H   (accel Y)
 *   Word 5 = AZ_L | AZ_H   (accel Z)
 *   → 6 words = 12 bytes per sample-set.
 *
 * Sensitivity values (from datasheet Table 2 & Table 3):
 *   Accel:  ±2g→0.061, ±4g→0.122, ±8g→0.244, ±16g→0.488 mg/LSB
 *   Gyro:   ±250→8.75, ±500→17.50, ±1000→35.00, ±2000→70.00 mdps/LSB
 *
 * Integer-only arithmetic — no floating-point in the driver layer.
 */

#include "b_l4s5i_imu_drv.h"
#include "b_l4s5i_i2c_drv.h"
#include "anbo_log.h"
#include "stm32l4xx_hal.h"

/* ================================================================== */
/*  LSM6DSL Register Addresses                                         */
/* ================================================================== */

#define REG_FIFO_CTRL1      0x06u   /* FIFO watermark threshold [7:0] */
#define REG_FIFO_CTRL2      0x07u   /* FIFO watermark [10:8] + timer */
#define REG_FIFO_CTRL3      0x08u   /* Gyro/XL FIFO decimation */
#define REG_FIFO_CTRL4      0x09u   /* FIFO mode + ODR */
#define REG_FIFO_CTRL5      0x0Au   /* FIFO ODR + mode */
#define REG_INT1_CTRL       0x0Du   /* INT1 pin control */
#define REG_INT2_CTRL       0x0Eu   /* INT2 pin control */
#define REG_WHO_AM_I        0x0Fu
#define REG_CTRL1_XL        0x10u   /* Accel control */
#define REG_CTRL2_G         0x11u   /* Gyro control */
#define REG_CTRL3_C         0x12u   /* Control register 3 */
#define REG_CTRL4_C         0x13u   /* Control register 4 */
#define REG_CTRL5_C         0x14u   /* Control register 5 */
#define REG_CTRL6_C         0x15u   /* Control register 6 */
#define REG_CTRL7_G         0x16u   /* Gyro control 7 */
#define REG_CTRL8_XL        0x17u   /* Accel control 8 */
#define REG_CTRL10_C        0x19u   /* Timer / pedo / tilt enable */
#define REG_WAKE_UP_DUR     0x5Cu   /* Wake-up duration */
#define REG_WAKE_UP_THS     0x5Bu   /* Wake-up threshold */
#define REG_WAKE_UP_SRC     0x1Bu   /* Wake-up source (read to clear latch) */
#define REG_MD1_CFG         0x5Eu   /* Functions routing to INT1 */
#define REG_TAP_CFG         0x58u   /* Interrupt enable / latch config */
#define REG_STATUS_REG      0x1Eu
#define REG_OUT_TEMP_L      0x20u
#define REG_OUT_TEMP_H      0x21u
#define REG_OUTX_L_G        0x22u   /* Gyro X low byte */
#define REG_OUTX_L_XL       0x28u   /* Accel X low byte */
#define REG_FIFO_STATUS1    0x3Au   /* FIFO status: word count [7:0] */
#define REG_FIFO_STATUS2    0x3Bu   /* FIFO status: word count [10:8] + flags */
#define REG_FIFO_DATA_OUT_L 0x3Eu   /* FIFO data output low byte */
#define REG_FIFO_DATA_OUT_H 0x3Fu   /* FIFO data output high byte */

/* STATUS_REG bits */
#define STATUS_XLDA         (1u << 0)   /* Accel data available */
#define STATUS_GDA          (1u << 1)   /* Gyro data available */
#define STATUS_TDA          (1u << 2)   /* Temp data available */

/* CTRL3_C bits */
#define CTRL3_C_SW_RESET    (1u << 0)
#define CTRL3_C_BDU         (1u << 6)
#define CTRL3_C_IF_INC      (1u << 2)   /* Auto-increment address */

/* FIFO_CTRL3: decimation = no decimation (001 for both) */
#define FIFO_CTRL3_DEC_XL_NONE   0x01u  /* Accel decimation = 1 */
#define FIFO_CTRL3_DEC_G_NONE    0x08u  /* Gyro decimation = 1 */

/* FIFO_CTRL5 bits */
#define FIFO_MODE_CONTINUOUS     0x06u  /* Continuous mode (overwrite oldest) */

/* INT1_CTRL bits */
#define INT1_FTH            (1u << 3)   /* FIFO threshold on INT1 */

/* FIFO_STATUS2 bits */
#define FIFO_EMPTY          (1u << 4)
#define FIFO_FULL           (1u << 5)
#define FIFO_OVER_RUN       (1u << 6)
#define FIFO_WTM            (1u << 7)   /* Watermark reached */

/* Words per sample-set: 3 gyro + 3 accel = 6 16-bit words */
#define FIFO_WORDS_PER_SET  6u

/* ================================================================== */
/*  Module state                                                       */
/* ================================================================== */

static LSM6DSL_XL_FS s_xl_fs;
static LSM6DSL_GY_FS s_gy_fs;

/* ================================================================== */
/*  Internal helpers                                                   */
/* ================================================================== */

static bool reg_write(uint8_t reg, uint8_t val)
{
    return BSP_I2C2_WriteReg(LSM6DSL_I2C_ADDR, reg, &val, 1u);
}

static bool reg_read(uint8_t reg, uint8_t *val)
{
    return BSP_I2C2_ReadReg(LSM6DSL_I2C_ADDR, reg, val, 1u);
}

static bool reg_read_multi(uint8_t reg, uint8_t *buf, uint16_t len)
{
    return BSP_I2C2_ReadReg(LSM6DSL_I2C_ADDR, reg, buf, len);
}

/**
 * @brief Convert raw 16-bit accel value to milli-g (integer).
 *
 * Sensitivity is stored as an integer numerator with implicit /1000
 * to avoid float:
 *   ±2 g  → 61,  ±4 g → 122,  ±8 g → 244,  ±16 g → 488
 *   result_mg = raw * sensitivity / 1000
 */
static int32_t raw_to_mg(int16_t raw)
{
    uint32_t sens;
    switch (s_xl_fs) {
    case LSM6DSL_XL_FS_2G:   sens = 61u;  break;
    case LSM6DSL_XL_FS_4G:   sens = 122u; break;
    case LSM6DSL_XL_FS_8G:   sens = 244u; break;
    case LSM6DSL_XL_FS_16G:  sens = 488u; break;
    default:                  sens = 61u;  break;
    }
    return ((int32_t)raw * (int32_t)sens) / 1000;
}

/**
 * @brief Convert raw 16-bit gyro value to milli-dps (integer).
 *
 *   ±250 dps  → 8750,  ±500 → 17500,  ±1000 → 35000,  ±2000 → 70000
 *   result_mdps = raw * sensitivity / 1000
 */
static int32_t raw_to_mdps(int16_t raw)
{
    uint32_t sens;
    switch (s_gy_fs) {
    case LSM6DSL_GY_FS_250DPS:   sens = 8750u;  break;
    case LSM6DSL_GY_FS_500DPS:   sens = 17500u; break;
    case LSM6DSL_GY_FS_1000DPS:  sens = 35000u; break;
    case LSM6DSL_GY_FS_2000DPS:  sens = 70000u; break;
    default:                     sens = 8750u;  break;
    }
    return ((int32_t)raw * (int32_t)sens) / 1000;
}

/* ================================================================== */
/*  Public API                                                         */
/* ================================================================== */

bool BSP_IMU_Init(LSM6DSL_ODR  xl_odr, LSM6DSL_XL_FS xl_fs,
                  LSM6DSL_ODR  gy_odr, LSM6DSL_GY_FS gy_fs,
                  uint16_t     fifo_wtm)
{
    uint8_t who;

    /* Verify device is present */
    if (!BSP_I2C2_IsReady(LSM6DSL_I2C_ADDR)) {
        ANBO_LOGE("IMU: LSM6DSL not found on I2C2 addr=0x%02x",
                  (unsigned)LSM6DSL_I2C_ADDR);
        return false;
    }

    /* Read WHO_AM_I */
    if (!reg_read(REG_WHO_AM_I, &who)) {
        ANBO_LOGE("IMU: WHO_AM_I read failed addr=0x%02x",
                  (unsigned)LSM6DSL_I2C_ADDR);
        return false;
    }
    if (who != LSM6DSL_WHO_AM_I_VAL) {
        ANBO_LOGE("IMU: WHO_AM_I=0x%02x (expect 0x%02x)",
                  (unsigned)who, (unsigned)LSM6DSL_WHO_AM_I_VAL);
        return false;
    }

    /* Software reset */
    if (!reg_write(REG_CTRL3_C, CTRL3_C_SW_RESET)) {
        return false;
    }
    /* Wait for reset to complete (bit auto-clears) */
    {
        uint8_t ctrl3;
        uint32_t retry = 100u;
        do {
            if (!reg_read(REG_CTRL3_C, &ctrl3)) {
                return false;
            }
        } while ((ctrl3 & CTRL3_C_SW_RESET) && (--retry > 0u));
        if (retry == 0u) {
            ANBO_LOGE("IMU: SW_RESET timeout retry=%u", (unsigned)retry);
            return false;
        }
    }

    /* Enable BDU (block data update) + IF_INC (auto-increment) */
    if (!reg_write(REG_CTRL3_C, CTRL3_C_BDU | CTRL3_C_IF_INC)) {
        return false;
    }

    /* Configure accelerometer: ODR | FS */
    {
        uint8_t ctrl1 = (uint8_t)((xl_odr << 4u) | (xl_fs << 2u));
        if (!reg_write(REG_CTRL1_XL, ctrl1)) {
            return false;
        }
    }

    /* Configure gyroscope: ODR | FS */
    {
        uint8_t ctrl2 = (uint8_t)((gy_odr << 4u) | (gy_fs << 2u));
        if (!reg_write(REG_CTRL2_G, ctrl2)) {
            return false;
        }
    }

    /* Store full-scale for scaling calculations */
    s_xl_fs = xl_fs;
    s_gy_fs = gy_fs;

    /* ---- FIFO configuration (if watermark > 0) ---- */
    if (fifo_wtm > 0u) {
        /*
         * Watermark is in FIFO words (16-bit).
         * Each accel+gyro set = 6 words (3 gyro + 3 accel).
         * Convert from sample-sets to words for the threshold register.
         */
        uint16_t wtm_words = fifo_wtm * FIFO_WORDS_PER_SET;
        if (wtm_words > 4095u) {
            wtm_words = 4095u;  /* 12-bit field (but FIFO is 4096 max) */
        }

        /* FIFO_CTRL1: watermark threshold [7:0] */
        if (!reg_write(REG_FIFO_CTRL1, (uint8_t)(wtm_words & 0xFFu))) {
            return false;
        }

        /* FIFO_CTRL2: watermark threshold [10:8] (bits [2:0]) */
        if (!reg_write(REG_FIFO_CTRL2, (uint8_t)((wtm_words >> 8u) & 0x07u))) {
            return false;
        }

        /* FIFO_CTRL3: no decimation for both accel and gyro */
        if (!reg_write(REG_FIFO_CTRL3,
                       FIFO_CTRL3_DEC_G_NONE | FIFO_CTRL3_DEC_XL_NONE)) {
            return false;
        }

        /*
         * FIFO_CTRL5: continuous mode + FIFO ODR.
         * FIFO ODR should match the higher of accel/gyro ODR.
         * Use the accel ODR value (bits [6:3]) | continuous mode (bits [2:0]).
         */
        {
            uint8_t fifo_odr = (xl_odr > gy_odr) ? xl_odr : gy_odr;
            uint8_t ctrl5 = (uint8_t)((fifo_odr << 3u) | FIFO_MODE_CONTINUOUS);
            if (!reg_write(REG_FIFO_CTRL5, ctrl5)) {
                return false;
            }
        }

        /* INT1_CTRL: route FIFO threshold (watermark) to INT1 pin */
        if (!reg_write(REG_INT1_CTRL, INT1_FTH)) {
            return false;
        }

        ANBO_LOGI("IMU: FIFO continuous, WTM=%u sets (%u words), INT1 on FTH",
                  (unsigned)fifo_wtm, (unsigned)wtm_words);
    }

    ANBO_LOGI("IMU: LSM6DSL OK (WHO=0x%02x, XL_ODR=%u, GY_ODR=%u)",
              (unsigned)who, (unsigned)xl_odr, (unsigned)gy_odr);
    return true;
}

bool BSP_IMU_ReadAccel(LSM6DSL_AccelData *out)
{
    uint8_t buf[6];

    if (!reg_read_multi(REG_OUTX_L_XL, buf, 6u)) {
        return false;
    }

    int16_t raw_x = (int16_t)((uint16_t)buf[1] << 8u | buf[0]);
    int16_t raw_y = (int16_t)((uint16_t)buf[3] << 8u | buf[2]);
    int16_t raw_z = (int16_t)((uint16_t)buf[5] << 8u | buf[4]);

    out->x = raw_to_mg(raw_x);
    out->y = raw_to_mg(raw_y);
    out->z = raw_to_mg(raw_z);

    return true;
}

bool BSP_IMU_ReadGyro(LSM6DSL_GyroData *out)
{
    uint8_t buf[6];

    if (!reg_read_multi(REG_OUTX_L_G, buf, 6u)) {
        return false;
    }

    int16_t raw_x = (int16_t)((uint16_t)buf[1] << 8u | buf[0]);
    int16_t raw_y = (int16_t)((uint16_t)buf[3] << 8u | buf[2]);
    int16_t raw_z = (int16_t)((uint16_t)buf[5] << 8u | buf[4]);

    out->x = raw_to_mdps(raw_x);
    out->y = raw_to_mdps(raw_y);
    out->z = raw_to_mdps(raw_z);

    return true;
}

/* ================================================================== */
/*  INT1 GPIO init (PD11 = EXTI line 11)                               */
/* ================================================================== */

void BSP_IMU_INT1_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOD_CLK_ENABLE();

    /*
     * PD11 = LSM6DSL INT1 (active-high, push-pull on the sensor side).
     * Configure as rising-edge EXTI with pull-down to avoid phantom
     * triggers when the sensor is in reset / power-down.
     */
    gpio.Pin   = GPIO_PIN_11;
    gpio.Mode  = GPIO_MODE_IT_RISING;
    gpio.Pull  = GPIO_PULLDOWN;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOD, &gpio);

    /*
     * EXTI15_10 IRQ is already enabled for BSP_BTN (PC13).
     * Just ensure the priority is set (same as button — level 6).
     * The shared EXTI15_10_IRQHandler dispatches via HAL, which
     * calls HAL_GPIO_EXTI_Callback with the triggering pin.
     */
    HAL_NVIC_SetPriority(EXTI15_10_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

    ANBO_LOGI("IMU: INT1 on PD11 EXTI rising, IRQn=%u", (unsigned)EXTI15_10_IRQn);
}

/* ================================================================== */
/*  FIFO readout                                                       */
/* ================================================================== */

uint16_t BSP_IMU_FIFO_GetLevel(void)
{
    uint8_t buf[2];

    if (!reg_read_multi(REG_FIFO_STATUS1, buf, 2u)) {
        return 0u;
    }

    /* Word count spans STATUS1[7:0] and STATUS2[2:0] = 11 bits */
    uint16_t words = (uint16_t)buf[0] | (((uint16_t)buf[1] & 0x07u) << 8u);
    return words;
}

uint16_t BSP_IMU_FIFO_Read(LSM6DSL_Sample *out, uint16_t max_sets)
{
    uint16_t words = BSP_IMU_FIFO_GetLevel();
    uint16_t avail_sets = words / FIFO_WORDS_PER_SET;

    if (avail_sets == 0u) {
        return 0u;
    }
    if (avail_sets > max_sets) {
        avail_sets = max_sets;
    }

    /*
     * FIFO readout: each read of FIFO_DATA_OUT_L/H pops one 16-bit word.
     * With IF_INC enabled, reading 2 bytes from FIFO_DATA_OUT_L returns
     * one word and auto-increments the FIFO read pointer.
     *
     * FIFO pattern (gyro ungrouped before accel ungrouped):
     *   Word 0: GX,  Word 1: GY,  Word 2: GZ
     *   Word 3: AX,  Word 4: AY,  Word 5: AZ
     */
    for (uint16_t i = 0u; i < avail_sets; i++) {
        uint8_t raw[12];   /* 6 words × 2 bytes = 12 bytes */

        /*
         * Burst-read 12 bytes from FIFO_DATA_OUT_L.
         * IF_INC causes the FIFO to pop 6 consecutive words.
         */
        if (!reg_read_multi(REG_FIFO_DATA_OUT_L, raw, 12u)) {
            return i;   /* return what we got so far */
        }

        /* Gyro: words 0–2 */
        int16_t gx = (int16_t)((uint16_t)raw[1]  << 8u | raw[0]);
        int16_t gy = (int16_t)((uint16_t)raw[3]  << 8u | raw[2]);
        int16_t gz = (int16_t)((uint16_t)raw[5]  << 8u | raw[4]);
        /* Accel: words 3–5 */
        int16_t ax = (int16_t)((uint16_t)raw[7]  << 8u | raw[6]);
        int16_t ay = (int16_t)((uint16_t)raw[9]  << 8u | raw[8]);
        int16_t az = (int16_t)((uint16_t)raw[11] << 8u | raw[10]);

        out[i].accel.x = raw_to_mg(ax);
        out[i].accel.y = raw_to_mg(ay);
        out[i].accel.z = raw_to_mg(az);
        out[i].gyro.x  = raw_to_mdps(gx);
        out[i].gyro.y  = raw_to_mdps(gy);
        out[i].gyro.z  = raw_to_mdps(gz);
    }

    return avail_sets;
}

/* ================================================================== */
/*  Misc                                                               */
/* ================================================================== */

int32_t BSP_IMU_ReadTemp(void)
{
    uint8_t buf[2];

    if (!reg_read_multi(REG_OUT_TEMP_L, buf, 2u)) {
        return INT32_MIN;
    }

    /*
     * LSM6DSL temperature: 16-bit signed, sensitivity 256 LSB/°C,
     * zero-level = 25 °C.
     * Result in 0.1 °C:  temp_x10 = 250 + raw * 10 / 256
     */
    int16_t raw = (int16_t)((uint16_t)buf[1] << 8u | buf[0]);
    int32_t temp_x10 = 250 + ((int32_t)raw * 10) / 256;

    return temp_x10;
}

void BSP_IMU_PowerDown(void)
{
    /* Disable INT1 routing */
    reg_write(REG_INT1_CTRL, 0x00u);
    /* Stop FIFO (bypass mode) */
    reg_write(REG_FIFO_CTRL5, 0x00u);
    /* Set ODR = 0 for both accel and gyro */
    reg_write(REG_CTRL1_XL, 0x00u);
    reg_write(REG_CTRL2_G,  0x00u);
}

void BSP_IMU_ConfigWakeup(uint8_t thresh_6bit)
{
    /*
     * LSM6DSL "wake-up" / activity recognition (datasheet AN5040 §6.1):
     *   - Accel only at low ODR (12.5 Hz), gyro off → ~20 µA
     *   - FIFO disabled (bypass mode)
     *   - Wake-up function compares instantaneous accel against threshold
     *   - Wake-up event routed to INT1 → EXTI PD11 → wakes STM32 from Stop 2
     *
     * Threshold formula (±4 g full-scale):
     *   1 LSB = FS / 64 = 4000 mg / 64 = 62.5 mg
     *   thresh=1 → 62.5 mg,  thresh=2 → 125 mg, etc.
     */

    /* Disable FIFO-watermark routing on INT1 */
    reg_write(REG_INT1_CTRL, 0x00u);

    /* Stop FIFO (bypass mode) */
    reg_write(REG_FIFO_CTRL5, 0x00u);

    /* Gyro off */
    reg_write(REG_CTRL2_G, 0x00u);

    /* Accel: 12.5 Hz, ±4 g (low-power) */
    reg_write(REG_CTRL1_XL,
              (uint8_t)((LSM6DSL_ODR_12_5HZ << 4u) | (LSM6DSL_XL_FS_4G << 2u)));

    /* Enable interrupts + slope filter + latched interrupts for wake-up
     * TAP_CFG bit 7 = INTERRUPTS_ENABLE (mandatory for wake-up function)
     * TAP_CFG bit 4 = SLOPE_FDS (slope filter for wake-up detection)
     * TAP_CFG bit 0 = LIR (latched: INT1 stays HIGH until WAKE_UP_SRC read) */
    reg_write(REG_TAP_CFG, 0x91u);

    /* Wake-up threshold (bits [5:0]); bit 6 = 0 (no single/double tap) */
    if (thresh_6bit > 63u) { thresh_6bit = 63u; }
    reg_write(REG_WAKE_UP_THS, thresh_6bit);

    /* Wake-up duration = 0 (1 sample above threshold is enough) */
    reg_write(REG_WAKE_UP_DUR, 0x00u);

    /* Route wake-up event to INT1 (MD1_CFG bit 5 = INT1_WU) */
    reg_write(REG_MD1_CFG, 0x20u);

    /* Read WAKE_UP_SRC to clear any startup transient latch */
    {
        uint8_t dummy;
        reg_read(REG_WAKE_UP_SRC, &dummy);
    }
}

bool BSP_IMU_ReadWakeUpSrc(uint8_t *src)
{
    return reg_read(REG_WAKE_UP_SRC, src);
}
