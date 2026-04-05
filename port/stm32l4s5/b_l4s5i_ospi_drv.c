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
 * @file  b_l4s5i_ospi_drv.c
 * @brief B-L4S5I-IOT01A — OCTOSPI1 bus + MX25R6435F low-level driver
 *
 * Macronix MX25R6435F on OCTOSPI1, standard SPI mode (1-1-1).
 *
 * SPI commands used (single-line / standard SPI for simplicity):
 *   RDID  (9Fh)  — read JEDEC ID   (1-0-1, 3 bytes)
 *   WREN  (06h)  — write enable     (1-0-0)
 *   RDSR  (05h)  — read status reg  (1-0-1, 1 byte)
 *   SE    (20h)  — sector erase 4KB (1-1-0)
 *   PP    (02h)  — page program     (1-1-1, max 256 bytes)
 *   READ  (03h)  — read data        (1-1-1)
 *
 * To port to a different NOR Flash chip, replace this file only.
 * Keep the BSP_OSPI_* API unchanged.
 */

#include "app_config.h"
#include "b_l4s5i_ospi_drv.h"   /* brings in BSP_OSPI_NEEDED macro */

#if BSP_OSPI_NEEDED

#include "anbo_log.h"
#include "stm32l4xx_hal.h"

/* ================================================================== */
/*  MX25R6435F constants                                               */
/* ================================================================== */

#define MX25R_JEDEC_MFR      0xC2u
#define MX25R_JEDEC_TYPE     0x28u
#define MX25R_JEDEC_CAP      0x17u       /* 64 Mbit */

#define MX25R_CMD_RDID       0x9Fu
#define MX25R_CMD_WREN       0x06u
#define MX25R_CMD_RDSR       0x05u
#define MX25R_CMD_SE         0x20u       /* sector erase 4 KB */
#define MX25R_CMD_PP         0x02u       /* page program (max 256 B) */
#define MX25R_CMD_READ       0x03u
#define MX25R_CMD_RDP        0xABu       /* Release from Deep Power-Down */

#define MX25R_SR_WIP         0x01u       /* write-in-progress bit */

/*
 * HAL_OSPI_TIMEOUT_DEFAULT_VALUE is 5 000 ms — far too long for an
 * IWDG window of 2 s.  Use a shorter timeout so that when the external
 * Flash chip is absent or the bus is faulty, every HAL call fails fast
 * and the cumulative init time stays within the watchdog budget.
 *
 * For a healthy chip, any SPI command completes in microseconds;
 * 500 ms is extremely generous and still < 2 s IWDG.
 */
#define BSP_OSPI_TIMEOUT     500u

/* ================================================================== */
/*  OSPI handle                                                        */
/* ================================================================== */

static OSPI_HandleTypeDef s_hospi;

/* ================================================================== */
/*  Internal SPI helpers                                               */
/* ================================================================== */

static bool ospi_cmd_only(uint8_t cmd)
{
    OSPI_RegularCmdTypeDef c = {0};
    c.OperationType      = HAL_OSPI_OPTYPE_COMMON_CFG;
    c.FlashId            = HAL_OSPI_FLASH_ID_1;
    c.InstructionMode    = HAL_OSPI_INSTRUCTION_1_LINE;
    c.InstructionSize    = HAL_OSPI_INSTRUCTION_8_BITS;
    c.Instruction        = cmd;
    c.AddressMode        = HAL_OSPI_ADDRESS_NONE;
    c.DataMode           = HAL_OSPI_DATA_NONE;
    c.AlternateBytesMode = HAL_OSPI_ALTERNATE_BYTES_NONE;
    c.DummyCycles        = 0;
    c.DQSMode            = HAL_OSPI_DQS_DISABLE;
    c.SIOOMode           = HAL_OSPI_SIOO_INST_EVERY_CMD;
    return HAL_OSPI_Command(&s_hospi, &c, BSP_OSPI_TIMEOUT) == HAL_OK;
}

