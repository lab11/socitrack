cmake_minimum_required (VERSION 3.8)
project (carrier)
# The version number.
set (carrier_VERSION_MAJOR 1)
set (carrier_VERSION_MINOR 0)

# Set compiler standard
set(CMAKE_CXX_STANDARD 11)

# Add sub-directories with their own CMakeLists.txt (this superseeds constant switching in-between different files)
add_subdirectory(apps/blink)
add_subdirectory(apps/tag)
add_subdirectory(apps/tag-anchor)
add_subdirectory(apps/tag-anchor-glossy-master)
add_subdirectory(boards)

# Include directories to search by discovering sub-directories recursively
# ATTENTION: Current setup compiles for SDK Version 15.0.0 together with Softdevice s140
set(SDK_VERSION "15.0.0")
set(SOFTDEVICE_VERSION "s140")
#
# https://cmake.org/pipermail/cmake/2012-June/050674.html
# https://stackoverflow.com/questions/31004421/does-cmake-support-include-directories-recursively
# https://stackoverflow.com/questions/39216997/search-a-list-of-path-in-make-for-include-directories-recursively
set(SEARCH_DIR_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/boards")
set(SEARCH_DIR_PATHS ${SEARCH_DIR_PATHS} "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(SEARCH_DIR_PATHS ${SEARCH_DIR_PATHS} "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(SEARCH_DIR_PATHS ${SEARCH_DIR_PATHS} "${CMAKE_CURRENT_SOURCE_DIR}/nrf5x-base/drivers/device_id")
set(SEARCH_DIR_PATHS ${SEARCH_DIR_PATHS} "${CMAKE_CURRENT_SOURCE_DIR}/nrf5x-base/drivers/led")
set(SEARCH_DIR_PATHS ${SEARCH_DIR_PATHS} "${CMAKE_CURRENT_SOURCE_DIR}/nrf5x-base/lib")
set(SEARCH_DIR_PATHS ${SEARCH_DIR_PATHS} "${CMAKE_CURRENT_SOURCE_DIR}/nrf5x-base/make")
set(SEARCH_DIR_PATHS ${SEARCH_DIR_PATHS} "${CMAKE_CURRENT_SOURCE_DIR}/nrf5x-base/peripherals")
set(SEARCH_DIR_PATHS ${SEARCH_DIR_PATHS} "${CMAKE_CURRENT_SOURCE_DIR}/nrf5x-base/sdk/nrf5_sdk_${SDK_VERSION}/components/802_15_4")
set(SEARCH_DIR_PATHS ${SEARCH_DIR_PATHS} "${CMAKE_CURRENT_SOURCE_DIR}/nrf5x-base/sdk/nrf5_sdk_${SDK_VERSION}/components/ant")
set(SEARCH_DIR_PATHS ${SEARCH_DIR_PATHS} "${CMAKE_CURRENT_SOURCE_DIR}/nrf5x-base/sdk/nrf5_sdk_${SDK_VERSION}/components/ble")
set(SEARCH_DIR_PATHS ${SEARCH_DIR_PATHS} "${CMAKE_CURRENT_SOURCE_DIR}/nrf5x-base/sdk/nrf5_sdk_${SDK_VERSION}/components/boards")
set(SEARCH_DIR_PATHS ${SEARCH_DIR_PATHS} "${CMAKE_CURRENT_SOURCE_DIR}/nrf5x-base/sdk/nrf5_sdk_${SDK_VERSION}/components/drivers_ext")
set(SEARCH_DIR_PATHS ${SEARCH_DIR_PATHS} "${CMAKE_CURRENT_SOURCE_DIR}/nrf5x-base/sdk/nrf5_sdk_${SDK_VERSION}/components/drivers_nrf")
set(SEARCH_DIR_PATHS ${SEARCH_DIR_PATHS} "${CMAKE_CURRENT_SOURCE_DIR}/nrf5x-base/sdk/nrf5_sdk_${SDK_VERSION}/components/iot")
set(SEARCH_DIR_PATHS ${SEARCH_DIR_PATHS} "${CMAKE_CURRENT_SOURCE_DIR}/nrf5x-base/sdk/nrf5_sdk_${SDK_VERSION}/components/libraries")
set(SEARCH_DIR_PATHS ${SEARCH_DIR_PATHS} "${CMAKE_CURRENT_SOURCE_DIR}/nrf5x-base/sdk/nrf5_sdk_${SDK_VERSION}/components/properitary_rf")
set(SEARCH_DIR_PATHS ${SEARCH_DIR_PATHS} "${CMAKE_CURRENT_SOURCE_DIR}/nrf5x-base/sdk/nrf5_sdk_${SDK_VERSION}/components/serialization")
set(SEARCH_DIR_PATHS ${SEARCH_DIR_PATHS} "${CMAKE_CURRENT_SOURCE_DIR}/nrf5x-base/sdk/nrf5_sdk_${SDK_VERSION}/components/softdevice/common")
set(SEARCH_DIR_PATHS ${SEARCH_DIR_PATHS} "${CMAKE_CURRENT_SOURCE_DIR}/nrf5x-base/sdk/nrf5_sdk_${SDK_VERSION}/components/softdevice/mbr")
set(SEARCH_DIR_PATHS ${SEARCH_DIR_PATHS} "${CMAKE_CURRENT_SOURCE_DIR}/nrf5x-base/sdk/nrf5_sdk_${SDK_VERSION}/components/softdevice/${SOFTDEVICE_VERSION}")
set(SEARCH_DIR_PATHS ${SEARCH_DIR_PATHS} "${CMAKE_CURRENT_SOURCE_DIR}/nrf5x-base/sdk/nrf5_sdk_${SDK_VERSION}/components/toolchain")
set(SEARCH_DIR_PATHS ${SEARCH_DIR_PATHS} "${CMAKE_CURRENT_SOURCE_DIR}/nrf5x-base/sdk/nrf5_sdk_${SDK_VERSION}/config")
set(SEARCH_DIR_PATHS ${SEARCH_DIR_PATHS} "${CMAKE_CURRENT_SOURCE_DIR}/nrf5x-base/sdk/nrf5_sdk_${SDK_VERSION}/external")
set(SEARCH_DIR_PATHS ${SEARCH_DIR_PATHS} "${CMAKE_CURRENT_SOURCE_DIR}/nrf5x-base/sdk/nrf5_sdk_${SDK_VERSION}/integration")
set(SEARCH_DIR_PATHS ${SEARCH_DIR_PATHS} "${CMAKE_CURRENT_SOURCE_DIR}/nrf5x-base/sdk/nrf5_sdk_${SDK_VERSION}/modules")

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
set(SOURCE_FILES
        include/boards.h
        include/module_interface.h
        include/SEGGER_RTT.h
        include/SEGGER_RTT_Conf.h
        src/module_interface.c
        src/SEGGER_RTT.c
        src/SEGGER_RTT_printf.c)

# Create dummy executable for CLion
add_executable(project_carrier ${SOURCE_FILES})

# Link to existing make file
# https://stackoverflow.com/questions/26918459/using-local-makefile-for-clion-instead-of-cmake
#add_custom_target(carrier COMMAND make -C ${carrier_SOURCE_DIR}
#        CLION_EXE_DIR=${PROJECT_BINARY_DIR})