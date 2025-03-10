cmake_minimum_required(VERSION 3.14...3.30)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "0e5b6991-d74f-4b3d-a41c-cf096e0b2508")

set(CMAKE_CXX_STANDARD_REQUIRED OFF)
set(CMAKE_CXX_MODULE_STD 1)

set(CMAKE_COLOR_DIAGNOSTICS ON)

project(specbolt VERSION 0.1 LANGUAGES CXX)

list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CPM)
include(dependency)
include(pedantic-and-modern)

option(SPECBOLT_TESTS "Build and run tests" ON)
option(SPECBOLT_CONSOLE "Build specbolt console" ON)
option(SPECBOLT_PREFER_SYSTEM_DEPS "Prefer using system dependencies" OFF)

find_package(SDL2 REQUIRED)

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_DEBUG OFF)

if (SPECBOLT_TESTS)
  enable_testing()
endif()

include_directories(${PROJECT_SOURCE_DIR})

if (SPECBOLT_CONSOLE)
  add_subdirectory(console)
endif()

add_subdirectory(peripherals)
add_subdirectory(sdl)
add_subdirectory(spectrum)
add_subdirectory(z80)
