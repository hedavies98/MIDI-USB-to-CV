# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(MIDI-USB-to-CV C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

add_executable(MIDI-USB-to-CV MIDI-USB-to-CV.cpp)

pico_set_program_name(MIDI-USB-to-CV "MIDI-USB-to-CV")
pico_set_program_version(MIDI-USB-to-CV "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(MIDI-USB-to-CV 1)
pico_enable_stdio_usb(MIDI-USB-to-CV 0)

# Add the standard library to the build
target_link_libraries(MIDI-USB-to-CV
        pico_stdlib)

# Add the standard include files to the build
target_include_directories(MIDI-USB-to-CV PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)
add_subdirectory(pico-ssd1306)

# Add any user requested libraries
target_link_libraries(MIDI-USB-to-CV 
        hardware_pwm
        tinyusb_host 
        tinyusb_board 
        usb_midi_host_app_driver 
        pico_ssd1306
        hardware_i2c
        hardware_adc
        )

add_library(usb_midi_host INTERFACE)
target_sources(usb_midi_host INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/usb_midi_host/usb_midi_host.c
)
target_include_directories(usb_midi_host INTERFACE
 ${CMAKE_CURRENT_LIST_DIR}/usb_midi_host
)
target_link_libraries(usb_midi_host INTERFACE pico_stdlib tinyusb_host)

add_library(usb_midi_host_app_driver INTERFACE)
target_sources(usb_midi_host_app_driver INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/usb_midi_host/usb_midi_host_app_driver.c
)
target_include_directories(usb_midi_host_app_driver INTERFACE
 ${CMAKE_CURRENT_LIST_DIR}/usb_midi_host
)
target_link_libraries(usb_midi_host_app_driver INTERFACE usb_midi_host)

pico_add_extra_outputs(MIDI-USB-to-CV)

