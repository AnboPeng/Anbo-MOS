# Anbo Kernel

A lightweight, event-driven embedded microkernel for resource-constrained
Cortex-M microcontrollers.

**Language**: C99 &nbsp;|&nbsp; **Memory**: Fully static &nbsp;|&nbsp;
**License**: MIT

## Features

- **Zero dynamic memory** — all pools, queues and buffers are sized at
  compile time; no `malloc`, no `sprintf`.
- **ISR-safe throughout** — critical sections use nested PRIMASK
  save/restore; ring buffers are lock-free SPSC.
- **Event-driven architecture** — publish/subscribe Event Bus with
  hash-bucketed O(1) routing, async event pool with Retain/Release
  reference counting.
- **Soft / hard timers** — one-shot and periodic timers managed via sorted
  intrusive linked list.  `ANBO_CONF_TIMER_ISR=1` (default) drives timers from
  SysTick ISR for 1 ms accuracy independent of main-loop latency; set to 0 for
  traditional main-loop polling.
- **Deep-sleep timer compensation** — `Anbo_Timer_CompensateAll()` snaps all
  running timer deadlines forward after a long sleep, preventing callback storms
  on wake.
- **Finite State Machines** — object-oriented FSMs with entry/exit/event
  callbacks and auto-subscription to EBus signals.
- **Async logging** — ring-buffered, non-blocking `printf`-style API with
  configurable sinks (UART, Flash).
- **Software watchdog** — multi-slot task-level monitor on top of the
  hardware IWDG.
- **Compile-time configuration** — every feature can be enabled/disabled
  or tuned via `ANBO_CONF_*` macros.

## Directory Structure

```
anbo/
├── CMakeLists.txt                  # Kernel static library build
├── kernel/
│   ├── include/
│   │   ├── anbo_arch.h             # Hardware abstraction interface
│   │   ├── anbo_config.h           # Compile-time configuration
│   │   ├── anbo_dev.h              # Abstract device (vtable I/O)
│   │   ├── anbo_ebus.h             # Event Bus (pub/sub)
│   │   ├── anbo_fsm.h              # Finite State Machine
│   │   ├── anbo_list.h             # Intrusive doubly-linked list
│   │   ├── anbo_log.h              # Async logging
│   │   ├── anbo_pool.h             # Fixed-block pool + event queue
│   │   ├── anbo_rb.h               # Lock-free ring buffer
│   │   ├── anbo_timer.h            # Software timers
│   │   └── anbo_wdt.h              # Software watchdog monitor
│   └── src/
│       ├── anbo_ebus.c
│       ├── anbo_fsm.c
│       ├── anbo_log.c
│       ├── anbo_pool.c
│       ├── anbo_rb.c
│       ├── anbo_timer.c
│       └── anbo_wdt.c
└── port/
    └── stm32l4s5/                  # B-L4S5I-IOT01A board port
        ├── CMakeLists.txt           # Port build + auto-download SDK
        ├── b_l4s5i_port.c           # Arch HAL (tick, critical, idle)
        ├── b_l4s5i_hw.c/h           # Clock, GPIO, USART, EXTI init
        ├── b_l4s5i_uart_drv.c/h     # DMA-driven UART driver
        ├── b_l4s5i_i2c_drv.c/h      # I2C2 master driver (LSM6DSL)
        ├── b_l4s5i_imu_drv.c/h      # LSM6DSL 6-axis IMU driver (FIFO + wake-up)
        ├── b_l4s5i_flash_drv.c/h    # Internal Flash driver
        ├── b_l4s5i_ospi_drv.c/h     # OSPI NOR controller
        ├── b_l4s5i_ospi_flash_drv.c/h # OSPI NOR Flash driver
        ├── b_l4s5i_log_flash.c/h    # Persistent log storage
        ├── startup_stm32l4s5xx.s    # Vector table & startup
        ├── STM32L4S5VITx_FLASH.ld   # Linker script
        └── Lib/                     # Auto-downloaded HAL/CMSIS
```

## Modules

