# ==================================================================
# Anbo Port — STM32L4S5 (B-L4S5I-IOT01A) porting layer
# ==================================================================
# b_l4s5i_port.c  — Anbo arch HAL implementation (critical, tick, wdt, idle)
# b_l4s5i_hw.c    — Board hardware init (clock, GPIO, USART1 VCP)
# ==================================================================
cmake_minimum_required(VERSION 3.18)

# ==================================================================
#  STM32 HAL & CMSIS — auto-download individual packages from GitHub
# ==================================================================
# Downloads three lightweight packages instead of the STM32CubeL4
# mega-repo (which uses git submodules that tar.gz cannot resolve):
#
#   1. stm32l4xx_hal_driver  — HAL/LL source + headers
#   2. cmsis_device_l4       — STM32L4xx device headers + system source
#   3. CMSIS_5               — ARM CMSIS Core headers (core_cm4.h etc.)
#
# Options:
#   -DSTM32_FORCE_DOWNLOAD=ON   Force re-download (upgrade)
# ==================================================================

set(_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Lib")

# Package versions (user-overridable)
if(NOT DEFINED STM32_HAL_TAG)
    set(STM32_HAL_TAG "v1.13.5")
endif()
if(NOT DEFINED STM32_CMSIS_DEV_TAG)
    set(STM32_CMSIS_DEV_TAG "v1.7.3")
endif()
if(NOT DEFINED ARM_CMSIS_TAG)
    set(ARM_CMSIS_TAG "5.9.0")
endif()

option(STM32_FORCE_DOWNLOAD "Force re-download / upgrade SDK packages" OFF)

# ------------------------------------------------------------------
#  Helper: download and extract a GitHub archive
#  _dl_pkg(<display_name> <github_org/repo> <tag> <dest_dir>)
# ------------------------------------------------------------------
macro(_dl_pkg _NAME _REPO _TAG _DEST)
    set(_MARKER "${_DEST}/.pkg_ready")

    if(STM32_FORCE_DOWNLOAD AND EXISTS "${_DEST}")
        file(REMOVE_RECURSE "${_DEST}")
    endif()

    if(NOT EXISTS "${_MARKER}")
        set(_URL "https://github.com/${_REPO}/archive/refs/tags/${_TAG}.tar.gz")
        set(_TGZ "${_LIB_DIR}/${_NAME}-${_TAG}.tar.gz")

        message(STATUS "[Port] ${_NAME} not found — downloading ${_TAG} ...")
        file(MAKE_DIRECTORY "${_LIB_DIR}")
        file(DOWNLOAD "${_URL}" "${_TGZ}"
             STATUS _dl_status SHOW_PROGRESS TLS_VERIFY ON)
        list(GET _dl_status 0 _dl_code)
        if(NOT _dl_code EQUAL 0)
            list(GET _dl_status 1 _dl_msg)
            message(FATAL_ERROR "[Port] Download ${_NAME} failed (${_dl_code}): ${_dl_msg}\n"
                                "       URL: ${_URL}")
        endif()

        message(STATUS "[Port] Extracting ${_NAME} ...")
        file(ARCHIVE_EXTRACT INPUT "${_TGZ}" DESTINATION "${_LIB_DIR}")
        file(REMOVE "${_TGZ}")

        # GitHub may transform the extracted directory name:
        #   - strips leading "v" from tag  (v1.13.5 → 1.13.5)
        #   - converts underscores to dashes in repo name
        # Try all four combinations to find the extracted folder.
        string(REGEX REPLACE "^.+/" "" _REPO_NAME "${_REPO}")
        string(REPLACE "_" "-" _REPO_NAME_DASH "${_REPO_NAME}")
        string(REGEX REPLACE "^v" "" _TAG_STRIPPED "${_TAG}")

        set(_CANDIDATES
            "${_LIB_DIR}/${_REPO_NAME}-${_TAG}"
            "${_LIB_DIR}/${_REPO_NAME}-${_TAG_STRIPPED}"
            "${_LIB_DIR}/${_REPO_NAME_DASH}-${_TAG}"
            "${_LIB_DIR}/${_REPO_NAME_DASH}-${_TAG_STRIPPED}")

        set(_EXTRACTED "")
        foreach(_CAND ${_CANDIDATES})
            if(EXISTS "${_CAND}")
                set(_EXTRACTED "${_CAND}")
                break()
            endif()
        endforeach()

        if(NOT _EXTRACTED STREQUAL "" AND NOT EXISTS "${_DEST}")
            file(RENAME "${_EXTRACTED}" "${_DEST}")
        elseif(NOT EXISTS "${_DEST}")
            message(FATAL_ERROR "[Port] Cannot find extracted folder for ${_NAME} in ${_LIB_DIR}")
        endif()

        file(WRITE "${_MARKER}" "tag=${_TAG}\n")
        message(STATUS "[Port] ${_NAME} ${_TAG} ready at ${_DEST}")
    else()
        message(STATUS "[Port] ${_NAME} found — skipping download")
    endif()
endmacro()

# ---- Download the three packages ----
set(HAL_DIR       "${_LIB_DIR}/stm32l4xx_hal_driver")
set(CMSIS_DEV_DIR "${_LIB_DIR}/cmsis_device_l4")
set(CMSIS5_DIR    "${_LIB_DIR}/CMSIS_5")

_dl_pkg("stm32l4xx_hal_driver"
        "STMicroelectronics/stm32l4xx_hal_driver" "${STM32_HAL_TAG}" "${HAL_DIR}")
_dl_pkg("cmsis_device_l4"
        "STMicroelectronics/cmsis_device_l4"      "${STM32_CMSIS_DEV_TAG}" "${CMSIS_DEV_DIR}")
_dl_pkg("CMSIS_5"
        "ARM-software/CMSIS_5"                    "${ARM_CMSIS_TAG}" "${CMSIS5_DIR}")

# ==================================================================
#  Derived include / source paths
# ==================================================================
set(CUBE_HAL_INC     "${HAL_DIR}/Inc")
set(CUBE_HAL_SRC     "${HAL_DIR}/Src")
set(CMSIS_CORE_INC   "${CMSIS5_DIR}/CMSIS/Core/Include")
set(CMSIS_DEV_INC    "${CMSIS_DEV_DIR}/Include")
set(CMSIS_DEV_SRC    "${CMSIS_DEV_DIR}/Source/Templates")

# ==================================================================
#  STM32 HAL driver sources (only the modules we use)
# ==================================================================
set(HAL_SRCS
    ${CUBE_HAL_SRC}/stm32l4xx_hal.c
    ${CUBE_HAL_SRC}/stm32l4xx_hal_cortex.c
    ${CUBE_HAL_SRC}/stm32l4xx_hal_rcc.c
    ${CUBE_HAL_SRC}/stm32l4xx_hal_rcc_ex.c
    ${CUBE_HAL_SRC}/stm32l4xx_hal_pwr.c
    ${CUBE_HAL_SRC}/stm32l4xx_hal_pwr_ex.c
    ${CUBE_HAL_SRC}/stm32l4xx_hal_gpio.c
    ${CUBE_HAL_SRC}/stm32l4xx_hal_dma.c
    ${CUBE_HAL_SRC}/stm32l4xx_hal_uart.c
    ${CUBE_HAL_SRC}/stm32l4xx_hal_uart_ex.c
    ${CUBE_HAL_SRC}/stm32l4xx_hal_flash.c
    ${CUBE_HAL_SRC}/stm32l4xx_hal_flash_ex.c
    ${CUBE_HAL_SRC}/stm32l4xx_hal_flash_ramfunc.c
    ${CUBE_HAL_SRC}/stm32l4xx_hal_exti.c
    ${CUBE_HAL_SRC}/stm32l4xx_hal_iwdg.c
    ${CUBE_HAL_SRC}/stm32l4xx_hal_lptim.c
    ${CUBE_HAL_SRC}/stm32l4xx_hal_ospi.c
    ${CUBE_HAL_SRC}/stm32l4xx_hal_adc.c
    ${CUBE_HAL_SRC}/stm32l4xx_hal_adc_ex.c
    ${CUBE_HAL_SRC}/stm32l4xx_hal_rtc.c
    ${CUBE_HAL_SRC}/stm32l4xx_hal_rtc_ex.c
)

# CMSIS device system source
set(CMSIS_SRCS
    ${CMSIS_DEV_SRC}/system_stm32l4xx.c
)

# ==================================================================
#  Port static library
# ==================================================================
add_library(anbo_port_stm32l4s5 STATIC
    b_l4s5i_port.c
    b_l4s5i_hw.c
    b_l4s5i_uart_drv.c
    b_l4s5i_flash_drv.c
    b_l4s5i_ospi_drv.c
    b_l4s5i_ospi_flash_drv.c
    b_l4s5i_log_flash.c
    startup_stm32l4s5xx.s
    ${HAL_SRCS}
    ${CMSIS_SRCS}
)

target_include_directories(anbo_port_stm32l4s5 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}                         # b_l4s5i_hw.h, stm32l4xx_hal_conf.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../kernel/include     # anbo_*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../app             # app_config.h
    ${CUBE_HAL_INC}                                      # stm32l4xx_hal.h etc.
    ${CMSIS_CORE_INC}                                    # core_cm4.h etc.
    ${CMSIS_DEV_INC}                                     # stm32l4s5xx.h etc.
)

target_compile_definitions(anbo_port_stm32l4s5 PUBLIC
    STM32L4S5xx
    USE_HAL_DRIVER
    ANBO_POOL_SECTION=__attribute__\(\(section\(".sram2_bss"\)\)\)
)

target_link_libraries(anbo_port_stm32l4s5 PUBLIC
    anbo_kernel
)

# Linker script path (exported to parent for firmware linking)
set(PORT_LINKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/STM32L4S5VITx_FLASH.ld"
    CACHE STRING "Linker script" FORCE)

# Compiler warnings (same as kernel)
target_compile_options(anbo_port_stm32l4s5 PRIVATE
    $<$<C_COMPILER_ID:GNU>:-Wall -Wextra -Wpedantic -Werror
        -Wno-unused-parameter>
    $<$<C_COMPILER_ID:Clang>:-Wall -Wextra -Wpedantic -Werror
        -Wno-unused-parameter>
)
