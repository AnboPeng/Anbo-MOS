# Anbo Kernel — Porting Guide

| Field       | Value                |
|-------------|----------------------|
| Document    | PG-ANBO-PORT-001     |
| Version     | 2.1                  |
| Audience    | Firmware engineers porting Anbo to a new MCU |

---

## 1  Overview

Porting Anbo to a new target requires implementing **7 functions** and
**1 macro** declared in `anbo_arch.h`, plus providing a startup file, linker
script, and board-level hardware initialisation. The kernel source itself is
never modified.

Optionally, the port may also provide:
- **NVM Flash drivers** — for persistent configuration storage
- **ADC drivers** — for sensor peripherals
- **Additional bus drivers** — I2C, SPI, etc.

### Files to Create

```
anbo/port/<target>/
├── CMakeLists.txt          # Build integration
├── <board>_hw.h            # Board-level public API (BSP_Init, GPIO, etc.)
├── <board>_hw.c            # Board-level hardware initialisation
├── <board>_port.c          # anbo_arch.h implementation
├── <board>_uart_drv.h      # UART device driver public API
├── <board>_uart_drv.c      # UART device driver implementation
├── <board>_flash_drv.h     # Internal Flash NVM driver (optional)
├── <board>_flash_drv.c     # Internal Flash NVM implementation (optional)
├── startup_<mcu>.s         # Vector table + Reset_Handler
└── <mcu>_FLASH.ld          # Linker script
```

---

## 2  Arch HAL Functions

### 2.1  Critical Section

```c
void Anbo_Arch_Critical_Enter(void);
void Anbo_Arch_Critical_Exit(void);
```

**Requirements:**
- Must support nesting (a counter or PRIMASK save/restore).
- Must disable all maskable interrupts on entry.
- Must restore exact previous state on exit.

**Cortex-M reference implementation (PRIMASK):**

```c
static volatile uint32_t s_crit_nesting = 0;
static volatile uint32_t s_primask_saved;

void Anbo_Arch_Critical_Enter(void)
{
    uint32_t pm;
    __asm volatile ("MRS %0, PRIMASK" : "=r"(pm));
    __asm volatile ("CPSID i" ::: "memory");
    if (s_crit_nesting == 0) s_primask_saved = pm;
    s_crit_nesting++;
}

void Anbo_Arch_Critical_Exit(void)
{
    s_crit_nesting--;
    if (s_crit_nesting == 0) {
        __asm volatile ("MSR PRIMASK, %0" :: "r"(s_primask_saved) : "memory");
    }
}
```

---

### 2.2  System Tick

```c
uint32_t Anbo_Arch_GetTick(void);
```

**Requirements:**
- Return milliseconds since boot.
- Resolution: 1 ms (or better).
- Shall handle 32-bit wrap-around gracefully (~49.7 days).
- Typically driven by SysTick or a hardware timer interrupt.

**Implementation notes:**
- Maintain a `static volatile uint32_t s_tick_ms`.
- Increment in SysTick/timer ISR.
- If using low-power modes that stop SysTick, compensate the counter
  after wake-up (see Section 5).

---

### 2.3  Watchdog Feed

```c
void Anbo_Arch_WDT_Feed(void);
```

**Requirements:**
- Feed (reload/refresh) the physical hardware watchdog.
- Called only by `Anbo_WDT_Monitor()` when all software slots are healthy.
- Compiled only when `ANBO_CONF_WDT == 1`.

**Example (IWDG on STM32):**

```c
void Anbo_Arch_WDT_Feed(void)
{
    IWDG->KR = 0xAAAAu;
}
```

---

### 2.4  Low-Power Idle

```c
void Anbo_Arch_Idle(uint32_t ms);
```

**Requirements:**
- Enter the deepest practical sleep mode for up to `ms` milliseconds.
- Must return within `ms` (±1 ms acceptable).
- Must compensate the system tick on wakeup if the tick source was stopped.
- Compiled only when `ANBO_CONF_IDLE_SLEEP == 1`.
- If `ms == 0`, return immediately.

**Implementation strategy by sleep depth:**

| `ms` Range | Recommended Action |
|------------|-------------------|
| 0          | Return immediately |
| 1–2        | `__WFI()` (SysTick keeps running) |
| > 2        | Deep sleep (Stop mode) + LPTIM or RTC wakeup + tick compensation |

**Wakeup Timer Selection: LPTIM vs RTC**

The L4S5 reference implementation utilizes two timers simultaneously for separation of duties:

