if (SPECBOLT_TESTS)
    add_subdirectory(test)
endif ()

add_library(z80_v1)

target_link_libraries(z80_v1 PUBLIC z80_common peripherals)
target_link_libraries(z80_v1 PUBLIC opt::pedantic opt::c++26)

target_sources(z80_v1
        PUBLIC
        FILE_SET modules
        TYPE CXX_MODULES
        FILES
        Decoder.cppm
        Disassembler.cppm
        Instruction.cppm
        InstructionImpl.cppm
        Z80.cppm
        module.cppm
)
