if (SPECBOLD_TESTS)
  add_subdirectory(test)
endif()

add_library(z80
        Alu.cpp
        Alu.hpp
        Disassembler.cpp
        Disassembler.hpp
        Flags.cpp
        Flags.hpp
        Instruction.cpp
        Instruction.hpp
        Opcodes.cpp
        Opcodes.hpp
        RegisterFile.cpp
        RegisterFile.hpp
        Z80.cpp
        Z80.hpp
)
target_link_libraries(z80 PUBLIC peripherals)
modern_and_pedantic_settings(z80 PUBLIC)
