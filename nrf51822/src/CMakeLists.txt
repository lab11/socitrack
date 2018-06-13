cmake_minimum_required (VERSION 3.8)
project (tritag)

# Set compiler standard
set(CMAKE_CXX_STANDARD 11)

# Include directories to search
#include_directories(
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../src)
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../nrf5x-base/
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../nrf5x-base/advertisement
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../nrf5x-base/devices
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../nrf5x-base/dfu
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../nrf5x-base/lib
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../nrf5x-base/peripherals
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../nrf5x-base/sdk
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../nrf5x-base/services)

# Recursive sub-directories:
# https://cmake.org/pipermail/cmake/2012-June/050674.html
# https://stackoverflow.com/questions/31004421/does-cmake-support-include-directories-recursively
# https://stackoverflow.com/questions/39216997/search-a-list-of-path-in-make-for-include-directories-recursively
set(SEARCH_DIR_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/../nrf5x-base/")

FUNCTION(R_SEARCH search_paths return_list)
    FOREACH(search_path ${search_paths})
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

R_SEARCH(${SEARCH_DIR_PATHS} recursive_list)
include_directories(${recursive_list})

# Add source files
set(SOURCE_FILES
        boards.h
        tripoint_interface.c
        tripoint_interface.h
        tritag.c
        tritag.h)

# Create dummy executable for CLion
add_executable(project ${SOURCE_FILES})

# Link to existing make file
# https://stackoverflow.com/questions/26918459/using-local-makefile-for-clion-instead-of-cmake
#add_custom_target(blink_s110 COMMAND make -C ${blink_SOURCE_DIR}
#        CLION_EXE_DIR=${PROJECT_BINARY_DIR})