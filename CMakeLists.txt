cmake_minimum_required(VERSION 3.30)
project(specbolt VERSION 0.1 LANGUAGES CXX)

list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CPM)
include(dependency)
include(pedantic-and-modern)
include(colors)

option(SPECBOLT_TESTS "Build and run tests" ON)
option(SPECBOLT_CONSOLE "Build specbolt console" ON)
option(SPECBOLT_PREFER_SYSTEM_DEPS "Prefer using system dependencies" OFF)

find_package(SDL2 REQUIRED)

if (SPECBOLT_TESTS)
  enable_testing()
endif()

include_directories(${PROJECT_SOURCE_DIR})

if (SPECBOLT_CONSOLE)
  add_subdirectory(console)
endif()

add_subdirectory(peripherals)
add_subdirectory(sdl)
add_subdirectory(z80)
