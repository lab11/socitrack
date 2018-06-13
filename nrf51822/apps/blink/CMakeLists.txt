cmake_minimum_required (VERSION 3.8)
project (blink)

# Set compiler standard
set(CMAKE_CXX_STANDARD 11)

# Include directories to search
include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}/../../src
        ${CMAKE_CURRENT_SOURCE_DIR}/../../nrf5x-base/
        ${CMAKE_CURRENT_SOURCE_DIR}/../../nrf5x-base/advertisement
        ${CMAKE_CURRENT_SOURCE_DIR}/../../nrf5x-base/devices
        ${CMAKE_CURRENT_SOURCE_DIR}/../../nrf5x-base/dfu
        ${CMAKE_CURRENT_SOURCE_DIR}/../../nrf5x-base/lib
        ${CMAKE_CURRENT_SOURCE_DIR}/../../nrf5x-base/peripherals
        ${CMAKE_CURRENT_SOURCE_DIR}/../../nrf5x-base/sdk
        ${CMAKE_CURRENT_SOURCE_DIR}/../../nrf5x-base/sdk/nrf51_sdk_9.0.0/components/libraries/util
        ${CMAKE_CURRENT_SOURCE_DIR}/../../nrf5x-base/sdk/nrf51_sdk_9.0.0/components/libraries/timer
        ${CMAKE_CURRENT_SOURCE_DIR}/../../nrf5x-base/sdk/nrf51_sdk_9.0.0/components/softdevice/common/softdevice_handler
        ${CMAKE_CURRENT_SOURCE_DIR}/../../nrf5x-base/services)

# Add source files
set(PROJ_FILES
        main.c)

set(SOURCE_FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/../../src/boards.h
        ${CMAKE_CURRENT_SOURCE_DIR}/../../src/tripoint_interface.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../src/tripoint_interface.h
        ${CMAKE_CURRENT_SOURCE_DIR}/../../src/tritag.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../src/tritag.h
        ${CMAKE_CURRENT_SOURCE_DIR}/../../nrf5x-base/sdk/nrf51_sdk_9.0.0/components/libraries/util/nordic_common.h
        ${CMAKE_CURRENT_SOURCE_DIR}/../../nrf5x-base/sdk/nrf51_sdk_9.0.0/components/libraries/timer/app_timer.h
        ${CMAKE_CURRENT_SOURCE_DIR}/../../nrf5x-base/sdk/nrf51_sdk_9.0.0/components/softdevice/common/softdevice_handler/softdevice_handler.h
        ${CMAKE_CURRENT_SOURCE_DIR}/../../nrf5x-base/peripherals/led.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../nrf5x-base/peripherals/led.h)

# Create dummy executable for CLion
add_executable(project ${PROJ_FILES} ${SOURCE_FILES})

# Link to existing make file
# https://stackoverflow.com/questions/26918459/using-local-makefile-for-clion-instead-of-cmake
#add_custom_target(blink_s110 COMMAND make -C ${blink_SOURCE_DIR}
#        CLION_EXE_DIR=${PROJECT_BINARY_DIR})