| Timer | Duty | Characteristics |
|--------|------|------|
| LPTIM1 | Tickless idle wakeup + Periodic IWDG feeding in deep sleep (1.5s) | Millisecond-level precision, but not available on all MCUs |
| RTC    | Deep sleep business wakeup alarm (e.g., 10 minutes) | Second-level precision, available on almost all MCUs |

**When porting to an MCU without an LPTIM, a single RTC timer can take on both roles:**

1. **`Anbo_Arch_Idle(ms)`**：Replace the LPTIM with the RTC Wakeup Timer, setting a 1-second periodic wakeup. Upon waking up, compensate the system tick and return. The RTC has second-level precision, which introduces slightly larger errors for tickless idle, but this is generally acceptable in low-power scenarios.
2. **Periodic IWDG Feeding in Deep Sleep**：Set the same RTC Wakeup Timer to a 1-second period to feed the IWDG upon each wakeup.
3. **Business Wakeup**：Utilize the RTC Alarm function (independent of the Wakeup Timer) to set an absolute time alarm, or use software to accumulate elapsed_total_s to determine timeouts.

This way, neither the kernel layer nor the application layer requires any modifications; only the BSP functions at the port layer need to be replaced.

**Tick compensation pattern:**

```c
void Anbo_Arch_Idle(uint32_t ms)
{
    if (ms == 0) return;
    if (ms <= 2) { __WFI(); return; }

    BSP_LPTIM_StartOnce(ms);       /* arm wakeup timer      */
    HAL_SuspendTick();              /* stop SysTick IRQ      */
    enter_stop_mode();              /* WFI → CPU sleeps      */
    uint32_t elapsed = BSP_LPTIM_StopAndRead();
    restore_system_clock();         /* PLL back to full speed */
    HAL_ResumeTick();
    s_tick_ms += elapsed;           /* compensate tick        */
}
```

---

### 2.5  UART Output (Fallback)

```c
void Anbo_Arch_UART_PutChar(char c);
```

**Requirements:**
- Blocking single-byte transmit on the log UART.
- Used as the last-resort fallback when the device driver is not yet ready.
- Busy-wait until the transmit register is empty, then write.

**Example:**

```c
void Anbo_Arch_UART_PutChar(char c)
{
    while (!(USARTx->ISR & USART_ISR_TXE)) {}
    USARTx->TDR = (uint8_t)c;
}
```

---

### 2.6  UART Bulk Transmit (Async)

```c
int Anbo_Arch_UART_Transmit_DMA(const uint8_t *buf, uint32_t len);
```

**Requirements:**
- Non-blocking bulk transmit via DMA or interrupt.
- Return 0 on success, -1 if the channel is busy.
- Typically delegates to the device driver's TX ring buffer and kicks the
  TXE interrupt.

**Typical implementation:**

```c
int Anbo_Arch_UART_Transmit_DMA(const uint8_t *buf, uint32_t len)
{
    Anbo_Device *dev = BSP_USART1_GetDevice();
    if (!dev) return -1;
    uint32_t written = Anbo_Dev_AsyncWrite(dev, buf, len);
    return (written > 0) ? 0 : -1;
}
```

---

### 2.7  Count Leading Zeros

```c
#define ANBO_ARCH_CLZ(x)  __builtin_clz(x)   /* GCC / Clang */
```

**Requirements:**
- Count leading zeros in a 32-bit unsigned integer.
- Map to the compiler intrinsic for maximum performance.
- Provide a software fallback for unsupported compilers.

---

## 3  UART Device Driver

The kernel uses the `Anbo_Device` abstraction for I/O. Each port must
provide at least one UART driver for log output.

### 3.1  Driver Structure

```c
/* Static device singleton */
static ANBO_RB_DEF(s_tx_rb, 9);    /* 512-byte TX ring buffer */
static ANBO_RB_DEF(s_rx_rb, 8);    /* 256-byte RX ring buffer */

static Anbo_DevOps s_ops = {
    .open  = uart_open,
    .close = uart_close,
    .write = uart_write,
    .read  = uart_read,
    .ioctl = NULL,
};

static Anbo_Device s_uart_dev = {
    .name   = "usart1",
    .ops    = &s_ops,
    .tx_rb  = &s_tx_rb,
    .rx_rb  = &s_rx_rb,
    .sig_tx = ANBO_SIG_UART_TX,
    .sig_rx = ANBO_SIG_UART_RX,
};
```

### 3.2  TX Path (Interrupt-Driven)

```
Application → Anbo_Dev_AsyncWrite() → push to tx_rb
                                     → uart_write() enables TXE interrupt
TXE ISR → pop 1 byte from tx_rb → write to TDR
        → if tx_rb empty: disable TXE, call Anbo_Dev_TxComplete()
```

