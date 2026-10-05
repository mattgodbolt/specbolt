# v4 has no modules build. Its compiled description, Target.hpp, is a header a
# modules build would include into more than one partition, duplicating its
# definitions; z80/CMakeLists.txt skips this
# directory entirely when modules are on. See notes/JOURNAL.md.

add_subdirectory(refract)

if (SPECBOLT_TESTS)
    add_subdirectory(test)
endif ()

add_library(z80_v4)
target_link_libraries(z80_v4 PUBLIC refract z80_common peripherals)
target_link_libraries(z80_v4 PUBLIC opt::pedantic opt::c++26)
# Whatever links v4 is told so: that is how code shared with the other
# implementations, which builds without it too, knows to include it.
target_compile_definitions(z80_v4 INTERFACE SPECBOLT_HAS_V4)

target_include_directories(z80_v4 PUBLIC include)

target_sources(z80_v4 PRIVATE
        Disassembler.cpp
        Z80.cpp
)
target_sources(z80_v4 PUBLIC FILE_SET interface TYPE HEADERS FILES
        Operations.hpp
        Target.hpp
        include/z80/v4/Disassembler.hpp
        include/z80/v4/Z80.hpp
)
