if (SPECBOLT_TESTS)
    add_subdirectory(test)
endif ()

add_library(z80_module)
target_link_libraries(z80_module PUBLIC peripherals_module opt::pedantic opt::c++26)
target_sources(z80_module PUBLIC FILE_SET module TYPE CXX_MODULES FILES
  All.cppm
  Alu.cppm
  Flags.cppm
  Instruction.cppm
  InstructionImpl.cppm
  CPU.cppm
  Decoder.cppm
  Disassembler.cppm
  new/Mnemonic.cppm
  new/Z80Support.cppm
  Generated.cppm
  RegisterFile.cppm
)