### 3.3  RX Path (Interrupt-Driven)

```
RXNE ISR → read RDR → push to rx_rb → call Anbo_Dev_RxNotify()
Application → Anbo_Dev_Read() → pull from rx_rb
```

### 3.4  ISR Handler

Provide a public IRQ function that dispatches TX and RX:

```c
void BSP_USARTx_IRQ(void)
{
    uint32_t isr = USARTx->ISR;

    /* RX: byte available */
    if (isr & USART_ISR_RXNE) {
        uint8_t byte = (uint8_t)USARTx->RDR;
        Anbo_RB_PutByte(s_uart_dev.rx_rb, byte);
        Anbo_Dev_RxNotify(&s_uart_dev, 1);
    }

    /* TX: register empty */
    if ((isr & USART_ISR_TXE) && (USARTx->CR1 & USART_CR1_TXEIE)) {
        uint8_t byte;
        if (Anbo_RB_GetByte(s_uart_dev.tx_rb, &byte) == 0) {
            USARTx->TDR = byte;
        } else {
            USARTx->CR1 &= ~USART_CR1_TXEIE;
            Anbo_Dev_TxComplete(&s_uart_dev, 0);
        }
    }

    /* Error: clear all flags */
    if (isr & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE | USART_ISR_PE)) {
        USARTx->ICR = USART_ICR_ORECF | USART_ICR_FECF
                     | USART_ICR_NECF  | USART_ICR_PECF;
    }
}
```

Wire this in the vector table:

```c
void USARTx_IRQHandler(void) { BSP_USARTx_IRQ(); }
```

> **FIFO-capable USARTs (e.g. STM32L4S5):** >
> If the target MCU's USART supports a hardware FIFO, it is recommended to enable FIFO mode (`HAL_UARTEx_EnableFifoMode`) and change the RX interrupt to use a `while (RXFNE)` loop to read all buffered bytes at once. 
>
> When waking up from low-power modes like Stop 2, the FIFO can buffer multiple bytes that arrive while the CPU clock is recovering, preventing data loss. 
>
> See the L4S5 reference port (`b_l4s5i_uart_drv.c`).

---

## 4  Startup and Linker Script

### 4.1  Startup Assembly

The startup file must:

1. Set the initial stack pointer (from linker symbol `_estack`).
2. Define the vector table (`.isr_vector` section).
3. In `Reset_Handler`:
   a. Copy `.data` from FLASH to RAM.
   b. Zero `.bss` in SRAM.
   c. Zero any additional RAM sections (e.g., `.sram2_bss`).
   d. Enable FPU if applicable (write to SCB->CPACR).
   e. Call `SystemInit()` (weak).
   f. Call `__libc_init_array()`.
   g. Call `main()`.
4. Define `Default_Handler` as an infinite loop.
5. Define all ISR vectors as weak aliases to `Default_Handler`.

### 4.2  Linker Script

Define memory regions matching the target MCU:

```ld
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = <flash_size>
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = <ram_size>
    /* Optional additional RAM regions */
}
```

Required sections:

| Section        | Placement | Purpose |
|----------------|-----------|---------|
| `.isr_vector`  | FLASH     | Vector table (must be at FLASH base) |
| `.text`        | FLASH     | Code |
| `.rodata`      | FLASH     | Constants |
| `.data`        | RAM (init from FLASH) | Initialised globals |
| `.bss`         | RAM       | Zero-initialised globals |

Provide linker symbols: `_estack`, `_sdata`, `_edata`, `_sbss`, `_ebss`,
`_sidata`.

**Optional SRAM2 section** (for ECC/retained memory):

```ld
SRAM2 (rwx) : ORIGIN = 0x10000000, LENGTH = 64K

.sram2_bss (NOLOAD) : { *(.sram2_bss) } > SRAM2
```

Override `ANBO_POOL_SECTION` in the port CMakeLists.txt:

```cmake
target_compile_definitions(... PUBLIC
    ANBO_POOL_SECTION=__attribute__\(\(section\(".sram2_bss"\)\)\)
)
```

---

## 5  Board Hardware Init

Provide a `BSP_Init()` function that configures all required hardware:

### 5.1  Minimum Hardware Checklist

| Item | Purpose | Notes |
|------|---------|-------|
| System clock | Full-speed operation | Configure PLL, flash wait states, voltage scaling |
| SysTick | 1 ms tick | Drives `Anbo_Arch_GetTick()` |
| FPU | Floating-point support | Enable CP10/CP11 (Cortex-M4F/M7F) |
| GPIO | LED, button | Application-specific |
| UART | Serial console (VCP) | For log output; configure TX/RX pins, baud rate |
| NVIC | Interrupt priorities | UART < Button < LPTIM (lower number = higher priority) |

