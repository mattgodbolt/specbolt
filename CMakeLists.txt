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
    set(CMAKE_CXX_STANDARD 26)
    add_compile_definitions(SPECBOLT_MODULES SPECBOLT_EXPORT=export)
else ()
    set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
    add_compile_definitions(SPECBOLT_EXPORT=)
endif ()

set(CMAKE_COLOR_DIAGNOSTICS ON)

project(specbolt VERSION 0.1 LANGUAGES CXX)

# The modules build targets libc++; gcc has no such switch and uses libstdc++.
# This has to live after project() as it needs the detected compiler id.
if (SPECBOLT_MODULES AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(SPECBOLT_STDLIB libc++)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
else ()
    set(SPECBOLT_STDLIB libstdc++)
endif ()

try_compile(CAN_USE_LLD SOURCE_FROM_CONTENT "test.cpp" "int main() { }" LINK_OPTIONS "-fuse-ld=lld")

if (CAN_USE_LLD)
    add_link_options("-fuse-ld=lld")
endif ()

# Compilers unpacked outside the system prefix (the compiler-explorer tarballs
# we use for bleeding-edge gcc, homebrew llvm) ship their own standard library
# but set no rpath, so binaries silently bind to whatever older copy the distro
# has on the loader's path and then die on the newer symbols. Ask the compiler
# where the standard library we're actually linking against lives, and embed
# that directory. A no-op when the toolchain is already the system one.
if (NOT SPECBOLT_WASM)
    execute_process(COMMAND ${CMAKE_CXX_COMPILER} -print-file-name=${SPECBOLT_STDLIB}.so
            OUTPUT_VARIABLE SPECBOLT_STDLIB_SO
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
    if (IS_ABSOLUTE "${SPECBOLT_STDLIB_SO}" AND EXISTS "${SPECBOLT_STDLIB_SO}")
        # -print-file-name yields a path riddled with ".." components; resolve it
        # so the rpath we bake in stays readable and stable.
        get_filename_component(SPECBOLT_STDLIB_DIR "${SPECBOLT_STDLIB_SO}" REALPATH)
        get_filename_component(SPECBOLT_STDLIB_DIR "${SPECBOLT_STDLIB_DIR}" DIRECTORY)
        add_link_options("-Wl,-rpath,${SPECBOLT_STDLIB_DIR}")
    endif ()
endif ()

list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CPM)
include(catch2)
include(dependency)
include(pedantic-and-modern)
include(reflection)

if (NOT SPECBOLT_WASM)
    find_package(SDL2 REQUIRED)
endif ()

find_program(CCACHE ccache)
if(CCACHE)
    # Depend mode keys off the compiler's depfile, which lists #embed resources;
    # direct mode only tracks includes and serves stale objects when they change.
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE
            "${CMAKE_COMMAND} -E env CCACHE_DEPEND=1 ${CCACHE}")
endif()

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_DEBUG OFF)

if (SPECBOLT_TESTS)
    enable_testing()
endif ()

specbolt_dependency(Lyra "gh:bfgroup/Lyra@1.8.0#4186a07f52392aaac31726c64c574d3496cdc13a")
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
