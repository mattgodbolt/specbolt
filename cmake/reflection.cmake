# C++26 static reflection (P2996). Probed by compiling, as the preprocessor
# can't tell us: gcc ships <meta> whether or not reflection is enabled, and only
# defines __cpp_impl_reflection once -freflection is passed.

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
    # Unadorned first, for compilers that need no flag for it.
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

if (SPECBOLT_HAS_REFLECTION)
    # Two more flags v4 needs from compilers that have them. Probed rather than
    # keyed off the compiler id, so whichever compiler accepts one gets it.
    include(CheckCXXCompilerFlag)

    # Parsing the description takes more constexpr steps than clang's default
    # allows, and the failure reads "possible infinite loop?", which it is not.
    check_cxx_compiler_flag("-fconstexpr-steps=100000000" SPECBOLT_HAS_CONSTEXPR_STEPS)
    if (SPECBOLT_HAS_CONSTEXPR_STEPS)
        list(APPEND SPECBOLT_REFLECTION_FLAGS "-fconstexpr-steps=100000000")
    endif ()

    # The description is `#embed`ded, which clang counts as a C23 extension,
    # and this project builds with -Werror. The positive form is probed because
    # gcc accepts any -Wno- flag, known or not.
    check_cxx_compiler_flag("-Wc23-extensions" SPECBOLT_HAS_C23_EXTENSIONS_FLAG)
    if (SPECBOLT_HAS_C23_EXTENSIONS_FLAG)
        list(APPEND SPECBOLT_REFLECTION_FLAGS "-Wno-c23-extensions")
    endif ()
endif ()

if (SPECBOLT_REFLECTION STREQUAL "ON" AND NOT SPECBOLT_HAS_REFLECTION)
    message(FATAL_ERROR
            "SPECBOLT_REFLECTION=ON but ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} "
            "cannot compile a C++26 reflection probe. gcc 16+ and the P2996 clang forks are the "
            "toolchains that can; see README.md for grabbing one. Use SPECBOLT_REFLECTION=AUTO to "
            "build without the reflection-based code.")
endif ()

if (SPECBOLT_HAS_REFLECTION)
    # On opt::c++26 so it reaches every specbolt target uniformly (gcc can't
    # import a module built without it) and is always paired with -std=c++26.
    if (SPECBOLT_REFLECTION_FLAGS)
        target_compile_options(opt_c++26 INTERFACE ${SPECBOLT_REFLECTION_FLAGS})
    endif ()
    target_compile_definitions(opt_c++26 INTERFACE SPECBOLT_REFLECTION)
    message(STATUS "C++26 reflection: available (flags: '${SPECBOLT_REFLECTION_FLAGS}')")
else ()
    message(STATUS "C++26 reflection: unavailable, reflection-based code will be skipped")
endif ()