static bool ospi_read_reg(uint8_t cmd, uint8_t *buf, uint32_t len)
{
    OSPI_RegularCmdTypeDef c = {0};
    c.OperationType      = HAL_OSPI_OPTYPE_COMMON_CFG;
    c.FlashId            = HAL_OSPI_FLASH_ID_1;
    c.InstructionMode    = HAL_OSPI_INSTRUCTION_1_LINE;
    c.InstructionSize    = HAL_OSPI_INSTRUCTION_8_BITS;
    c.Instruction        = cmd;
    c.AddressMode        = HAL_OSPI_ADDRESS_NONE;
    c.DataMode           = HAL_OSPI_DATA_1_LINE;
    c.NbData             = len;
    c.AlternateBytesMode = HAL_OSPI_ALTERNATE_BYTES_NONE;
    c.DummyCycles        = 0;
    c.DQSMode            = HAL_OSPI_DQS_DISABLE;
    c.SIOOMode           = HAL_OSPI_SIOO_INST_EVERY_CMD;
    if (HAL_OSPI_Command(&s_hospi, &c, BSP_OSPI_TIMEOUT) != HAL_OK) {
        return false;
    }
    return HAL_OSPI_Receive(&s_hospi, buf, BSP_OSPI_TIMEOUT) == HAL_OK;
}

static bool ospi_write_enable(void)
{
    return ospi_cmd_only(MX25R_CMD_WREN);
}

static bool ospi_wait_ready(uint32_t timeout_ms)
{
    uint32_t t0 = HAL_GetTick();
    uint8_t sr;
    for (;;) {
        if (!ospi_read_reg(MX25R_CMD_RDSR, &sr, 1u)) {
            return false;
        }
        if ((sr & MX25R_SR_WIP) == 0u) {
            return true;
        }
        if ((HAL_GetTick() - t0) > timeout_ms) {
            return false;
        }
    }
}

/* ================================================================== */
/*  OSPI peripheral + GPIO init                                        */
/* ================================================================== */

static void ospi_gpio_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOE_CLK_ENABLE();

    /* PE10=CLK, PE11=NCS, PE12=IO0, PE13=IO1, PE14=IO2, PE15=IO3 */
    gpio.Pin       = GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 |
                     GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_NOPULL;
    gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF10_OCTOSPIM_P1;
    HAL_GPIO_Init(GPIOE, &gpio);
}

/* ================================================================== */
/*  Public API                                                         */
/* ================================================================== */

