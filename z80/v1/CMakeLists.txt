if (SPECBOLT_TESTS)
    add_subdirectory(test)
endif ()

add_library(z80_v1)

target_link_libraries(z80_v1 PUBLIC z80_common peripherals)
target_link_libraries(z80_v1 PUBLIC opt::pedantic opt::c++26)

target_sources(z80_v1 PRIVATE
        Disassembler.cpp
        Instruction.cpp
        Decoder.cpp
        Z80.cpp
)

# you can also have private headers not accessible from outside, later you can use sets to change it nicely to modules
target_sources(z80_v1 PUBLIC FILE_SET interface TYPE HEADERS FILES
        Disassembler.hpp
        Instruction.hpp
        Decoder.hpp
        Z80.hpp
)
