# Anbo Kernel — Requirements Specification

| Field       | Value                |
|-------------|----------------------|
| Language    | C99                  |
| Target      | ARM Cortex-M (bare-metal) |

---

## 1  Purpose

Anbo is a lightweight, event-driven microkernel for resource-constrained
bare-metal embedded systems. It provides cooperative multitasking through an
event-bus publish/subscribe model, software timers, a hierarchical finite
state machine (FSM), asynchronous device I/O, async logging, a software
watchdog, a fixed-size block memory pool with async event queue, and
instrumentation trace hooks — all without dynamic memory allocation.

---

## 2  Design Constraints

| ID       | Constraint                                |
|----------|-------------------------------------------|
| DC-001   | Pure C99 — no C++ or compiler extensions required |
| DC-002   | Zero `malloc` / `free` — fully static memory |
| DC-003   | Zero `stdio.h` dependency (`printf`, `sprintf`, `scanf` forbidden) |
| DC-004   | All pool sizes configurable at compile time via `anbo_config.h` |
| DC-005   | ISR-safe public APIs (critical-section protection or lock-free) |
| DC-006   | Hardware abstraction through `anbo_arch.h` — kernel never touches registers |
| DC-007   | Single-header per module, no circular includes |
| DC-008   | Cooperative scheduling only (no preemption, no context switch) |

---

## 3  Module Overview

```
anbo_config.h  ← global compile-time configuration
anbo_arch.h    ← hardware abstraction (port implements)
anbo_list.h    ← intrusive doubly-linked circular list
anbo_rb.h      ← static lock-free ring buffer (SPSC)
anbo_ebus.h    ← event bus (publish / subscribe)
anbo_fsm.h     ← finite state machine (event-driven)
anbo_timer.h   ← software timers (one-shot / periodic)
anbo_dev.h     ← asynchronous device abstraction
anbo_log.h     ← async logging (zero-stdio formatting) + trace hooks
anbo_wdt.h     ← software watchdog monitor
anbo_pool.h    ← fixed-size block pool + async event pointer queue
