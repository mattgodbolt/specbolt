if (SPECBOLT_TESTS)
    add_subdirectory(test)
endif ()

add_library(z80_v2)

target_link_libraries(z80_v2 PUBLIC z80_common peripherals)
target_link_libraries(z80_v2 PUBLIC opt::pedantic opt::c++26)

target_sources(z80_v2 PRIVATE
        Disassembler.cpp
        Z80.cpp
        Z80Impl.cpp
)

# you can also have private headers not accessible from outside, later you can use sets to change it nicely to modules
target_sources(z80_v2 PUBLIC FILE_SET interface TYPE HEADERS FILES
        Z80.hpp
        Mnemonic.hpp # todo this is private?
        Z80Impl.hpp # todo this is private?
)
