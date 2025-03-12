if (SPECBOLT_TESTS)
    add_subdirectory(test)
endif ()

add_library(z80_v2)

target_link_libraries(z80_v2 PUBLIC z80_common peripherals)
target_link_libraries(z80_v2 PUBLIC opt::pedantic opt::c++26)

target_sources(z80_v2 PRIVATE
        Z80.cpp
        Generated.cpp
)

# you can also have private headers not accessible from outside, later you can use sets to change it nicely to modules
target_sources(z80_v2 PUBLIC FILE_SET interface TYPE HEADERS FILES
        Z80.hpp
        Generated.hpp
        Mnemonic.hpp
)
