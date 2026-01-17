# MIG Dumper - STM32G0B1 Firmware
cmake_minimum_required(VERSION 3.20)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# Toolchain
set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
set(CMAKE_OBJCOPY arm-none-eabi-objcopy)
set(CMAKE_SIZE arm-none-eabi-size)

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

project(mig_dumper C ASM)

# MCU
set(MCU_FLAGS "-mcpu=cortex-m0plus -mthumb")

set(CMAKE_C_FLAGS "${MCU_FLAGS} -Wall -fdata-sections -ffunction-sections")
set(CMAKE_C_FLAGS_DEBUG "-Og -g3")
set(CMAKE_C_FLAGS_RELEASE "-O2")

set(CMAKE_EXE_LINKER_FLAGS "${MCU_FLAGS} -specs=nosys.specs -specs=nano.specs")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -T${CMAKE_SOURCE_DIR}/STM32G0B1CBTx_FLASH.ld")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--gc-sections")

# STM32CubeG0 Pfad
set(STM32CUBE_PATH "$ENV{HOME}/STM32Cube/Repository/STM32Cube_FW_G0_V1.6.0"
    CACHE PATH "STM32CubeG0 SDK Path")

set(HAL_PATH ${STM32CUBE_PATH}/Drivers/STM32G0xx_HAL_Driver)
set(CMSIS_PATH ${STM32CUBE_PATH}/Drivers/CMSIS)
set(USB_PATH ${STM32CUBE_PATH}/Middlewares/ST/STM32_USB_Device_Library)

# HAL Sources
set(HAL_SOURCES
    ${HAL_PATH}/Src/stm32g0xx_hal.c
    ${HAL_PATH}/Src/stm32g0xx_hal_cortex.c
    ${HAL_PATH}/Src/stm32g0xx_hal_rcc.c
    ${HAL_PATH}/Src/stm32g0xx_hal_gpio.c
    ${HAL_PATH}/Src/stm32g0xx_hal_pcd.c
    ${HAL_PATH}/Src/stm32g0xx_hal_pcd_ex.c
    ${HAL_PATH}/Src/stm32g0xx_ll_usb.c
)

# USB Sources
set(USB_SOURCES
    ${USB_PATH}/Core/Src/usbd_core.c
    ${USB_PATH}/Core/Src/usbd_ctlreq.c
    ${USB_PATH}/Core/Src/usbd_ioreq.c
    ${USB_PATH}/Class/CDC/Src/usbd_cdc.c
)

# Project Sources
set(PROJECT_SOURCES
    src/main.c
)

# Include Directories
include_directories(
    include
    ${HAL_PATH}/Inc
    ${CMSIS_PATH}/Include
    ${CMSIS_PATH}/Device/ST/STM32G0xx/Include
    ${USB_PATH}/Core/Inc
    ${USB_PATH}/Class/CDC/Inc
)

# Definitions
add_definitions(
    -DSTM32G0B1xx
    -DUSE_HAL_DRIVER
)

# Build
add_executable(${PROJECT_NAME}.elf
    ${PROJECT_SOURCES}
    ${HAL_SOURCES}
    ${USB_SOURCES}
)

# Generate outputs
add_custom_command(TARGET ${PROJECT_NAME}.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex ${PROJECT_NAME}.elf ${PROJECT_NAME}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary ${PROJECT_NAME}.elf ${PROJECT_NAME}.bin
    COMMAND ${CMAKE_SIZE} ${PROJECT_NAME}.elf
)
