if (SPECBOLT_TESTS)
    add_subdirectory(test)
endif ()

add_library(z80_common)

target_link_libraries(z80_common PUBLIC peripherals opt::pedantic opt::c++26)
target_include_directories(z80_common PUBLIC include)

target_sources(z80_common PRIVATE
        Alu.cpp
        Flags.cpp
        RegisterFile.cpp
        Z80Base.cpp
)

# you can also have private headers not accessible from outside, later you can use sets to change it nicely to modules
target_sources(z80_common PUBLIC FILE_SET interface TYPE HEADERS FILES
        include/z80/common/Alu.hpp
        include/z80/common/Flags.hpp
        include/z80/common/RegisterFile.hpp
        include/z80/common/Z80Base.hpp
)
