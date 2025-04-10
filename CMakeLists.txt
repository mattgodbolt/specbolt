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
    set(CMAKE_CXX_SCAN_FOR_MODULES ON)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
    set(CMAKE_CXX_STANDARD 26)
    add_compile_definitions(SPECBOLT_MODULES SPECBOLT_EXPORT=export)
else ()
    set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
    add_compile_definitions(SPECBOLT_EXPORT=)
endif ()

set(CMAKE_COLOR_DIAGNOSTICS ON)

project(specbolt VERSION 0.1 LANGUAGES CXX)

try_compile(CAN_USE_LLD SOURCE_FROM_CONTENT "test.cpp" "int main() { }" LINK_OPTIONS "-fuse-ld=lld")

if (CAN_USE_LLD)
    add_link_options("-fuse-ld=lld")
endif ()

list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CPM)
include(catch2)
include(dependency)
include(pedantic-and-modern)

if (NOT SPECBOLT_WASM)
    find_package(SDL2 REQUIRED)
endif ()

find_program(CCACHE ccache)
if(CCACHE)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CCACHE}")
endif()

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

if (SPECBOLT_WASM)
    add_subdirectory(web)
endif ()
