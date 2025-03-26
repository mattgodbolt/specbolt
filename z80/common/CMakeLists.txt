if (SPECBOLT_TESTS)
    add_subdirectory(test)
endif ()

add_library(z80_common)
target_link_libraries(z80_common PUBLIC peripherals opt::pedantic opt::c++26)
target_sources(z80_common
        PUBLIC
        FILE_SET modules
        TYPE CXX_MODULES
        FILES
        module.cppm
        Alu.cppm
        Flags.cppm
        RegisterFile.cppm
        Scheduler.cppm
        Z80Base.cppm
)
