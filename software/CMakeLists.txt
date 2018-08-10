cmake_minimum_required (VERSION 3.8)
project (surepoint)
# The version number.
set (surepoint_VERSION_MAJOR 1)
set (surepoint_VERSION_MINOR 0)

# Set compiler standard
set(CMAKE_CXX_STANDARD 11)

# Add sub-directories with their own CMakeLists.txt (this superseeds constant switching in-between different files)
add_subdirectory(carrier)
add_subdirectory(module)

# Create executables
#add_executable(project_surepoint)
