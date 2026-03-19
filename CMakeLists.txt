# MimiClaw AMOLED Project CMakeLists.txt
# ESP32-S3 with RM67162 AMOLED Display

cmake_minimum_required(VERSION 3.5)

# Include project configuration
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Project name
project(MimiClaw_AMOLED)

# Set C/C++ standards
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Add compile definitions for AMOLED display
add_compile_definitions(
    AMOLED_DEFAULT_BRIGHTNESS=175
)

# Set component directories
set(EXTRA_COMPONENT_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/components
)

# Print project info
message(STATUS "MimiClaw AMOLED Project")
message(STATUS "Target: ESP32-S3 with RM67162 AMOLED")
message(STATUS "Resolution: 240x536")
message(STATUS "Touch: CST816S")
