if (SPECBOLT_TESTS)
    add_subdirectory(test)
endif ()

add_library(z80_v2)

target_link_libraries(z80_v2 PUBLIC z80_common peripherals)
target_link_libraries(z80_v2 PUBLIC opt::pedantic opt::c++26)
target_sources(z80_v2
        PUBLIC
        FILE_SET modules
        TYPE CXX_MODULES
        FILES
        Disassembler.cppm
        Mnemonic.cppm
        Z80.cppm
        module.cppm
)
