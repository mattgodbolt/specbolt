# v4 has no modules build. Its table is a header included into more than one
# module partition, so its definitions duplicate; z80/CMakeLists.txt skips this
# directory entirely when modules are on. See notes/JOURNAL.md.

if (SPECBOLT_TESTS)
    add_subdirectory(test)
endif ()

add_library(z80_v4)
target_link_libraries(z80_v4 PUBLIC z80_common peripherals)
target_link_libraries(z80_v4 PUBLIC opt::pedantic opt::c++26)

target_include_directories(z80_v4 PUBLIC include refract/include)
target_compile_definitions(z80_v4 PUBLIC SPECBOLT_CPU_TABLE="z80.cpu")

target_sources(z80_v4 PRIVATE
        Disassembler.cpp
        Z80.cpp
)
target_sources(z80_v4 PUBLIC FILE_SET interface TYPE HEADERS FILES
        Operations.hpp
        include/z80/v4/Disassembler.hpp
        include/z80/v4/Z80.hpp
        refract/include/refract/Coverage.hpp
        refract/include/refract/Disassemble.hpp
        refract/include/refract/Execute.hpp
        refract/include/refract/Lexical.hpp
        refract/include/refract/Machine.hpp
        refract/include/refract/Model.hpp
        refract/include/refract/Parse.hpp
        refract/include/refract/Parser.hpp
        refract/include/refract/Pattern.hpp
        refract/include/refract/TableError.hpp
        refract/include/refract/ToArray.hpp
        refract/include/refract/Vector.hpp
)
