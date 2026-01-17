# GB/GBA USB Dumper - RP2040 Firmware
cmake_minimum_required(VERSION 3.13)

# Pico SDK importieren
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(gb_gba_dumper C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Pico SDK initialisieren
pico_sdk_init()

# Executable
add_executable(gb_gba_dumper
    src/main.c
)

# USB Device
target_link_libraries(gb_gba_dumper
    pico_stdlib
    hardware_gpio
    hardware_pio
    hardware_dma
    tinyusb_device
    tinyusb_board
)

# USB stdio ausschalten (wir nutzen CDC manuell)
pico_enable_stdio_usb(gb_gba_dumper 0)
pico_enable_stdio_uart(gb_gba_dumper 0)

# UF2 und BIN erzeugen
pico_add_extra_outputs(gb_gba_dumper)

# USB Descriptors
target_compile_definitions(gb_gba_dumper PRIVATE
    CFG_TUSB_CONFIG_FILE="tusb_config.h"
)
