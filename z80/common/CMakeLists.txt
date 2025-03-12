if (SPECBOLT_TESTS)
    add_subdirectory(test)
endif ()

#add_subdirectory()
add_library(z80_common)

target_link_libraries(z80_common PUBLIC opt::pedantic opt::c++26)

target_sources(z80_common PRIVATE
        Alu.cpp
        Flags.cpp
        RegisterFile.cpp
        Z80Base.cpp
)

# you can also have private headers not accessible from outside, later you can use sets to change it nicely to modules
target_sources(z80_common PUBLIC FILE_SET interface TYPE HEADERS FILES
        Alu.hpp
        Flags.hpp
        RegisterFile.hpp
        Z80Base.hpp
)
