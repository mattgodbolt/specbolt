if (SPECBOLD_TESTS)
  add_subdirectory(test)
endif()

add_library(z80)

target_link_libraries(z80 PUBLIC peripherals)
target_link_libraries(z80 PUBLIC opt::pedantic opt::c++26)

target_sources(z80 PRIVATE
  Alu.cpp
  Disassembler.cpp
  Flags.cpp
  Instruction.cpp
  Opcodes.cpp
  RegisterFile.cpp
  Z80.cpp  
)

# you can also have private headers not accessible from outside, later you can use sets to change it nicely to modules
target_sources(z80 PUBLIC FILE_SET interface TYPE HEADERS FILES
  Alu.hpp
  Disassembler.hpp
  Flags.hpp
  Instruction.hpp
  Opcodes.hpp
  RegisterFile.hpp
  Z80.hpp
)