bool BSP_OSPI_Init(void)
{
    uint8_t id[3];

    /* GPIO */
    ospi_gpio_init();

    /* OCTOSPI1 clock: use SYSCLK (120 MHz) */
    RCC_PeriphCLKInitTypeDef pclk = {0};
    pclk.PeriphClockSelection = RCC_PERIPHCLK_OSPI;
    pclk.OspiClockSelection   = RCC_OSPICLKSOURCE_SYSCLK;
    if (HAL_RCCEx_PeriphCLKConfig(&pclk) != HAL_OK) {
        ANBO_LOGW("OSPI: RCC PeriphCLK fail%s", "");
        return false;
    }

    __HAL_RCC_OSPIM_CLK_ENABLE();
    __HAL_RCC_OSPI1_CLK_ENABLE();

    /* OCTOSPI1 peripheral — must be initialised BEFORE OSPIM mapping */
    s_hospi.Instance = OCTOSPI1;
    s_hospi.Init.FifoThreshold         = 4u;
    s_hospi.Init.DualQuad              = HAL_OSPI_DUALQUAD_DISABLE;
    s_hospi.Init.MemoryType            = HAL_OSPI_MEMTYPE_MACRONIX;
    s_hospi.Init.DeviceSize            = 23u;    /* 2^23 = 8 MB */
    s_hospi.Init.ChipSelectHighTime    = 2u;
    s_hospi.Init.FreeRunningClock      = HAL_OSPI_FREERUNCLK_DISABLE;
    s_hospi.Init.ClockMode             = HAL_OSPI_CLOCK_MODE_0;
    s_hospi.Init.ClockPrescaler        = 16u;    /* 120 / 16 = 7.5 MHz (MX25R ULP max 8 MHz) */
    s_hospi.Init.SampleShifting        = HAL_OSPI_SAMPLE_SHIFTING_NONE;
    s_hospi.Init.DelayHoldQuarterCycle = HAL_OSPI_DHQC_DISABLE;
    s_hospi.Init.ChipSelectBoundary    = 0u;
    if (HAL_OSPI_Init(&s_hospi) != HAL_OK) {
        ANBO_LOGW("OSPI: HAL_OSPI_Init fail (state=%u err=0x%08x)",
                  (unsigned)s_hospi.State, (unsigned)s_hospi.ErrorCode);
        return false;
    }

    /*
     * OCTOSPI Manager — map physical Port 1 pins to OCTOSPI1.
     * MUST be called AFTER HAL_OSPI_Init(): the HAL needs a valid
     * hospi->Instance to determine which OSPI controller to bind.
     * Without this mapping, OSPI commands go nowhere — the controller
     * is not connected to the physical pins.
     */
    OSPIM_CfgTypeDef mgr = {0};
    mgr.ClkPort    = 1u;
    mgr.NCSPort    = 1u;
    mgr.IOLowPort  = HAL_OSPIM_IOPORT_1_LOW;
    mgr.IOHighPort = HAL_OSPIM_IOPORT_1_HIGH;
    if (HAL_OSPIM_Config(&s_hospi, &mgr, BSP_OSPI_TIMEOUT) != HAL_OK) {
        ANBO_LOGW("OSPI: OSPIM config fail (state=%u err=0x%08x)",
                  (unsigned)s_hospi.State, (unsigned)s_hospi.ErrorCode);
        return false;   /* fatal: pins not mapped → chip unreachable */
    }

    /*
     * Wake chip from Deep Power-Down (if active).
     * MX25R6435F enters DPD after power-on or if previous firmware
     * issued a DP (B9h) command.  While in DPD, the chip ignores
     * all commands except RDP (ABh).  After RDP, wait tRES1 = 35 µs
     * before issuing any other command.
     * If the chip is not in DPD, RDP is harmless (no-op).
     * If the chip is absent, this will simply timeout — no harm done.
     */
    ospi_cmd_only(MX25R_CMD_RDP);
    {
        /* tRES1 = 35 µs; busy-wait ~50 µs at 120 MHz ≈ 6000 cycles */
        volatile uint32_t dly;
        for (dly = 0u; dly < 6000u; dly++) { /* empty */ }
    }

    /* Verify JEDEC ID: C2 28 17 */
    if (!ospi_read_reg(MX25R_CMD_RDID, id, 3u)) {
        ANBO_LOGW("OSPI: RDID cmd fail (state=%u err=0x%08x)",
                  (unsigned)s_hospi.State, (unsigned)s_hospi.ErrorCode);
        return false;
    }
    if (id[0] != MX25R_JEDEC_MFR || id[1] != MX25R_JEDEC_TYPE ||
        id[2] != MX25R_JEDEC_CAP) {
        ANBO_LOGW("OSPI: JEDEC ID mismatch: %02x %02x %02x (expect C2 28 17)",
                  id[0], id[1], id[2]);
        return false;
    }
    return true;
}

