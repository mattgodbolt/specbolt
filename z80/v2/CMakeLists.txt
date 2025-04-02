if (SPECBOLT_TESTS)
    add_subdirectory(test)
endif ()

add_library(z80_v2)

target_link_libraries(z80_v2 PUBLIC z80_common peripherals)
target_link_libraries(z80_v2 PUBLIC opt::pedantic opt::c++26)


if (SPECBOLT_MODULES)
    target_include_directories(z80_v2 PRIVATE include)
    target_sources(z80_v2
            PUBLIC
            FILE_SET modules
            TYPE CXX_MODULES
            FILES
            Disassembler.cppm
            Mnemonic.cppm
            Z80.cppm
            Z80Impl.cppm
            module.cppm
    )
else ()
    target_include_directories(z80_v2 PUBLIC include)

    target_sources(z80_v2 PRIVATE
            Disassembler.cpp
            Z80.cpp
            Z80Impl.cpp
            Mnemonic.hpp
            Z80Impl.hpp
    )

    target_sources(z80_v2 PUBLIC FILE_SET interface TYPE HEADERS FILES
            include/z80/v2/Disassembler.hpp
            include/z80/v2/Z80.hpp
    )
endif ()
