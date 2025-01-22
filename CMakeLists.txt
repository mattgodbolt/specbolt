cmake_minimum_required(VERSION 3.30)
project(specbolt)
set(CMAKE_CXX_STANDARD 26)
enable_testing()

list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# add dependencies before we fiddle with compilation options
include(CPM)
CPMAddPackage("gh:catchorg/Catch2@3.8.0")
CPMAddPackage("gh:microsoft/GSL@4.1.0")
find_package(SDL2 REQUIRED)

# add these after dependencies
include(pedantic)
include(colors)

add_subdirectory(test)

add_library(specbolt_lib
        Z80.cpp
        Z80.hpp
        RegisterFile.cpp
        RegisterFile.hpp
        Disassembler.cpp
        Disassembler.hpp
        Memory.cpp
        Memory.hpp)
add_executable(specbolt main.cpp)
target_link_libraries(specbolt specbolt_lib SDL2::SDL2)