| Module | Header | Description |
|--------|--------|-------------|
| **List** | `anbo_list.h` | Linux-style intrusive doubly-linked circular list |
| **Ring Buffer** | `anbo_rb.h` | Lock-free SPSC FIFO (ISR ↔ main safe) |
| **Arch HAL** | `anbo_arch.h` | Critical section, tick, watchdog, idle, UART — implemented by each port |
| **Timer** | `anbo_timer.h` | One-shot & periodic timers; ISR-driven (hard) or main-loop (soft); deep-sleep compensation |
| **Event Bus** | `anbo_ebus.h` | Many-to-many pub/sub with hash-bucketed routing |
| **FSM** | `anbo_fsm.h` | State machine with entry/exit/event handlers |
| **Log** | `anbo_log.h` | `ANBO_LOGI()`/`LOGW()`/`LOGE()`/`LOGD()` — async ring buffer → sink |
| **Pool** | `anbo_pool.h` | Fixed-block allocator + async event queue + Retain/Release |
| **Watchdog** | `anbo_wdt.h` | Multi-slot SW monitor; calls `Anbo_Arch_WDT_Feed()` |
| **Device** | `anbo_dev.h` | Abstract device interface (open/close/read/write/ioctl vtable) |

## Quick Start

### 1. Add as a Git Submodule

```bash
git submodule add <repo-url> anbo
```

### 2. CMake Integration

```cmake
add_subdirectory(anbo)
target_link_libraries(my_app
    anbo_kernel
    anbo_port_stm32l4s5   # or your own port
)
```

### 3. Minimal main.c

```c
#include "anbo_config.h"
#include "anbo_ebus.h"
#include "anbo_timer.h"
#include "anbo_log.h"
#include "b_l4s5i_hw.h"

int main(void)
{
    BSP_Init();            /* Port: clock, GPIO, UART */
    Anbo_Log_Init();
    Anbo_EBus_Init();
    Anbo_Timer_Init();

    ANBO_LOGI("System boot");

    for (;;) {
#if !ANBO_CONF_TIMER_ISR
        Anbo_Timer_Update(Anbo_Arch_GetTick());  /* soft-timer mode only */
#endif
        Anbo_Log_Flush();
    }
}
```

## Configuration

All options live in `anbo_config.h` and can be overridden with `-D` flags.

| Macro | Default | Description |
|-------|---------|-------------|
| `ANBO_CONF_IDLE_SLEEP` | 1 | Low-power idle when no timers pending |
| `ANBO_CONF_TIMER_ISR` | 1 | Hard-timer mode: drive timers from SysTick ISR (1 ms accuracy) |
| `ANBO_CONF_WDT` | 1 | Software watchdog monitor |
| `ANBO_CONF_MAX_TIMERS` | 16 | Soft-timer pool capacity |
| `ANBO_CONF_TICK_HZ` | 1000 | System tick frequency (Hz) |
| `ANBO_CONF_LOG_RB_ORDER` | 10 | Log ring buffer size (2^N bytes) |
| `ANBO_CONF_TRACE` | 1 | EBus / FSM trace hooks |
| `ANBO_CONF_POOL` | 1 | Async event pool + queue |
| `ANBO_CONF_POOL_BLOCK_SIZE` | 64 | Pool block size (bytes) |
| `ANBO_CONF_POOL_BLOCK_COUNT` | 20 | Pool block count |
| `ANBO_CONF_EVTQ_DEPTH` | 32 | Async event queue depth (power of 2) |

## Port Layer

The kernel is hardware-agnostic. Each port implements the functions
declared in `anbo_arch.h`:

| Function | Required | Purpose |
|----------|----------|---------|
| `Anbo_Arch_Critical_Enter/Exit()` | Yes | ISR-safe critical section |
| `Anbo_Arch_GetTick()` | Yes | Monotonic millisecond counter |
| `Anbo_Arch_WDT_Feed()` | If WDT=1 | Feed hardware watchdog |
| `Anbo_Arch_Idle()` | If IDLE=1 | Low-power wait (WFI / Stop) |
| `Anbo_Arch_UART_PutChar()` | If Log | Byte-level UART fallback |

### STM32L4S5 Port

The included port targets the **B-L4S5I-IOT01A** discovery board:

- 120 MHz MSI + PLL system clock
- SysTick 1 ms tick
- USART1 VCP (115200-8N1) with DMA TX
- IWDG (2 s timeout, 32 kHz LSI)
- Stop 2 low-power idle with LPTIM1 wakeup
- Internal Flash + external OSPI NOR (MX25R6435F) drivers
- STM32 HAL / CMSIS auto-downloaded by CMake on first build

## Documentation

- [ Anbo Kernel Porting Tips](doc/PORTING_TIPS.md)
- [ Anbo Kernel Porting Guide](doc/PORTING_GUIDE.md)

## License

MIT — see [LICENSE](../LICENSE).

Copyright (c) 2026 Anbo Peng