### 5.2  Optional Hardware

| Item | Purpose | Required By |
|------|---------|-------------|
| IWDG/WWDG | Hardware watchdog | `ANBO_CONF_WDT == 1` |
| LPTIM / RTC | Low-power wakeup timer | `ANBO_CONF_IDLE_SLEEP == 1` (deep sleep) |
| EXTI | External interrupt (button) | Application-specific |
| I2C | Sensor communication (e.g. IMU) | Application-specific |
| IMU (e.g. LSM6DSL) | Vibration detection + deep sleep wake | I2C bus + INT pin (EXTI) |

---

## 6  CMake Integration

### 6.1  Port CMakeLists.txt Template

```cmake
# anbo/port/<target>/CMakeLists.txt

add_library(anbo_port_<target> STATIC
    <board>_port.c
    <board>_hw.c
    <board>_uart_drv.c
    startup_<mcu>.s
    ${VENDOR_HAL_SRCS}
)

target_include_directories(anbo_port_<target> PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../kernel/include
    ${VENDOR_HAL_INCLUDE_DIRS}
)

target_compile_definitions(anbo_port_<target> PUBLIC
    <MCU_DEFINE>                    # e.g. STM32L4S5xx
    USE_HAL_DRIVER                  # if using vendor HAL
    ANBO_POOL_SECTION=__attribute__\(\(section\(".sram2_bss"\)\)\)
)

# Export linker script path to top-level
set(PORT_LINKER_SCRIPT
    ${CMAKE_CURRENT_SOURCE_DIR}/<mcu>_FLASH.ld
    CACHE FILEPATH "" FORCE)
```

### 6.2  Top-Level CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.18)
project(<project_name> C ASM)

add_subdirectory(anbo)

add_executable(firmware
    app/main.c
    anbo/port/<target>/syscalls.c    # newlib stubs
)

target_link_libraries(firmware PRIVATE
    anbo_port_<target>
    anbo_kernel
)

