cmake_minimum_required(VERSION 3.30)
project(specbolt)
set(CMAKE_CXX_STANDARD 26)
enable_testing()

# add dependencies before we fiddle with compilation options
include(cmake/CPM.cmake)
CPMAddPackage("gh:catchorg/Catch2@3.8.0")
CPMAddPackage("gh:microsoft/GSL@4.1.0")
find_package(SDL2 REQUIRED)
add_compile_options(-Wall -Wextra -Wpedantic -Werror -Wconversion)

add_subdirectory(test)

find_package(X11 REQUIRED)
add_executable(specbolt main.cpp)
target_link_libraries(specbolt SDL2::SDL2)
