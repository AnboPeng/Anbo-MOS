# Anbo Kernel — Porting Quick Tips

| Field       | Value                        |
|-------------|------------------------------|
| Document    | PG-ANBO-TIPS-001             |
| Version     | 1.0                          |
| Audience    | Engineers who want to **pick & choose** kernel modules |
| See Also    | `PORTING_GUIDE.md` (full port walkthrough) |

> **TL;DR** — You do NOT have to implement the entire `anbo_arch.h` if you
> only need a subset of the kernel. Some modules have **zero** platform
> dependencies and work on any C99 toolchain out of the box.

---

## 1  Module Dependency Matrix

| Module | Source | Arch Functions Required | Config Gate | Standalone? |
|--------|--------|------------------------|-------------|:-----------:|
| **List** | `anbo_list.h` (header-only) | *None* | — | **YES** |
| **Ring Buffer** | `anbo_rb.c` | *None* | — | **YES** |
| **Event Bus** | `anbo_ebus.c` | `Critical_Enter/Exit` | — | No |
| **FSM** | `anbo_fsm.c` | *(indirect via EBus)* | — | Partial |
| **Timer** | `anbo_timer.c` | `Critical_Enter/Exit`, `GetTick` | — | No |
| **Pool** | `anbo_pool.c` | `Critical_Enter/Exit` | `ANBO_CONF_POOL` | No |
| **Log** | `anbo_log.c` | `Critical_Enter/Exit`, `UART_PutChar`, `UART_Transmit_DMA` | — | No |
| **Watchdog** | `anbo_wdt.c` | `GetTick`, `WDT_Feed` | `ANBO_CONF_WDT` | No |
| **Device** | `anbo_dev.h` (header-only) | *None* | — | **Optional** |

---

## 2  Three Tiers of Adoption

### Tier 0 — Zero Porting (drop-in)

Use **List** and **Ring Buffer** as plain C99 data-structure libraries.
No `anbo_arch.h` implementation needed. No configuration macros needed.

```c
#include "anbo_list.h"
#include "anbo_rb.h"

ANBO_RB_DEF(my_rb, 8);          /* 256-byte ring buffer */
Anbo_RB_PutByte(my_rb, 0xAB);
```

### Tier 1 — Minimal Porting (2 functions)

Implement only the critical section pair:

```c
void Anbo_Arch_Critical_Enter(void) { __disable_irq(); }
void Anbo_Arch_Critical_Exit(void)  { __enable_irq();  }
```

This unlocks:

| Module | Capability |
|--------|-----------|
| **Event Bus** | Publish/subscribe signals |
| **Pool** | Ref-counted block allocator + async event queue |
| **FSM** | Hierarchical state machine (uses EBus) |

Add `Anbo_Arch_GetTick()` (return a millisecond counter) to also unlock
**Timer** (software one-shot / periodic timers).

### Tier 2 — Full Porting (5–6 functions)

Implement the complete `anbo_arch.h` interface to unlock all modules:

| Function | Module(s) | Notes |
|----------|-----------|-------|
| `Critical_Enter/Exit` | EBus, Pool, Timer, Log | Must support nesting |
| `GetTick()` | Timer, WDT | Millisecond resolution |
| `UART_PutChar(c)` | Log | Byte-level fallback output |
| `UART_Transmit_DMA(buf,len)` | Log | Bulk output (can stub to PutChar loop) |
| `WDT_Feed()` | WDT | Optional: set `ANBO_CONF_WDT=0` to skip |
| `Idle(ms)` | Idle loop | Optional: set `ANBO_CONF_IDLE_SLEEP=0` to skip |

See `PORTING_GUIDE.md` §2 for detailed requirements of each function.

---

## 3  Disabling Unused Modules

Create `anbo_config.h` (or override via compiler `-D` flags):

```c
/* Only need EBus + Timer — disable the rest */
#define ANBO_CONF_POOL          0   /* Turns anbo_pool.c into an empty TU */
#define ANBO_CONF_WDT           0   /* Turns anbo_wdt.c into an empty TU */
#define ANBO_CONF_IDLE_SLEEP    0   /* No Anbo_Arch_Idle() needed */
#define ANBO_CONF_TRACE         0   /* Strip all trace macros */
```

When a module is gated to 0, its `.c` compiles to nothing — zero code size,
zero arch dependencies. You only need to implement the `Anbo_Arch_*`
functions that **enabled** modules actually call.

---

## 4  Anbo_Device — Optional Peripheral Abstraction

`Anbo_Device` is a **header-only** abstraction (`anbo_dev.h`). It provides:

- Unified `Open / Close / Read / Write / Ioctl` API via function-pointer vtable
- Automatic EBus signal publishing on RX/TX events (`sig_rx`, `sig_tx`)
- Ring buffer integration for async I/O

**You do NOT need Anbo_Device to access peripherals.** You can call your
own HAL / register-level code directly. The kernel modules (EBus, Timer,
Pool, etc.) have no dependency on Anbo_Device.

If you choose to use it, fill in an `Anbo_DevOps` struct for your driver:

```c
static const Anbo_DevOps my_uart_ops = {
    .open  = my_uart_open,
    .close = my_uart_close,
    .write = my_uart_write,
    .read  = my_uart_read,
    .ioctl = NULL,
};

static uint8_t rx_buf[256];
ANBO_RB_DEF(my_rx_rb, 8);      /* 2^8 = 256 */

Anbo_Device my_uart = {
    .name    = "uart0",
    .ops     = &my_uart_ops,
    .rx_rb   = my_rx_rb,
    .sig_rx  = 0x0010,          /* ANBO_SIG_UART_RX */
};
```

---

## 5  Quick-Start Checklist

```
[ ] Decide which tier you need (0 / 1 / 2)
[ ] Copy anbo/kernel/ into your project
[ ] Set ANBO_CONF_* macros to disable unneeded modules
[ ] Implement required Anbo_Arch_* stubs (see Tier table above)
[ ] Include headers, link .c files, build
[ ] (Optional) Wire up Anbo_Device for peripheral abstraction
```

