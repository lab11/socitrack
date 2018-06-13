cmake_minimum_required (VERSION 3.8)
project (nrf51822_BLE)
# The version number.
set (nrf51822_VERSION_MAJOR 1)
set (nrf51822_VERSION_MINOR 0)

# Set compiler standard
set(CMAKE_CXX_STANDARD 11)

# Add sub-directories with their own CMakeLists.txt (this superseeds constant switching in-between different files)
add_subdirectory(src)
add_subdirectory(apps/blink)
add_subdirectory(apps/tritag)
add_subdirectory(apps/tritag-anchor)
add_subdirectory(apps/tritag-anchor-glossy-master)

# Include directories to search
#include_directories(
#        src
#        nrf5x-base)

# Add source files
#set(SOURCE_FILES
#        src/boards.h
#        src/tripoint_interface.c
#        src/tripoint_interface.h
#        src/tritag.c
#        src/tritag.h
#        apps/blink/main.c
#        apps/tritag/ble_config.h
#        apps/tritag/main.c
#        apps/tritag/nrf_drv_config.h
#        apps/tritag-anchor/ble_config.h
#        apps/tritag-anchor/main.c
#        apps/tritag-anchor/nrf_drv_config.h
#        apps/tritag-anchor-glossy-master/ble_config.h
#        apps/tritag-anchor-glossy-master/main.c
#        apps/tritag-anchor-glossy-master/nrf_drv_config.h)

# Create executables
#add_executable(nrf51822_BLE ${SOURCE_FILES})