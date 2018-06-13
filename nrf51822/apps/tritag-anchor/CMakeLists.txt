cmake_minimum_required (VERSION 3.8)
project (tritag-anchor)

# Set compiler standard
set(CMAKE_CXX_STANDARD 11)

# Include directories to search by discovering sub-directories recursively
# ATTENTION: Current setup compiles for SDK Version 9.0.0 together with Softdevice s110
set(SDK_VERSION "9.0.0")
set(SOFTDEVICE_VERSION "s110")
#
# https://cmake.org/pipermail/cmake/2012-June/050674.html
# https://stackoverflow.com/questions/31004421/does-cmake-support-include-directories-recursively
# https://stackoverflow.com/questions/39216997/search-a-list-of-path-in-make-for-include-directories-recursively
set(SEARCH_DIR_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
set(SEARCH_DIR_PATHS ${SEARCH_DIR_PATHS} "${CMAKE_CURRENT_SOURCE_DIR}/../../nrf5x-base/advertisement")
set(SEARCH_DIR_PATHS ${SEARCH_DIR_PATHS} "${CMAKE_CURRENT_SOURCE_DIR}/../../nrf5x-base/devices")
set(SEARCH_DIR_PATHS ${SEARCH_DIR_PATHS} "${CMAKE_CURRENT_SOURCE_DIR}/../../nrf5x-base/dfu")
set(SEARCH_DIR_PATHS ${SEARCH_DIR_PATHS} "${CMAKE_CURRENT_SOURCE_DIR}/../../nrf5x-base/lib")
set(SEARCH_DIR_PATHS ${SEARCH_DIR_PATHS} "${CMAKE_CURRENT_SOURCE_DIR}/../../nrf5x-base/make")
set(SEARCH_DIR_PATHS ${SEARCH_DIR_PATHS} "${CMAKE_CURRENT_SOURCE_DIR}/../../nrf5x-base/peripherals")
set(SEARCH_DIR_PATHS ${SEARCH_DIR_PATHS} "${CMAKE_CURRENT_SOURCE_DIR}/../../nrf5x-base/sdk/nrf51_sdk_${SDK_VERSION}/components/ant")
set(SEARCH_DIR_PATHS ${SEARCH_DIR_PATHS} "${CMAKE_CURRENT_SOURCE_DIR}/../../nrf5x-base/sdk/nrf51_sdk_${SDK_VERSION}/components/ble")
set(SEARCH_DIR_PATHS ${SEARCH_DIR_PATHS} "${CMAKE_CURRENT_SOURCE_DIR}/../../nrf5x-base/sdk/nrf51_sdk_${SDK_VERSION}/components/device")
set(SEARCH_DIR_PATHS ${SEARCH_DIR_PATHS} "${CMAKE_CURRENT_SOURCE_DIR}/../../nrf5x-base/sdk/nrf51_sdk_${SDK_VERSION}/components/drivers_ext")
set(SEARCH_DIR_PATHS ${SEARCH_DIR_PATHS} "${CMAKE_CURRENT_SOURCE_DIR}/../../nrf5x-base/sdk/nrf51_sdk_${SDK_VERSION}/components/drivers_nrf")
set(SEARCH_DIR_PATHS ${SEARCH_DIR_PATHS} "${CMAKE_CURRENT_SOURCE_DIR}/../../nrf5x-base/sdk/nrf51_sdk_${SDK_VERSION}/components/libraries")
set(SEARCH_DIR_PATHS ${SEARCH_DIR_PATHS} "${CMAKE_CURRENT_SOURCE_DIR}/../../nrf5x-base/sdk/nrf51_sdk_${SDK_VERSION}/components/properitary_rf")
set(SEARCH_DIR_PATHS ${SEARCH_DIR_PATHS} "${CMAKE_CURRENT_SOURCE_DIR}/../../nrf5x-base/sdk/nrf51_sdk_${SDK_VERSION}/components/serialization")
set(SEARCH_DIR_PATHS ${SEARCH_DIR_PATHS} "${CMAKE_CURRENT_SOURCE_DIR}/../../nrf5x-base/sdk/nrf51_sdk_${SDK_VERSION}/components/softdevice/common")
set(SEARCH_DIR_PATHS ${SEARCH_DIR_PATHS} "${CMAKE_CURRENT_SOURCE_DIR}/../../nrf5x-base/sdk/nrf51_sdk_${SDK_VERSION}/components/softdevice/${SOFTDEVICE_VERSION}")
set(SEARCH_DIR_PATHS ${SEARCH_DIR_PATHS} "${CMAKE_CURRENT_SOURCE_DIR}/../../nrf5x-base/sdk/nrf51_sdk_${SDK_VERSION}/components/toolchain")
set(SEARCH_DIR_PATHS ${SEARCH_DIR_PATHS} "${CMAKE_CURRENT_SOURCE_DIR}/../../nrf5x-base/services")
set(SEARCH_DIR_PATHS ${SEARCH_DIR_PATHS} "${CMAKE_CURRENT_SOURCE_DIR}/../../nrf5x-base/startup")

FUNCTION(R_SEARCH search_paths return_list)
    #message("Received list: ${search_paths}")
    FOREACH(search_path ${search_paths})
        #message("Searching through directory: ${search_path}")
        R_SEARCH_INNER(${search_path} inner_return)
        SET(all_list ${all_list} ${inner_return})
    ENDFOREACH()
    SET(${return_list} ${all_list} PARENT_SCOPE)
ENDFUNCTION(R_SEARCH)

FUNCTION(R_SEARCH_INNER search_path return_list)
    FILE(GLOB_RECURSE new_list ${search_path}/*.h)
    SET(dir_list "")
    FOREACH(file_path ${new_list})
        GET_FILENAME_COMPONENT(dir_path ${file_path} PATH)
        SET(dir_list ${dir_list} ${dir_path})
    ENDFOREACH()
    LIST(REMOVE_DUPLICATES dir_list)
    SET(${return_list} ${dir_list} PARENT_SCOPE)
ENDFUNCTION(R_SEARCH_INNER)

R_SEARCH("${SEARCH_DIR_PATHS}" recursive_list)
#message("Found headers in: ${recursive_list}")

# Include header directories
include_directories(${recursive_list})


# Add source files
set(PROJ_FILES
        ble_config.h
        main.c
        nrf_drv_config.h)

set(SOURCE_FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/../../src/boards.h
        ${CMAKE_CURRENT_SOURCE_DIR}/../../src/tripoint_interface.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../src/tripoint_interface.h
        ${CMAKE_CURRENT_SOURCE_DIR}/../../src/tritag.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../src/tritag.h)

# Create dummy executable for CLion
add_executable(project_tritag_anchor ${PROJ_FILES} ${SOURCE_FILES})

# Link to existing make file
# https://stackoverflow.com/questions/26918459/using-local-makefile-for-clion-instead-of-cmake
#add_custom_target(tritag-anchor_s110 COMMAND make -C ${tritag-anchor_SOURCE_DIR}
#        CLION_EXE_DIR=${PROJECT_BINARY_DIR})