bool BSP_OSPI_Read(uint32_t addr, uint8_t *buf, uint32_t len)
{
    OSPI_RegularCmdTypeDef c = {0};
    c.OperationType      = HAL_OSPI_OPTYPE_COMMON_CFG;
    c.FlashId            = HAL_OSPI_FLASH_ID_1;
    c.InstructionMode    = HAL_OSPI_INSTRUCTION_1_LINE;
    c.InstructionSize    = HAL_OSPI_INSTRUCTION_8_BITS;
    c.Instruction        = MX25R_CMD_READ;
    c.AddressMode        = HAL_OSPI_ADDRESS_1_LINE;
    c.AddressSize        = HAL_OSPI_ADDRESS_24_BITS;
    c.Address            = addr;
    c.DataMode           = HAL_OSPI_DATA_1_LINE;
    c.NbData             = len;
    c.AlternateBytesMode = HAL_OSPI_ALTERNATE_BYTES_NONE;
    c.DummyCycles        = 0;
    c.DQSMode            = HAL_OSPI_DQS_DISABLE;
    c.SIOOMode           = HAL_OSPI_SIOO_INST_EVERY_CMD;
    if (HAL_OSPI_Command(&s_hospi, &c, BSP_OSPI_TIMEOUT) != HAL_OK) {
        return false;
    }
    return HAL_OSPI_Receive(&s_hospi, buf, BSP_OSPI_TIMEOUT) == HAL_OK;
}

bool BSP_OSPI_PageProgram(uint32_t addr, const uint8_t *data, uint32_t len)
{
    OSPI_RegularCmdTypeDef c = {0};

    if (!ospi_write_enable()) { return false; }

    c.OperationType      = HAL_OSPI_OPTYPE_COMMON_CFG;
    c.FlashId            = HAL_OSPI_FLASH_ID_1;
    c.InstructionMode    = HAL_OSPI_INSTRUCTION_1_LINE;
    c.InstructionSize    = HAL_OSPI_INSTRUCTION_8_BITS;
    c.Instruction        = MX25R_CMD_PP;
    c.AddressMode        = HAL_OSPI_ADDRESS_1_LINE;
    c.AddressSize        = HAL_OSPI_ADDRESS_24_BITS;
    c.Address            = addr;
    c.DataMode           = HAL_OSPI_DATA_1_LINE;
    c.NbData             = len;
    c.AlternateBytesMode = HAL_OSPI_ALTERNATE_BYTES_NONE;
    c.DummyCycles        = 0;
    c.DQSMode            = HAL_OSPI_DQS_DISABLE;
    c.SIOOMode           = HAL_OSPI_SIOO_INST_EVERY_CMD;
    if (HAL_OSPI_Command(&s_hospi, &c, BSP_OSPI_TIMEOUT) != HAL_OK) {
        return false;
    }
    if (HAL_OSPI_Transmit(&s_hospi, (uint8_t *)data,
                          BSP_OSPI_TIMEOUT) != HAL_OK) {
        return false;
    }
    /* MX25R6435F page program time: typ 1.5 ms, max 10 ms */
    return ospi_wait_ready(50u);
}

bool BSP_OSPI_SectorErase(uint32_t addr)
{
    OSPI_RegularCmdTypeDef c = {0};

    if (!ospi_write_enable()) { return false; }

    c.OperationType      = HAL_OSPI_OPTYPE_COMMON_CFG;
    c.FlashId            = HAL_OSPI_FLASH_ID_1;
    c.InstructionMode    = HAL_OSPI_INSTRUCTION_1_LINE;
    c.InstructionSize    = HAL_OSPI_INSTRUCTION_8_BITS;
    c.Instruction        = MX25R_CMD_SE;
    c.AddressMode        = HAL_OSPI_ADDRESS_1_LINE;
    c.AddressSize        = HAL_OSPI_ADDRESS_24_BITS;
    c.Address            = addr;
    c.DataMode           = HAL_OSPI_DATA_NONE;
    c.AlternateBytesMode = HAL_OSPI_ALTERNATE_BYTES_NONE;
    c.DummyCycles        = 0;
    c.DQSMode            = HAL_OSPI_DQS_DISABLE;
    c.SIOOMode           = HAL_OSPI_SIOO_INST_EVERY_CMD;
    if (HAL_OSPI_Command(&s_hospi, &c, BSP_OSPI_TIMEOUT) != HAL_OK) {
        return false;
    }
    /* MX25R6435F sector erase time: typ 50 ms, max 240 ms */
    return ospi_wait_ready(500u);
}

#else
typedef int b_l4s5i_ospi_drv_empty_tu;
#endif /* BSP_OSPI_NEEDED */
