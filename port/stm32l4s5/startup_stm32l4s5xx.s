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
 * @file  startup_stm32l4s5xx.s
 * @brief Cortex-M4 startup for STM32L4S5VITx (GCC / arm-none-eabi)
 *
 * - Sets initial SP from linker (_estack)
 * - Vector table with all STM32L4S5 IRQs
 * - Reset_Handler: .data copy, .bss zero, SystemInit, main
 * - Default_Handler infinite loop for unhandled interrupts
 */

    .syntax unified
    .cpu    cortex-m4
    .fpu    fpv4-sp-d16
    .thumb

/* ================================================================== */
/*  Linker-provided symbols                                            */
/* ================================================================== */
    .word _sidata       /* Start of .data initializers in FLASH */
    .word _sdata        /* Start of .data in RAM */
    .word _edata        /* End   of .data in RAM */
    .word _sbss         /* Start of .bss */
    .word _ebss         /* End   of .bss */
    .word _sbss_sram2   /* Start of .sram2_bss (SRAM2 pools) */
    .word _ebss_sram2   /* End   of .sram2_bss */

/* ================================================================== */
/*  Reset Handler                                                      */
/* ================================================================== */
    .section .text.Reset_Handler
    .weak   Reset_Handler
    .type   Reset_Handler, %function
Reset_Handler:

    /* --- Copy .data from FLASH to RAM --- */
    ldr     r0, =_sdata
    ldr     r1, =_edata
    ldr     r2, =_sidata
    movs    r3, #0
    b       .LoopCopyDataInit

.CopyDataInit:
    ldr     r4, [r2, r3]
    str     r4, [r0, r3]
    adds    r3, r3, #4

.LoopCopyDataInit:
    adds    r4, r0, r3
    cmp     r4, r1
    bcc     .CopyDataInit

    /* --- Zero .bss --- */
    ldr     r2, =_sbss
    ldr     r4, =_ebss
    movs    r3, #0
    b       .LoopFillZerobss

.FillZerobss:
    str     r3, [r2]
    adds    r2, r2, #4

.LoopFillZerobss:
    cmp     r2, r4
    bcc     .FillZerobss

    /* --- Zero .sram2_bss (Anbo kernel pools in SRAM2) --- */
    ldr     r2, =_sbss_sram2
    ldr     r4, =_ebss_sram2
    movs    r3, #0
    b       .LoopFillZeroSram2

.FillZeroSram2:
    str     r3, [r2]
    adds    r2, r2, #4

.LoopFillZeroSram2:
    cmp     r2, r4
    bcc     .FillZeroSram2

    /* --- Enable FPU (CP10/CP11 full access) --- */
    ldr     r0, =0xE000ED88        /* SCB->CPACR */
    ldr     r1, [r0]
    orr     r1, r1, #(0xF << 20)   /* CP10 + CP11 = full access */
    str     r1, [r0]
    dsb
    isb

    /* --- Call SystemInit if defined (weak) --- */
    bl      SystemInit

    /* --- Call libc init (for static constructors) --- */
    bl      __libc_init_array

    /* --- Jump to main --- */
    bl      main

    /* --- If main returns, loop forever --- */
    b       .

    .size   Reset_Handler, .-Reset_Handler

/* ================================================================== */
/*  Default Handler — infinite loop for unimplemented IRQs             */
/* ================================================================== */
    .section .text.Default_Handler, "ax", %progbits
Default_Handler:
    b       .
    .size   Default_Handler, .-Default_Handler

/* ================================================================== */
/*  Weak SystemInit — override in user code if needed                  */
/* ================================================================== */
    .weak   SystemInit
    .thumb_set SystemInit, Default_Handler

/* ================================================================== */
/*  Vector Table (placed at FLASH base by linker .isr_vector section)  */
/* ================================================================== */
    .section .isr_vector, "a", %progbits
    .type   g_pfnVectors, %object
    .size   g_pfnVectors, .-g_pfnVectors

