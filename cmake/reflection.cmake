# C++26 static reflection (P2996) detection.
#
# Link against opt::reflection to get the flags; guard code with
# SPECBOLT_REFLECTION, which that target defines when support is present.
#
# Sniffing this from the preprocessor doesn't work: gcc ships <meta> on the
# include path whether or not reflection is enabled, and only defines
# __cpp_impl_reflection once -freflection is passed — so __has_include(<meta>)
# happily lies. We compile a real reflection expression instead.

set(SPECBOLT_REFLECTION "AUTO" CACHE STRING "C++26 reflection: AUTO (use if available), ON (require), OFF")
set_property(CACHE SPECBOLT_REFLECTION PROPERTY STRINGS AUTO ON OFF)

set(SPECBOLT_REFLECTION_PROBE_SOURCE [[
#include <meta>
struct Probe { int a; bool b; };
static_assert(std::meta::nonstatic_data_members_of(
        ^^Probe, std::meta::access_context::current()).size() == 2);
int main() { }
]])

set(SPECBOLT_HAS_REFLECTION OFF)
set(SPECBOLT_REFLECTION_FLAGS "")

if (NOT SPECBOLT_REFLECTION STREQUAL "OFF")
    include(CheckCXXSourceCompiles)
    include(CMakePushCheckState)

    cmake_push_check_state()
    # Try unadorned first: reflection is opt-in on gcc 16 but is expected to
    # become the default as implementations mature, and we'd rather not pass a
    # gcc-specific flag to a compiler that doesn't need it.
    set(CMAKE_REQUIRED_FLAGS "-std=c++26")
    check_cxx_source_compiles("${SPECBOLT_REFLECTION_PROBE_SOURCE}" SPECBOLT_REFLECTION_BY_DEFAULT)
    if (SPECBOLT_REFLECTION_BY_DEFAULT)
        set(SPECBOLT_HAS_REFLECTION ON)
    else ()
        set(CMAKE_REQUIRED_FLAGS "-std=c++26 -freflection")
        check_cxx_source_compiles("${SPECBOLT_REFLECTION_PROBE_SOURCE}" SPECBOLT_REFLECTION_NEEDS_FLAG)
        if (SPECBOLT_REFLECTION_NEEDS_FLAG)
            set(SPECBOLT_HAS_REFLECTION ON)
            set(SPECBOLT_REFLECTION_FLAGS "-freflection")
        endif ()
    endif ()
    cmake_pop_check_state()
endif ()

if (SPECBOLT_REFLECTION STREQUAL "ON" AND NOT SPECBOLT_HAS_REFLECTION)
    message(FATAL_ERROR
            "SPECBOLT_REFLECTION=ON but ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} "
            "cannot compile a C++26 reflection probe. gcc 16+ is currently the only toolchain "
            "that can; see README.md for grabbing one. Use SPECBOLT_REFLECTION=AUTO to build "
            "without the reflection-based code.")
endif ()

add_library(opt_reflection INTERFACE)
add_library(opt::reflection ALIAS opt_reflection)

if (SPECBOLT_HAS_REFLECTION)
    target_compile_options(opt_reflection INTERFACE ${SPECBOLT_REFLECTION_FLAGS})
    target_compile_definitions(opt_reflection INTERFACE SPECBOLT_REFLECTION)
    message(STATUS "C++26 reflection: available (flags: '${SPECBOLT_REFLECTION_FLAGS}')")
else ()
    message(STATUS "C++26 reflection: unavailable, reflection-based code will be skipped")
endif ()
