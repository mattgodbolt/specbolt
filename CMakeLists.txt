cmake_minimum_required(VERSION 3.30)

option(SPECBOLT_TESTS "Build and run tests" ON)
option(SPECBOLT_CONSOLE "Build specbolt console" ON)
option(SPECBOLT_PREFER_SYSTEM_DEPS "Prefer using system dependencies" OFF)
option(SPECBOLT_MODULES "Use C++ Modules" ON)
option(SPECBOLT_WASM "Compile for WebAssembly" OFF)
set(SPECBOLT_WASI_SYSROOT "" CACHE STRING "Wasi root")

if (SPECBOLT_WASM)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -target wasm32-wasi --sysroot=${SPECBOLT_WASI_SYSROOT}")
    set(SPECBOLT_TESTS OFF)
    set(SPECBOLT_CONSOLE OFF)
endif ()

if (SPECBOLT_MODULES)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
    set(CMAKE_CXX_STANDARD 26)
    set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "0e5b6991-d74f-4b3d-a41c-cf096e0b2508")
    set(CMAKE_CXX_MODULE_STD 1)
    add_compile_definitions(SPECBOLT_MODULES SPECBOLT_EXPORT=export)
else ()
    add_compile_definitions(SPECBOLT_EXPORT=)
endif ()

set(CMAKE_COLOR_DIAGNOSTICS ON)

add_link_options("-fuse-ld=lld")

project(specbolt VERSION 0.1 LANGUAGES CXX)

list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CPM)
include(catch2)
include(dependency)
include(pedantic-and-modern)

if (NOT SPECBOLT_WASM)
    find_package(SDL2 REQUIRED)
endif ()

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_DEBUG OFF)

if (SPECBOLT_TESTS)
    enable_testing()
endif ()

specbolt_dependency(Lyra "gh:bfgroup/Lyra@1.6.1#ee3c076fa6b9d64c9d249a21f5b9b5a8dae92cd8")
include_directories(${PROJECT_SOURCE_DIR})

if (SPECBOLT_CONSOLE)
    add_subdirectory(console)
endif ()

add_subdirectory(peripherals)
if (NOT SPECBOLT_WASM)
    add_subdirectory(sdl)
endif ()
add_subdirectory(spectrum)
add_subdirectory(z80)
