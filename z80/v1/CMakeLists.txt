if (SPECBOLT_TESTS)
    add_subdirectory(test)
endif ()

add_library(z80_v1)

target_link_libraries(z80_v1 PUBLIC z80_common peripherals)
target_link_libraries(z80_v1 PUBLIC opt::pedantic opt::c++26)
target_include_directories(z80_v1 PUBLIC include)



if (SPECBOLT_MODULES)
    target_include_directories(z80_v1 PRIVATE include)
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
    )
else ()
  target_sources(z80_v1 PRIVATE
          Disassembler.cpp
          Instruction.cpp
          Decoder.cpp
          Decoder.hpp
          Z80.cpp
  )
  # you can also have private headers not accessible from outside, later you can use sets to change it nicely to modules
  target_sources(z80_v1 PUBLIC FILE_SET interface TYPE HEADERS FILES
          include/z80/v1/Disassembler.hpp
          include/z80/v1/Instruction.hpp
          include/z80/v1/Z80.hpp
  )
endif ()