g_pfnVectors:
    /* Cortex-M4 system exceptions */
    .word   _estack                     /*  0: Initial Stack Pointer */
    .word   Reset_Handler               /*  1: Reset */
    .word   NMI_Handler                 /*  2: NMI */
    .word   HardFault_Handler           /*  3: Hard Fault */
    .word   MemManage_Handler           /*  4: Mem Manage */
    .word   BusFault_Handler            /*  5: Bus Fault */
    .word   UsageFault_Handler          /*  6: Usage Fault */
    .word   0                           /*  7: Reserved */
    .word   0                           /*  8: Reserved */
    .word   0                           /*  9: Reserved */
    .word   0                           /* 10: Reserved */
    .word   SVC_Handler                 /* 11: SVCall */
    .word   DebugMon_Handler            /* 12: Debug Monitor */
    .word   0                           /* 13: Reserved */
    .word   PendSV_Handler              /* 14: PendSV */
    .word   SysTick_Handler             /* 15: SysTick */

    /* STM32L4S5 peripheral interrupts (IRQn 0..94) */
    .word   WWDG_IRQHandler                     /*  0 */
    .word   PVD_PVM_IRQHandler                  /*  1 */
    .word   TAMP_STAMP_IRQHandler               /*  2 */
    .word   RTC_WKUP_IRQHandler                 /*  3 */
    .word   FLASH_IRQHandler                    /*  4 */
    .word   RCC_IRQHandler                      /*  5 */
    .word   EXTI0_IRQHandler                    /*  6 */
    .word   EXTI1_IRQHandler                    /*  7 */
    .word   EXTI2_IRQHandler                    /*  8 */
    .word   EXTI3_IRQHandler                    /*  9 */
    .word   EXTI4_IRQHandler                    /* 10 */
    .word   DMA1_Channel1_IRQHandler            /* 11 */
    .word   DMA1_Channel2_IRQHandler            /* 12 */
    .word   DMA1_Channel3_IRQHandler            /* 13 */
    .word   DMA1_Channel4_IRQHandler            /* 14 */
    .word   DMA1_Channel5_IRQHandler            /* 15 */
    .word   DMA1_Channel6_IRQHandler            /* 16 */
    .word   DMA1_Channel7_IRQHandler            /* 17 */
    .word   ADC1_IRQHandler                     /* 18 */
    .word   CAN1_TX_IRQHandler                  /* 19 */
    .word   CAN1_RX0_IRQHandler                 /* 20 */
    .word   CAN1_RX1_IRQHandler                 /* 21 */
    .word   CAN1_SCE_IRQHandler                 /* 22 */
    .word   EXTI9_5_IRQHandler                  /* 23 */
    .word   TIM1_BRK_TIM15_IRQHandler           /* 24 */
    .word   TIM1_UP_TIM16_IRQHandler            /* 25 */
    .word   TIM1_TRG_COM_TIM17_IRQHandler       /* 26 */
    .word   TIM1_CC_IRQHandler                  /* 27 */
    .word   TIM2_IRQHandler                     /* 28 */
    .word   TIM3_IRQHandler                     /* 29 */
    .word   TIM4_IRQHandler                     /* 30 */
    .word   I2C1_EV_IRQHandler                  /* 31 */
    .word   I2C1_ER_IRQHandler                  /* 32 */
    .word   I2C2_EV_IRQHandler                  /* 33 */
    .word   I2C2_ER_IRQHandler                  /* 34 */
    .word   SPI1_IRQHandler                     /* 35 */
    .word   SPI2_IRQHandler                     /* 36 */
    .word   USART1_IRQHandler                   /* 37 */
    .word   USART2_IRQHandler                   /* 38 */
    .word   USART3_IRQHandler                   /* 39 */
    .word   EXTI15_10_IRQHandler                /* 40 */
    .word   RTC_Alarm_IRQHandler                /* 41 */
    .word   DFSDM1_FLT3_IRQHandler              /* 42 */
    .word   TIM8_BRK_IRQHandler                 /* 43 */
    .word   TIM8_UP_IRQHandler                  /* 44 */
    .word   TIM8_TRG_COM_IRQHandler             /* 45 */
    .word   TIM8_CC_IRQHandler                  /* 46 */
    .word   0                                   /* 47: Reserved */
    .word   FMC_IRQHandler                      /* 48 */
    .word   SDMMC1_IRQHandler                   /* 49 */
    .word   TIM5_IRQHandler                     /* 50 */
    .word   SPI3_IRQHandler                     /* 51 */
    .word   UART4_IRQHandler                    /* 52 */
    .word   UART5_IRQHandler                    /* 53 */
    .word   TIM6_DAC_IRQHandler                 /* 54 */
    .word   TIM7_IRQHandler                     /* 55 */
    .word   DMA2_Channel1_IRQHandler            /* 56 */
    .word   DMA2_Channel2_IRQHandler            /* 57 */
    .word   DMA2_Channel3_IRQHandler            /* 58 */
    .word   DMA2_Channel4_IRQHandler            /* 59 */
    .word   DMA2_Channel5_IRQHandler            /* 60 */
    .word   DFSDM1_FLT0_IRQHandler              /* 61 */
    .word   DFSDM1_FLT1_IRQHandler              /* 62 */
    .word   DFSDM1_FLT2_IRQHandler              /* 63 */
    .word   COMP_IRQHandler                     /* 64 */
    .word   LPTIM1_IRQHandler                   /* 65 */
    .word   LPTIM2_IRQHandler                   /* 66 */
    .word   OTG_FS_IRQHandler                   /* 67 */
    .word   DMA2_Channel6_IRQHandler            /* 68 */
    .word   DMA2_Channel7_IRQHandler            /* 69 */
    .word   LPUART1_IRQHandler                  /* 70 */
    .word   OCTOSPI1_IRQHandler                 /* 71 */
    .word   I2C3_EV_IRQHandler                  /* 72 */
    .word   I2C3_ER_IRQHandler                  /* 73 */
    .word   SAI1_IRQHandler                     /* 74 */
    .word   SAI2_IRQHandler                     /* 75 */
    .word   OCTOSPI2_IRQHandler                 /* 76 */
    .word   TSC_IRQHandler                      /* 77 */
    .word   0                                   /* 78: Reserved */
    .word   AES_IRQHandler                      /* 79 */
    .word   RNG_IRQHandler                      /* 80 */
    .word   FPU_IRQHandler                      /* 81 */
    .word   HASH_CRS_IRQHandler                 /* 82 */
    .word   I2C4_EV_IRQHandler                  /* 83 */
    .word   I2C4_ER_IRQHandler                  /* 84 */
    .word   DCMI_IRQHandler                     /* 85 */
    .word   0                                   /* 86: Reserved */
    .word   0                                   /* 87: Reserved */
    .word   0                                   /* 88: Reserved */
    .word   0                                   /* 89: Reserved */
    .word   DMA2D_IRQHandler                    /* 90 */
    .word   LTDC_IRQHandler                     /* 91 */
    .word   LTDC_ER_IRQHandler                  /* 92 */
    .word   GFXMMU_IRQHandler                   /* 93 */
    .word   DMAMUX1_OVR_IRQHandler              /* 94 */

