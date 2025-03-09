#pragma once

#include "z80/Alu.hpp"
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
    BC_Indirect8,
    DE_Indirect8,
    HL_Indirect8,
    HL_Indirect16,
    SP_Indirect16,
    SP,
    IX,
    IY,
    IXL,
    IYL,
    IXH,
    IYH,
    IX_Offset_Indirect8,
    IY_Offset_Indirect8, // TODO decide on naming
    AF_,
    I,
    R,
    ByteImmediate,
    ByteImmediate_A,
    WordImmediate,
    WordImmediateIndirect8,
    WordImmediateIndirect16,
    PcOffset,
    Const_0,
    Const_1,
    Const_2,
    Const_3,
    Const_4,
    Const_5,
    Const_6,
    Const_7,
    Const_8,
    Const_16,
    Const_24,
    Const_32,
    Const_40,
    Const_48,
    Const_52,
    Const_ffff,
  };
  enum class Operation {
    None,
    Load,
    LoadSpecial,
    Add8,
    Add16,
    Subtract8,
    Compare, // compare is subtly different to subtract8 as the undocumented flag bits come from the operand, not result
    Subtract16,
    And,
    Or,
    Xor,
    Bit,
    Reset,
    Set,
    Jump,
    JumpRelative,
    Djnz,
    Call,
    Return,
    Cpl,
    Daa,
    Ccf,
    Scf,
    Invalid,
    Irq,
    IrqMode,
    In,
    Out,
    Exx,
    Exchange,
    EdOp,
    Add16NoFlags,
    Shift,
    Push,
    Pop,
    Inc8,
    Dec8,
    Neg,
    Rrd,
    Rld,
    Halt,
  };
  std::string_view opcode; // todo rename mnemonic
  std::uint8_t length;
  Operation operation;
  Operand lhs{Operand::None};
  Operand rhs{Operand::None};
  struct WithCarry {};
  struct NoFlags {};
  enum class Condition { NonZero, Zero, NoCarry, Carry, NoParity, Parity, Positive, Negative };
  struct EdOpArgs {
    enum class Op { Load = 0x00, Compare = 0x01, In = 0x02, Out = 0x03 };
    Op op;
    bool increment{false};
    bool repeat{false};
  };
  struct ShiftArgs {
    Alu::Direction direction;
    enum class Type { RotateCircular = 0x00, Rotate = 0x01, ShiftArithmetic = 0x02, Shift = 0x03 };
    Type type;
    bool fast;
  };
  enum class WithIrq { Retn, Reti };
  using Args = std::variant<std::monostate, WithCarry, Condition, EdOpArgs, ShiftArgs, NoFlags, WithIrq>;
  Args args{};
  std::int8_t index_offset{};
  std::uint8_t decode_t_states{};

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
