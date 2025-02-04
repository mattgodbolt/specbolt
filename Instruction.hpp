#pragma once

#include <cstdint>
#include <string_view>

#include "Flags.hpp"

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
    I,
    ByteImmediate,
    WordImmediate,
    WordImmediateIndirect,
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
    Add8,
    Add8WithCarry,
    Add16,
    Add16WithCarry,
    Subtract8,
    Compare, // compare is subtly different to subtract8 as the undocumented flag bits come from the operand, not result
    Subtract8WithCarry,
    Subtract16,
    Subtract16WithCarry,
    And,
    Or,
    Xor,
    Bit, // Maybe and?
    Jump,
    Invalid,
    Irq,
    Out, // what to do about this?
  };
  std::string_view opcode;
  std::uint8_t length;
  Operation operation;
  // CONSIDER an output, LHS and RHS, to allow "cmp" to do NONE = A - source?
  Operand dest{Operand::None};
  Operand source{Operand::None};
  enum class Condition { None, NonZero, Zero };
  Condition condition{Condition::None};
  struct Input {
    std::uint16_t dest;
    std::uint16_t source;
    std::uint16_t pc;
    std::uint16_t sp;
    Flags flags;
    bool iff1;
    bool iff2;
  };
  struct Output {
    std::uint16_t value;
    std::uint16_t pc;
    std::uint16_t sp;
    Flags flags;
    bool iff1;
    bool iff2;
    std::uint8_t extra_t_states{};
  };

  [[nodiscard]] static Output apply(Operation operation, const Input &input);
};

} // namespace specbolt
