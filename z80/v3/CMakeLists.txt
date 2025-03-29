if (SPECBOLT_TESTS)
    add_subdirectory(test)
endif ()

add_library(z80_v3)
target_link_libraries(z80_v3 PUBLIC z80_common peripherals)
target_link_libraries(z80_v3 PUBLIC opt::pedantic opt::c++26)

add_executable(z80_v3_make)
target_link_libraries(z80_v3_make PUBLIC opt::pedantic opt::c++26)
target_sources(z80_v3_make PRIVATE
        MakeZ80.cpp
)
add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/Z80Generated.cpp
        COMMAND z80_v3_make ${CMAKE_CURRENT_BINARY_DIR}/Z80Generated.cpp ${CMAKE_CURRENT_SOURCE_DIR}
        DEPENDS z80_v3_make)

if (SPECBOLT_MODULES)
    target_include_directories(z80_v3 PRIVATE include ${CMAKE_CURRENT_BINARY_DIR})
    target_sources(z80_v3
            PUBLIC
            FILE_SET modules
            TYPE CXX_MODULES
            FILES
            Disassembler.cppm
            Z80.cppm
            module.cppm
    )
    set_property(SOURCE Z80.cppm APPEND PROPERTY OBJECT_DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/Z80Generated.cpp)
else ()
    target_include_directories(z80_v3 PUBLIC include)

    target_sources(z80_v3 PRIVATE
            Disassembler.cpp
            Z80.cpp
            ${CMAKE_CURRENT_BINARY_DIR}/Z80Generated.cpp
    )
    target_sources(z80_v3 PUBLIC FILE_SET interface TYPE HEADERS FILES
            include/z80/v3/Disassembler.hpp
            include/z80/v3/Z80.hpp
    )
endif ()
