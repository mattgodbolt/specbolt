if (SPECBOLT_TESTS)
    add_subdirectory(test)
endif ()

add_library(z80_v4)
target_link_libraries(z80_v4 PUBLIC z80_common peripherals)
target_link_libraries(z80_v4 PUBLIC opt::pedantic opt::c++26)

if (SPECBOLT_MODULES)
    target_include_directories(z80_v4 PRIVATE include ${CMAKE_CURRENT_BINARY_DIR})
    target_sources(z80_v4
            PUBLIC
            FILE_SET modules
            TYPE CXX_MODULES
            FILES
            Disassembler.cppm
            Z80.cppm
            module.cppm
    )
else ()
    target_include_directories(z80_v4 PUBLIC include)

    target_sources(z80_v4 PRIVATE
            Disassembler.cpp
            Z80.cpp
    )
    target_sources(z80_v4 PUBLIC FILE_SET interface TYPE HEADERS FILES
            include/z80/v4/Disassembler.hpp
            include/z80/v4/Z80.hpp
    )
endif ()
