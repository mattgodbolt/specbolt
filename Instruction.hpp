#pragma once

#include <cstdint>
#include <string_view>

namespace specbolt {

struct Instruction {
  enum class Operand {
    None,
    A,
    B,
    C,
    D,
    E,
    H,
    L,
    AF,
    BC,
    DE,
    HL,
    BC_Indirect,
    DE_Indirect,
    HL_Indirect,
    SP,
    IX,
    IY,
    IX_Offset_Indirect,
    IY_Offset_Indirect, // TODO decide on naming
    AF_,
    ByteImmediate,
    WordImmediate,
    WordImmediateIndirect,
    Offset,
    PcOffset,
    Const_0,
    Const_1,
    Const_2,
    Const_4,
    Const_8,
    Const_16,
    Const_32,
    Const_64,
    Const_128,
  };
  enum class Operation {
    None,
    Load,
    Add,
    Subtract,
    Xor,
    Bit, // Maybe and?
    Jump,
    Invalid,
    Irq,
  };
  std::string_view opcode;
  uint8_t length;
  Operation operation;
  Operand dest{Operand::None};
  Operand source{Operand::None};
  struct Input {
    std::uint16_t dest;
    std::uint16_t source;
    std::uint16_t pc;
    std::uint16_t sp;
    std::uint8_t flags;
  };
  struct Output {
    std::uint16_t value;
    std::uint16_t pc;
    std::uint16_t sp;
    std::uint8_t flags;
  };

  [[nodiscard]] static Output apply(Operation operation, const Input &input);
};

} // namespace specbolt