target_link_options(firmware PRIVATE
    -T${PORT_LINKER_SCRIPT}
    -Wl,-Map=${CMAKE_BINARY_DIR}/firmware.map,--cref
    -Wl,--print-memory-usage
)
```

---

## 7  Porting Checklist

Use this checklist when porting to a new MCU:

- [ ] **Create port directory**: `anbo/port/<target>/`
- [ ] **Startup assembly**: vector table, Reset_Handler, `.data` copy, `.bss` zero
- [ ] **Linker script**: memory regions, sections, stack/heap symbols
- [ ] **Board hardware init** (`BSP_Init`): clock tree, GPIO, UART pins, NVIC
- [ ] **`Anbo_Arch_Critical_Enter/Exit`**: PRIMASK or equivalent
- [ ] **`Anbo_Arch_GetTick`**: SysTick counter with ISR
- [ ] **UART driver**: `Anbo_Device` with TX/RX ring buffers, ISR handler
- [ ] **`Anbo_Arch_UART_PutChar`**: blocking fallback TX
- [ ] **`Anbo_Arch_UART_Transmit_DMA`**: async TX via device driver
- [ ] **`Anbo_Arch_WDT_Feed`**: hardware watchdog reload (if `ANBO_CONF_WDT`)
- [ ] **`Anbo_Arch_Idle`**: WFI or deep sleep + tick compensation (if `ANBO_CONF_IDLE_SLEEP`)
- [ ] **`ANBO_ARCH_CLZ`**: compiler intrinsic mapping
- [ ] **CMakeLists.txt**: port library, include paths, linker script export
- [ ] **syscalls.c**: newlib stubs (if using `-specs=nano.specs`)
- [ ] **HardFault handler**: naked asm trampoline + C handler printing CFSR/HFSR/PC/LR/R0/R12/BFAR/MSP via blocking UART. Essential for post-mortem crash diagnosis.
- [ ] **Build and test**: zero warnings, verify serial output

### 7.2  Optional Features

- [ ] **NVM Flash driver**: `BSP_Flash_ReadLatest` / `BSP_Flash_WriteAppend` for persistent config (supports multi-page rotation via `APP_CONF_PARAM_FLASH_INT_PAGES`)
- [ ] **Pool section placement**: define `ANBO_POOL_SECTION` if using dedicated RAM
- [ ] **ADC driver**: for sensor peripherals (application-level)
- [ ] **External Flash chip driver** (`BSP_OSPI_Init/Read/PageProgram/SectorErase`): OCTOSPI bus + NOR Flash chip primitives — replace this file to support a different chip
- [ ] **External Flash NVM layer** (`BSP_OSPI_Flash_Init/BSP_Flash_ReadLatest/WriteAppend`): chip-agnostic log-structured append + multi-sector rotation via `APP_CONF_PARAM_FLASH_EXT_PAGES`
- [ ] **Multi-WDT slots**: register per-module WDT slots (e.g. sensor, controller) for silent-failure detection beyond the main-loop slot
- [ ] **Deep sleep module**: long-press entry, IWDG strategy selection (`APP_CONF_SLEEP_FREEZE_IWDG`), RTC wakeup timer for auto-wake (or LPTIM cumulative timeout as fallback on platforms with LPTIM). Auto deep-sleep on stable temperature (60 s window, ±1.0 °C). RTC wakeup ADC check (re-sleep if stable, full wake if delta ≥ 1.0 °C). UART TX drain (Flush loop + hardware TC flag) before entering Stop 2. **Note:** each wakeup source needs its EXTI line unmasked (e.g. EXTI 20 for RTC, EXTI 26 for USART1, EXTI 32 for LPTIM1).
- [ ] **I2C driver** (`BSP_I2C_Init/Read/Write`): I2C master communication for external sensors (e.g. LSM6DSL IMU)
- [ ] **IMU driver** (`BSP_IMU_Init/ReadFIFO/ConfigWakeup/ReadWakeUpSrc`): sensor FIFO readout, vibration detection, hardware wake-up configuration for deep sleep. Replace for different IMU chips.

---

## 8  Validation

After porting, verify:

| Test | Expected Result |
|------|-----------------|
| Boot banner on serial | `"Anbo Kernel vX.X"` appears at configured baud rate |
| Log output | `ANBO_LOGI("hello")` prints `[I] hello\r\n` |
| Timer accuracy | 1-second periodic timer fires ±1 ms |
| Button event | EXTI fires → event bus delivers signal |
| Watchdog reset | Comment out `Anbo_WDT_Checkin()` for one slot → system resets after timeout |
| Multi-slot WDT | Register sensor + controller slots; stop sensor timer → sensor slot timeout → IWDG reset |
| HardFault handler | Force a NULL pointer dereference → serial prints CFSR, PC, LR with correct addresses |
| Low-power idle | Current drops during Stop mode; wakes on timer |
| Tick compensation | `Anbo_Arch_GetTick()` stays accurate after deep sleep |
| RTC wakeup | `App_Sleep_SetTimeout(600)` → device wakes after 10 min, performs ADC check |
| RTC EXTI 20 | Verify `EXTI->IMR1 & EXTI_IMR1_IM20` is set after `BSP_RTC_Init()` |
| Auto deep-sleep | Keep temperature stable (±1.0 °C) for 60 s → system enters deep sleep automatically |
| RTC ADC re-sleep | During deep sleep, RTC wakes → temp stable → logs "OK, re-sleeping" → continues |
| RTC ADC full wake | During deep sleep, RTC wakes → temp changed ≥ 1.0 °C → full wake with WARN log |
| UART TX drain | All log messages before sleep appear without garbled characters on terminal |

### 8.2  Pool Async Path (if `ANBO_CONF_POOL == 1`)

| Test | Expected Result |
|------|-----------------|
| Pool alloc/free | `Anbo_Pool_Alloc()` returns non-NULL; `FreeCount` decrements/increments correctly |
| EvtQ post/get | Posted event pointer is retrieved in FIFO order |
| Pool dispatch | `Anbo_Pool_Dispatch()` sets ref_count=1, delivers event to all EBus subscribers, then calls `Release` (auto-frees if no subscriber retained) |
| Multi-subscriber | Same pooled event is received by 2+ subscribers without corruption |
| Pool exhaustion | `Anbo_Pool_Alloc()` returns NULL when all blocks are in use |
| Queue full | `Anbo_EvtQ_Post()` returns -1 when queue is full; event is not lost (caller handles) |

### 8.3  NVM Persistence (if `APP_CONF_PARAM_FLASH == 1`)

| Test | Expected Result |
|------|-----------------|
| First boot | Default config written: magic valid, CRC32 correct |
| Config save/reload | Modify threshold, save, reboot — threshold survives |
| Corruption recovery | Corrupt CRC byte in Flash → defaults restored on next boot |
| Page full wrap | Fill NVM page with writes → rotate to next page (if multi-page) or erase + rewrite (single page) |
| Multi-page rotation | With `INT_PAGES=2`: fill page 0 → auto-rotate to page 1 → fill page 1 → erase page 0 → rotate back |
| Record size check | Change `App_Config` to non-divisible size → compile error from static assert |
