cmake_minimum_required(VERSION 3.20)

# ============================================================================
# UFI Flux Engine - STM32H723 Firmware
# ============================================================================

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# Toolchain
set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
set(CMAKE_OBJCOPY arm-none-eabi-objcopy)
set(CMAKE_OBJDUMP arm-none-eabi-objdump)
set(CMAKE_SIZE arm-none-eabi-size)

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

project(ufi_firmware C ASM)

# Firmware version + git revision for GET_INFO (revision is taken at configure time)
set(UFI_FW_VERSION "1.3")
execute_process(COMMAND git rev-parse --short HEAD WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                OUTPUT_VARIABLE UFI_GIT_REV OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT UFI_GIT_REV)
    set(UFI_GIT_REV "dev")
endif()
add_compile_definitions(UFI_FW_VERSION="${UFI_FW_VERSION}" UFI_GIT_REV="${UFI_GIT_REV}")

# ============================================================================
# MCU Configuration
# ============================================================================

set(MCU_FAMILY STM32H7xx)
set(MCU_MODEL STM32H723xx)
set(MCU_SPEC cortex-m7)
set(MCU_FLOAT_ABI hard)
set(MCU_FPU fpv5-d16)

set(LINKER_SCRIPT ${CMAKE_SOURCE_DIR}/STM32H723ZGTX_FLASH.ld)

# ============================================================================
# Compiler Flags
# ============================================================================

set(MCU_FLAGS "-mcpu=${MCU_SPEC} -mthumb -mfpu=${MCU_FPU} -mfloat-abi=${MCU_FLOAT_ABI}")

set(CMAKE_C_FLAGS "${MCU_FLAGS} -Wall -Wextra -fdata-sections -ffunction-sections")
set(CMAKE_C_FLAGS_DEBUG "-Og -g3 -DDEBUG")
set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG")

set(CMAKE_ASM_FLAGS "${MCU_FLAGS} -x assembler-with-cpp")

set(CMAKE_EXE_LINKER_FLAGS "${MCU_FLAGS} -specs=nosys.specs -specs=nano.specs")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -T${LINKER_SCRIPT}")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--gc-sections -Wl,-Map=${PROJECT_NAME}.map -Wl,--no-warn-rwx-segments")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--print-memory-usage")

# ============================================================================
# STM32 HAL & CMSIS
# ============================================================================

# STM32CubeH7 Pfad (anpassen!)
set(STM32CUBE_PATH "$ENV{HOME}/STM32Cube/Repository/STM32Cube_FW_H7_V1.11.0"
    CACHE PATH "STM32CubeH7 SDK Path")

set(HAL_PATH ${STM32CUBE_PATH}/Drivers/STM32H7xx_HAL_Driver)
set(CMSIS_PATH ${STM32CUBE_PATH}/Drivers/CMSIS)
set(USB_PATH ${STM32CUBE_PATH}/Middlewares/ST/STM32_USB_Device_Library)
set(FATFS_PATH ${STM32CUBE_PATH}/Middlewares/Third_Party/FatFs/src)

# HAL Sources
set(HAL_SOURCES
    ${HAL_PATH}/Src/stm32h7xx_hal.c
    ${HAL_PATH}/Src/stm32h7xx_hal_cortex.c
    ${HAL_PATH}/Src/stm32h7xx_hal_rcc.c
    ${HAL_PATH}/Src/stm32h7xx_hal_rcc_ex.c
    ${HAL_PATH}/Src/stm32h7xx_hal_pwr.c
    ${HAL_PATH}/Src/stm32h7xx_hal_pwr_ex.c
    ${HAL_PATH}/Src/stm32h7xx_hal_gpio.c
    ${HAL_PATH}/Src/stm32h7xx_hal_dma.c
    ${HAL_PATH}/Src/stm32h7xx_hal_tim.c
    ${HAL_PATH}/Src/stm32h7xx_hal_tim_ex.c
    ${HAL_PATH}/Src/stm32h7xx_hal_ospi.c
    ${HAL_PATH}/Src/stm32h7xx_hal_mdma.c
    ${HAL_PATH}/Src/stm32h7xx_hal_adc.c
    ${HAL_PATH}/Src/stm32h7xx_hal_adc_ex.c
    ${HAL_PATH}/Src/stm32h7xx_hal_sd.c
    ${HAL_PATH}/Src/stm32h7xx_hal_sd_ex.c
    ${HAL_PATH}/Src/stm32h7xx_ll_sdmmc.c
    ${HAL_PATH}/Src/stm32h7xx_ll_delayblock.c
    ${HAL_PATH}/Src/stm32h7xx_hal_uart.c
    ${HAL_PATH}/Src/stm32h7xx_hal_pcd.c
    ${HAL_PATH}/Src/stm32h7xx_hal_pcd_ex.c
    ${HAL_PATH}/Src/stm32h7xx_ll_usb.c
)

