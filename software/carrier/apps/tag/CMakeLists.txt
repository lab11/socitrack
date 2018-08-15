cmake_minimum_required (VERSION 3.8)
project (tag)

# Set compiler standard
set(CMAKE_CXX_STANDARD 11)

# Add source files
set(PROJ_FILES
        ble_config.h
        nrf_drv_config.h
        main.c)

# Create dummy executable for CLion
add_executable(project_tritag ${PROJ_FILES})

# Link to existing make file
# https://stackoverflow.com/questions/26918459/using-local-makefile-for-clion-instead-of-cmake
#add_custom_target(tag_s140 COMMAND make -C ${tag_SOURCE_DIR}
#        CLION_EXE_DIR=${PROJECT_BINARY_DIR})