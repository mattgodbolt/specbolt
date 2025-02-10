#pragma once

#include "z80/Flags.hpp"

#include <cstdint>
#include <string_view>
#include <variant>

namespace specbolt {

class Z80;

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
    WordImmediateIndirect16,
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
    Add16,
    Subtract8,
    Compare, // compare is subtly different to subtract8 as the undocumented flag bits come from the operand, not result
    Subtract16,
    And,
    Or,
    Xor,
    Bit, // Maybe and?
    Jump,
    Invalid,
    Irq,
    Out, // what to do about this?
    Exx,
    Exchange,
    EdOp,
  };
  std::string_view opcode;
  std::uint8_t length;
  Operation operation;
  Operand lhs{Operand::None};
  Operand rhs{Operand::None};
  struct WithCarry {};
  enum class Condition { NonZero, Zero, NoCarry, Carry };
  struct EdOpArgs {
    enum class Op { Load = 0x00, Compare = 0x01, In = 0x02, Out = 0x03 };
    Op op;
    bool increment{false};
    bool repeat{false};
  };
  std::variant<std::monostate, WithCarry, Condition, EdOpArgs> args{};
  struct Input {
    std::uint16_t lhs;
    std::uint16_t rhs;
    Flags flags;
  };
  struct Output {
    std::uint16_t value;
    Flags flags;
    std::uint8_t extra_t_states{};
  };

  [[nodiscard]] Output apply(Input input, Z80 &cpu) const;
  [[nodiscard]] bool should_execute(Flags flags) const;
};

} // namespace specbolt
