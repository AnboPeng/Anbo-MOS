# ==================================================================
# Anbo Kernel — Top-level CMake build
# ==================================================================
cmake_minimum_required(VERSION 3.18)
project(anbo C ASM)

# ---- C99 standard ----
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# ---- Kernel static library ----
add_library(anbo_kernel STATIC
    kernel/src/anbo_rb.c
    kernel/src/anbo_ebus.c
    kernel/src/anbo_timer.c
    kernel/src/anbo_fsm.c
    kernel/src/anbo_wdt.c
    kernel/src/anbo_log.c
    kernel/src/anbo_pool.c
)

target_include_directories(anbo_kernel PUBLIC
    kernel/include
)

# ---- Compiler warnings ----
target_compile_options(anbo_kernel PRIVATE
    $<$<C_COMPILER_ID:GNU>:-Wall -Wextra -Wpedantic -Werror>
    $<$<C_COMPILER_ID:Clang>:-Wall -Wextra -Wpedantic -Werror>
)

# ---- Optional: platform port layer ----
# When porting to STM32L4S5 / B-L4S5I-IOT01A, uncomment the line below:
add_subdirectory(port/stm32l4s5)
