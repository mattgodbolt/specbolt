cmake_minimum_required(VERSION 3.30)
project(specbolt VERSION 0.1 LANGUAGES CXX)

list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CPM)
include(dependency)
include(pedantic-and-modern)
include(colors)

option(SPECBOLD_TESTS "Build and run tests" ON)
option(SPECBOLD_CONSOLE "Build specbold console" ON)
option(SPECBOLD_PREFER_SYSTEM_DEPS "Prefer using system dependencies" OFF)

# put all dependencies here (find package name first, cpm identifier second)
specbold_dependency(Readline "gh:mattgodbolt/cpp-readline#e7461f2a429c43a742197db1e6351dcef46a09ba")
#specbold_dependency(GSL "gh:microsoft/GSL@4.1.0") # GSL is not needed
find_package(SDL2 REQUIRED) # just find_package?

if (SPECBOLD_TESTS)
  enable_testing()
endif()

include_directories(${PROJECT_SOURCE_DIR})

if (SPECBOLD_CONSOLE)
  add_subdirectory(console)
endif()

add_subdirectory(peripherals)
add_subdirectory(sdl)
add_subdirectory(z80)
