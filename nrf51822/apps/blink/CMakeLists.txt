cmake_minimum_required (VERSION 3.8)
project (blink)

# Set compiler standard
set(CMAKE_CXX_STANDARD 11)

# Link to existing make file
# https://stackoverflow.com/questions/26918459/using-local-makefile-for-clion-instead-of-cmake
add_custom_target(blink COMMAND make -C ${./}
        CLION_EXE_DIR=${PROJECT_BINARY_DIR})