# Experimental v4 Z80: reflection-driven instruction dispatch.
#
# Requires Barry Revzin's clang fork with `-freflection-latest` and the
# token-injection extensions. Probe for the flag before committing.

include(CheckCXXCompilerFlag)
check_cxx_compiler_flag(-freflection-latest SPECBOLT_HAVE_REFLECTION_LATEST)
if (NOT SPECBOLT_HAVE_REFLECTION_LATEST)
    message(FATAL_ERROR
        "SPECBOLT_V4=ON requires a compiler that accepts -freflection-latest "
        "(Barry Revzin's clang fork). The current compiler does not. "
        "Disable with -DSPECBOLT_V4=OFF or point CC/CXX at the fork.")
endif ()

if (SPECBOLT_TESTS)
    add_subdirectory(test)
endif ()

add_library(z80_v4)
target_link_libraries(z80_v4 PUBLIC z80_common peripherals)
target_link_libraries(z80_v4 PUBLIC opt::pedantic opt::c++26)
target_compile_options(z80_v4 PUBLIC -freflection-latest)
# Barry's fork defaults to libc++; make sure we use it explicitly too.
target_compile_options(z80_v4 PUBLIC -stdlib=libc++)
target_link_options(z80_v4 PUBLIC -stdlib=libc++)

target_include_directories(z80_v4 PUBLIC include)
target_sources(z80_v4 PRIVATE
        Z80.cpp
)
target_sources(z80_v4 PUBLIC FILE_SET interface TYPE HEADERS FILES
        include/z80/v4/Z80.hpp
        include/z80/v4/Instructions.hpp
)
