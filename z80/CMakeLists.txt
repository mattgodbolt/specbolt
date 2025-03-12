if (SPECBOLT_TESTS)
    add_subdirectory(test)
endif ()

add_subdirectory(common)

add_library(z80)

target_link_libraries(z80 PUBLIC z80_common peripherals)
target_link_libraries(z80 PUBLIC opt::pedantic opt::c++26)

target_sources(z80 PRIVATE
        Disassembler.cpp
        Instruction.cpp
        Decoder.cpp
        Z80.cpp
        Generated.cpp
)

# you can also have private headers not accessible from outside, later you can use sets to change it nicely to modules
target_sources(z80 PUBLIC FILE_SET interface TYPE HEADERS FILES
        Disassembler.hpp
        Instruction.hpp
        Decoder.hpp
        Z80.hpp
        Generated.hpp
        new/Mnemonic.hpp
        new/Z80Support.hpp
)
