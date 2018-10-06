cmake_minimum_required (VERSION 3.8)
project (module)
# The version number.
set (module_VERSION_MAJOR 1)
set (module_VERSION_MINOR 0)

# Set compiler standard
set(CMAKE_CXX_STANDARD 11)

# Include directories to search
#include_directories(
#        dw1000-driver
#        firmware
#        include
#        source)

# Include directories to search by discovering sub-directories recursively
#
# https://cmake.org/pipermail/cmake/2012-June/050674.html
# https://stackoverflow.com/questions/31004421/does-cmake-support-include-directories-recursively
# https://stackoverflow.com/questions/39216997/search-a-list-of-path-in-make-for-include-directories-recursively
set(SEARCH_DIR_PATHS "dw1000-driver")
set(SEARCH_DIR_PATHS ${SEARCH_DIR_PATHS} "firmware")
set(SEARCH_DIR_PATHS ${SEARCH_DIR_PATHS} "include")
set(SEARCH_DIR_PATHS ${SEARCH_DIR_PATHS} "source")
set(SEARCH_DIR_PATHS ${SEARCH_DIR_PATHS} "stm32f0-base/sdk")

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
        firmware/dw1000.c
        firmware/dw1000.h
        firmware/firmware.h
        firmware/glossy.c
        firmware/glossy.h
        firmware/host_interface.c
        firmware/host_interface.h
        firmware/main.c
        firmware/oneway_anchor.c
        firmware/oneway_anchor.h
        firmware/oneway_common.c
        firmware/oneway_common.h
        firmware/oneway_tag.c
        firmware/oneway_tag.h
        firmware/rangetest_anchor.c
        firmware/rangetest_anchor.h
        firmware/rangetest_common.c
        firmware/rangetest_common.h
        firmware/rangetest_tag.c
        firmware/rangetest_tag.h
        firmware/polypoint_conf.h
        firmware/port.h
        firmware/stm32f0xx_conf.h
        firmware/stm32f0xx_i2c_cpal_conf.h)

set(SOURCE_FILES
        include/board.h
        include/delay.h
        include/led.h
        include/prng.h
        include/system.h
        include/timer.h
        include/tripoint.h
        include/squarepoint.h
        include/SEGGER_RTT.h
        include/SEGGER_RTT_Conf.h
        source/delay.c
        source/led.c
        source/prng.c
        source/timer.c
        source/SEGGER_RTT.c
        source/SEGGER_RTT_printf.c)

# Create dummy executable for CLion
add_executable(project_module ${PROJ_FILES} ${SOURCE_FILES})