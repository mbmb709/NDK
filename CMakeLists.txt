cmake_minimum_required(VERSION 3.21)
project(ImageSchematicEditor VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

if(NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Set GEODE_SDK to your Geode v5.11.2 SDK folder")
endif()

add_library(${PROJECT_NAME} SHARED src/main.cpp src/Pixelizer.cpp src/Schematic.cpp)
target_include_directories(${PROJECT_NAME} PRIVATE src)
add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)
setup_geode_mod(${PROJECT_NAME})