# USB Device Sources
set(USB_SOURCES
    ${USB_PATH}/Core/Src/usbd_core.c
    ${USB_PATH}/Core/Src/usbd_ctlreq.c
    ${USB_PATH}/Core/Src/usbd_ioreq.c
    ${USB_PATH}/Class/CDC/Src/usbd_cdc.c
    ${USB_PATH}/Class/MSC/Src/usbd_msc.c
    ${USB_PATH}/Class/MSC/Src/usbd_msc_bot.c
    ${USB_PATH}/Class/MSC/Src/usbd_msc_data.c
    ${USB_PATH}/Class/MSC/Src/usbd_msc_scsi.c
)

# FatFs R0.12c (own disk glue in ufi_sd.c, ffconf.h in include/)
set(FATFS_SOURCES
    ${FATFS_PATH}/ff.c
)

# Startup
set(STARTUP_SOURCE ${CMSIS_PATH}/Device/ST/STM32H7xx/Source/Templates/gcc/startup_stm32h723xx.s)

# ============================================================================
# Project Sources
# ============================================================================

set(PROJECT_SOURCES
    src/ufi_main.c
    src/board_headless.c
    src/ufi_psram.c
    src/syscalls.c
    src/ufi_power.c
    src/ufi_usb.c
    src/ufi_flux.c
    src/ufi_stream.c
    src/ufi_board.c
    src/ufi_sd.c
    src/ufi_dump.c
    src/ufi_msc.c
    src/ufi_drive.c
    src/ufi_iec.c
    src/ufi_write.c
    src/ufi_debug.c
    src/ufi_clock.c
    src/system_stm32h7xx.c
    src/stm32h7xx_it.c
    src/usbd_conf.c
    src/usbd_desc.c
    src/usbd_cdc_if.c
)

# ============================================================================
# Include Directories
# ============================================================================

include_directories(
    include
    ${HAL_PATH}/Inc
    ${CMSIS_PATH}/Include
    ${CMSIS_PATH}/Device/ST/STM32H7xx/Include
    ${USB_PATH}/Core/Inc
    ${USB_PATH}/Class/CDC/Inc
    ${USB_PATH}/Class/MSC/Inc
    ${FATFS_PATH}
)

# ============================================================================
# Definitions
# ============================================================================

add_definitions(
    -DSTM32H723xx
    -DUSE_HAL_DRIVER
    -DHSE_VALUE=25000000
    -DUSE_USB_HS
)

# ============================================================================
# Build Target
# ============================================================================

add_executable(${PROJECT_NAME}.elf
    ${PROJECT_SOURCES}
    ${HAL_SOURCES}
    ${USB_SOURCES}
    ${FATFS_SOURCES}
    ${STARTUP_SOURCE}
)

# Vendor code (ST HAL / USB library) is used unmodified; silence its unused-parameter
# noise only, project sources keep the full -Wall -Wextra set.
set_source_files_properties(${HAL_SOURCES} ${USB_SOURCES}
    PROPERTIES COMPILE_OPTIONS "-Wno-unused-parameter")

# Generate .hex and .bin
add_custom_command(TARGET ${PROJECT_NAME}.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex ${PROJECT_NAME}.elf ${PROJECT_NAME}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary ${PROJECT_NAME}.elf ${PROJECT_NAME}.bin
    COMMAND ${CMAKE_SIZE} ${PROJECT_NAME}.elf
    COMMENT "Building ${PROJECT_NAME}.hex and ${PROJECT_NAME}.bin"
)

# ============================================================================
# Flash Targets
# ============================================================================

add_custom_target(flash
    COMMAND st-flash write ${PROJECT_NAME}.bin 0x08000000
    DEPENDS ${PROJECT_NAME}.elf
    COMMENT "Flashing ${PROJECT_NAME}.bin to STM32"
)

add_custom_target(flash-dfu
    COMMAND dfu-util -a 0 -s 0x08000000:leave -D ${PROJECT_NAME}.bin
    DEPENDS ${PROJECT_NAME}.elf
    COMMENT "Flashing via DFU"
)

# ============================================================================
# Debug Target
# ============================================================================

add_custom_target(debug
    COMMAND openocd -f interface/stlink.cfg -f target/stm32h7x.cfg &
    COMMAND arm-none-eabi-gdb ${PROJECT_NAME}.elf
        -ex "target remote localhost:3333"
        -ex "monitor reset halt"
        -ex "load"
    COMMENT "Starting debug session"
)