/* ================================================================== */
/*  Weak aliases — all unhandled IRQs default to infinite loop         */
/* ================================================================== */

    .weak   NMI_Handler
    .thumb_set NMI_Handler, Default_Handler

    .weak   HardFault_Handler
    .thumb_set HardFault_Handler, Default_Handler

    .weak   MemManage_Handler
    .thumb_set MemManage_Handler, Default_Handler

    .weak   BusFault_Handler
    .thumb_set BusFault_Handler, Default_Handler

    .weak   UsageFault_Handler
    .thumb_set UsageFault_Handler, Default_Handler

    .weak   SVC_Handler
    .thumb_set SVC_Handler, Default_Handler

    .weak   DebugMon_Handler
    .thumb_set DebugMon_Handler, Default_Handler

    .weak   PendSV_Handler
    .thumb_set PendSV_Handler, Default_Handler

    .weak   SysTick_Handler
    .thumb_set SysTick_Handler, Default_Handler

    .weak   WWDG_IRQHandler
    .thumb_set WWDG_IRQHandler, Default_Handler

    .weak   PVD_PVM_IRQHandler
    .thumb_set PVD_PVM_IRQHandler, Default_Handler

    .weak   TAMP_STAMP_IRQHandler
    .thumb_set TAMP_STAMP_IRQHandler, Default_Handler

    .weak   RTC_WKUP_IRQHandler
    .thumb_set RTC_WKUP_IRQHandler, Default_Handler

    .weak   FLASH_IRQHandler
    .thumb_set FLASH_IRQHandler, Default_Handler

    .weak   RCC_IRQHandler
    .thumb_set RCC_IRQHandler, Default_Handler

    .weak   EXTI0_IRQHandler
    .thumb_set EXTI0_IRQHandler, Default_Handler

    .weak   EXTI1_IRQHandler
    .thumb_set EXTI1_IRQHandler, Default_Handler

    .weak   EXTI2_IRQHandler
    .thumb_set EXTI2_IRQHandler, Default_Handler

    .weak   EXTI3_IRQHandler
    .thumb_set EXTI3_IRQHandler, Default_Handler

    .weak   EXTI4_IRQHandler
    .thumb_set EXTI4_IRQHandler, Default_Handler

    .weak   DMA1_Channel1_IRQHandler
    .thumb_set DMA1_Channel1_IRQHandler, Default_Handler

    .weak   DMA1_Channel2_IRQHandler
    .thumb_set DMA1_Channel2_IRQHandler, Default_Handler

    .weak   DMA1_Channel3_IRQHandler
    .thumb_set DMA1_Channel3_IRQHandler, Default_Handler

    .weak   DMA1_Channel4_IRQHandler
    .thumb_set DMA1_Channel4_IRQHandler, Default_Handler

    .weak   DMA1_Channel5_IRQHandler
    .thumb_set DMA1_Channel5_IRQHandler, Default_Handler

    .weak   DMA1_Channel6_IRQHandler
    .thumb_set DMA1_Channel6_IRQHandler, Default_Handler

    .weak   DMA1_Channel7_IRQHandler
    .thumb_set DMA1_Channel7_IRQHandler, Default_Handler

    .weak   ADC1_IRQHandler
    .thumb_set ADC1_IRQHandler, Default_Handler

    .weak   CAN1_TX_IRQHandler
    .thumb_set CAN1_TX_IRQHandler, Default_Handler

    .weak   CAN1_RX0_IRQHandler
    .thumb_set CAN1_RX0_IRQHandler, Default_Handler

    .weak   CAN1_RX1_IRQHandler
    .thumb_set CAN1_RX1_IRQHandler, Default_Handler

    .weak   CAN1_SCE_IRQHandler
    .thumb_set CAN1_SCE_IRQHandler, Default_Handler

    .weak   EXTI9_5_IRQHandler
    .thumb_set EXTI9_5_IRQHandler, Default_Handler

    .weak   TIM1_BRK_TIM15_IRQHandler
    .thumb_set TIM1_BRK_TIM15_IRQHandler, Default_Handler

    .weak   TIM1_UP_TIM16_IRQHandler
    .thumb_set TIM1_UP_TIM16_IRQHandler, Default_Handler

    .weak   TIM1_TRG_COM_TIM17_IRQHandler
    .thumb_set TIM1_TRG_COM_TIM17_IRQHandler, Default_Handler

    .weak   TIM1_CC_IRQHandler
    .thumb_set TIM1_CC_IRQHandler, Default_Handler

    .weak   TIM2_IRQHandler
    .thumb_set TIM2_IRQHandler, Default_Handler

    .weak   TIM3_IRQHandler
    .thumb_set TIM3_IRQHandler, Default_Handler

    .weak   TIM4_IRQHandler
    .thumb_set TIM4_IRQHandler, Default_Handler

    .weak   I2C1_EV_IRQHandler
    .thumb_set I2C1_EV_IRQHandler, Default_Handler

    .weak   I2C1_ER_IRQHandler
    .thumb_set I2C1_ER_IRQHandler, Default_Handler

    .weak   I2C2_EV_IRQHandler
    .thumb_set I2C2_EV_IRQHandler, Default_Handler

    .weak   I2C2_ER_IRQHandler
    .thumb_set I2C2_ER_IRQHandler, Default_Handler

    .weak   SPI1_IRQHandler
    .thumb_set SPI1_IRQHandler, Default_Handler

    .weak   SPI2_IRQHandler
    .thumb_set SPI2_IRQHandler, Default_Handler

    .weak   USART1_IRQHandler
    .thumb_set USART1_IRQHandler, Default_Handler

    .weak   USART2_IRQHandler
    .thumb_set USART2_IRQHandler, Default_Handler

    .weak   USART3_IRQHandler
    .thumb_set USART3_IRQHandler, Default_Handler

    .weak   EXTI15_10_IRQHandler
    .thumb_set EXTI15_10_IRQHandler, Default_Handler

    .weak   RTC_Alarm_IRQHandler
    .thumb_set RTC_Alarm_IRQHandler, Default_Handler

    .weak   DFSDM1_FLT3_IRQHandler
    .thumb_set DFSDM1_FLT3_IRQHandler, Default_Handler

    .weak   TIM8_BRK_IRQHandler
    .thumb_set TIM8_BRK_IRQHandler, Default_Handler

    .weak   TIM8_UP_IRQHandler
    .thumb_set TIM8_UP_IRQHandler, Default_Handler

    .weak   TIM8_TRG_COM_IRQHandler
    .thumb_set TIM8_TRG_COM_IRQHandler, Default_Handler

    .weak   TIM8_CC_IRQHandler
    .thumb_set TIM8_CC_IRQHandler, Default_Handler

    .weak   FMC_IRQHandler
    .thumb_set FMC_IRQHandler, Default_Handler

    .weak   SDMMC1_IRQHandler
    .thumb_set SDMMC1_IRQHandler, Default_Handler

    .weak   TIM5_IRQHandler
    .thumb_set TIM5_IRQHandler, Default_Handler

    .weak   SPI3_IRQHandler
    .thumb_set SPI3_IRQHandler, Default_Handler

    .weak   UART4_IRQHandler
    .thumb_set UART4_IRQHandler, Default_Handler

    .weak   UART5_IRQHandler
    .thumb_set UART5_IRQHandler, Default_Handler

    .weak   TIM6_DAC_IRQHandler
    .thumb_set TIM6_DAC_IRQHandler, Default_Handler

    .weak   TIM7_IRQHandler
    .thumb_set TIM7_IRQHandler, Default_Handler

    .weak   DMA2_Channel1_IRQHandler
    .thumb_set DMA2_Channel1_IRQHandler, Default_Handler

    .weak   DMA2_Channel2_IRQHandler
    .thumb_set DMA2_Channel2_IRQHandler, Default_Handler

    .weak   DMA2_Channel3_IRQHandler
    .thumb_set DMA2_Channel3_IRQHandler, Default_Handler

    .weak   DMA2_Channel4_IRQHandler
    .thumb_set DMA2_Channel4_IRQHandler, Default_Handler

    .weak   DMA2_Channel5_IRQHandler
    .thumb_set DMA2_Channel5_IRQHandler, Default_Handler

    .weak   DFSDM1_FLT0_IRQHandler
    .thumb_set DFSDM1_FLT0_IRQHandler, Default_Handler

    .weak   DFSDM1_FLT1_IRQHandler
    .thumb_set DFSDM1_FLT1_IRQHandler, Default_Handler

    .weak   DFSDM1_FLT2_IRQHandler
    .thumb_set DFSDM1_FLT2_IRQHandler, Default_Handler

    .weak   COMP_IRQHandler
    .thumb_set COMP_IRQHandler, Default_Handler

    .weak   LPTIM1_IRQHandler
    .thumb_set LPTIM1_IRQHandler, Default_Handler

    .weak   LPTIM2_IRQHandler
    .thumb_set LPTIM2_IRQHandler, Default_Handler

    .weak   OTG_FS_IRQHandler
    .thumb_set OTG_FS_IRQHandler, Default_Handler

    .weak   DMA2_Channel6_IRQHandler
    .thumb_set DMA2_Channel6_IRQHandler, Default_Handler

    .weak   DMA2_Channel7_IRQHandler
    .thumb_set DMA2_Channel7_IRQHandler, Default_Handler

    .weak   LPUART1_IRQHandler
    .thumb_set LPUART1_IRQHandler, Default_Handler

    .weak   OCTOSPI1_IRQHandler
    .thumb_set OCTOSPI1_IRQHandler, Default_Handler

    .weak   I2C3_EV_IRQHandler
    .thumb_set I2C3_EV_IRQHandler, Default_Handler

    .weak   I2C3_ER_IRQHandler
    .thumb_set I2C3_ER_IRQHandler, Default_Handler

    .weak   SAI1_IRQHandler
    .thumb_set SAI1_IRQHandler, Default_Handler

    .weak   SAI2_IRQHandler
    .thumb_set SAI2_IRQHandler, Default_Handler

    .weak   OCTOSPI2_IRQHandler
    .thumb_set OCTOSPI2_IRQHandler, Default_Handler

    .weak   TSC_IRQHandler
    .thumb_set TSC_IRQHandler, Default_Handler

    .weak   AES_IRQHandler
    .thumb_set AES_IRQHandler, Default_Handler

    .weak   RNG_IRQHandler
    .thumb_set RNG_IRQHandler, Default_Handler

    .weak   FPU_IRQHandler
    .thumb_set FPU_IRQHandler, Default_Handler

    .weak   HASH_CRS_IRQHandler
    .thumb_set HASH_CRS_IRQHandler, Default_Handler

    .weak   I2C4_EV_IRQHandler
    .thumb_set I2C4_EV_IRQHandler, Default_Handler

    .weak   I2C4_ER_IRQHandler
    .thumb_set I2C4_ER_IRQHandler, Default_Handler

    .weak   DCMI_IRQHandler
    .thumb_set DCMI_IRQHandler, Default_Handler

    .weak   DMA2D_IRQHandler
    .thumb_set DMA2D_IRQHandler, Default_Handler

    .weak   LTDC_IRQHandler
    .thumb_set LTDC_IRQHandler, Default_Handler

    .weak   LTDC_ER_IRQHandler
    .thumb_set LTDC_ER_IRQHandler, Default_Handler

    .weak   GFXMMU_IRQHandler
    .thumb_set GFXMMU_IRQHandler, Default_Handler

    .weak   DMAMUX1_OVR_IRQHandler
    .thumb_set DMAMUX1_OVR_IRQHandler, Default_Handler
