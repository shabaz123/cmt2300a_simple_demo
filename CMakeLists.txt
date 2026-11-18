# ==============================================================================
# CMT2300A Simple CW Demo
# Target: Raspberry Pi Pico (RP2040)
# ==============================================================================

cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(cmt2300a_simple C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()


# Main executable
add_executable(cmt2300a_simple
    main.cpp
    cmt2300a.c
    cmt2300a_hal.c
    radio.c
    menu.c
    freq_ctrl.c
)


# Required Pico SDK and TinyUSB libraries
target_link_libraries(cmt2300a_simple PRIVATE
    pico_stdlib
)

# Select stdio either over USB or UART
pico_enable_stdio_usb(cmt2300a_simple 1)
pico_enable_stdio_uart(cmt2300a_simple 0)


# Generate UF2, HEX, and bin outputs in addition to ELF
pico_add_extra_outputs(cmt2300a_simple)
