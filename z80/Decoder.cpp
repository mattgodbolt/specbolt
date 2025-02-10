#include "z80/Decoder.hpp"

#include <array>
#include <span>
#include <stdexcept>
#include <utility>

namespace specbolt {

namespace {

constexpr Instruction invalid{"??", 1, Instruction::Operation::Invalid};
constexpr Instruction invalid_2{"??", 2, Instruction::Operation::Invalid};

struct RegisterSetUnshifted {
  // static constexpr auto direct = Instruction::Operand::HL;
  static constexpr auto indirect = Instruction::Operand::HL_Indirect;
  static constexpr auto extra_bytes = 0u;
};

struct RegisterSetIx {
  static constexpr auto direct = Instruction::Operand::IX;
  static constexpr auto indirect = Instruction::Operand::IX_Offset_Indirect8;
  static constexpr auto extra_bytes = 1u;
};

struct RegisterSetIy {
  static constexpr auto direct = Instruction::Operand::IY;
  static constexpr auto indirect = Instruction::Operand::IY_Offset_Indirect8;
  static constexpr auto extra_bytes = 1u;
};

} // namespace

template<typename RegisterSet>
Instruction decode_bit(const std::span<const std::uint8_t> opcodes) {
  const auto opcode = opcodes[0];
  static constexpr std::array registers_from_opcode = {Instruction::Operand::B, Instruction::Operand::C,
      Instruction::Operand::D, Instruction::Operand::E, Instruction::Operand::H, Instruction::Operand::L,
      RegisterSet::indirect, Instruction::Operand::A};
  const auto operand = (RegisterSet::indirect == Instruction::Operand::HL_Indirect)
                           ? registers_from_opcode[opcode & 0x07]
                           : RegisterSet::indirect;
  if (opcode < 0x40) {
    // it's a shift
    const auto direction =
        opcode & 0x08 ? Instruction::ShiftArgs::Direction::Right : Instruction::ShiftArgs::Direction::Left;
    const auto type = static_cast<Instruction::ShiftArgs::Type>((opcode & 0x30) >> 4);
    std::array<std::string_view, 8> names = {
        "rlc {}", "rrc {}", "rl {}", "rr {}", "sla {}", "sra {}", "sll {}", "srl {}"};
    return {
        names[(static_cast<std::size_t>(type) << 1) | (direction == Instruction::ShiftArgs::Direction::Right ? 1 : 0)],
        2 + RegisterSet::extra_bytes, Instruction::Operation::Shift, operand, operand,
        Instruction::ShiftArgs{direction, type}};
  }
  const std::size_t bit_offset = (opcode >> 3) & 0x07;
  switch (opcode >> 6) {
    default:
      throw std::runtime_error("Not possible");
    // WTF extra bytes crap
    case 1:
      return {"bit {1}, {0}", 2 + RegisterSet::extra_bytes, Instruction::Operation::Bit, operand,
          static_cast<Instruction::Operand>(static_cast<std::size_t>(Instruction::Operand::Const_0) + bit_offset)};
    case 2:
      return {
          "res {1}, {0}",
          2 + RegisterSet::extra_bytes,
          Instruction::Operation::Reset,
          operand,
          static_cast<Instruction::Operand>(static_cast<std::size_t>(Instruction::Operand::Const_0) + bit_offset),
      };
    case 3:
      return {"set {1}, {0}", 2 + RegisterSet::extra_bytes, Instruction::Operation::Set, operand,
          static_cast<Instruction::Operand>(static_cast<std::size_t>(Instruction::Operand::Const_0) + bit_offset)};
  }
  return invalid_2;
}

template<typename RegisterSet>
Instruction decode_ddfd(const std::span<const std::uint8_t> opcodes) {
  const auto offset = static_cast<std::int8_t>(opcodes[1]);
  switch (opcodes[0]) {
    case 0x75:
      return {"ld {}, {}", 3, Instruction::Operation::Load, RegisterSet::indirect, Instruction::Operand::L, {}, offset};
    case 0x21:
      return {"ld {}, {}", 4, Instruction::Operation::Load, RegisterSet::direct, Instruction::Operand::WordImmediate};
    case 0x35:
      return {"dec {}", 3, Instruction::Operation::Add16NoFlags, RegisterSet::indirect,
          Instruction::Operand::Const_ffff, {}, offset};
    case 0xcb: {
      // Next byte is the offset, then finally the opcode
      // BLEH
      auto result = decode_bit<RegisterSet>(opcodes.subspan(2));
      result.length += 1; // TODO NO WHAT THE HECK IS THE POINT OF THE EXTRA
      result.index_offset = offset;
      return result;
    }
    default:
      break;
  }
  return invalid_2;
}

Instruction decode_ed(const std::span<const std::uint8_t> opcodes) {
  switch (const auto opcode = opcodes[0]) {
    case 0x43:
      return {"ld {}, {}", 4, Instruction::Operation::Load, Instruction::Operand::WordImmediateIndirect16,
          Instruction::Operand::BC};
    case 0x53:
      return {"ld {}, {}", 4, Instruction::Operation::Load, Instruction::Operand::WordImmediateIndirect16,
          Instruction::Operand::DE};
    case 0x47:
      return {"ld {}, {}", 2, Instruction::Operation::Load, Instruction::Operand::I, Instruction::Operand::A};
    case 0x52:
      return {"sbc {}, {}", 2, Instruction::Operation::Subtract16, Instruction::Operand::HL, Instruction::Operand::DE,
          Instruction::WithCarry{}};
    case 0x7b:
      return {"ld {}, {}", 4, Instruction::Operation::Load, Instruction::Operand::SP,
          Instruction::Operand::WordImmediateIndirect16};
    case 0x46:
      return {"im 0", 2, Instruction::Operation::IrqMode, Instruction::Operand::None, Instruction::Operand::Const_0};
    case 0x56:
      return {"im 1", 2, Instruction::Operation::IrqMode, Instruction::Operand::None, Instruction::Operand::Const_1};
    case 0x5e:
      return {"im 2", 2, Instruction::Operation::IrqMode, Instruction::Operand::None, Instruction::Operand::Const_2};
    case 0xa0:
    case 0xa1:
    case 0xa2:
    case 0xa3:
    case 0xa8:
    case 0xa9:
    case 0xaa:
    case 0xab:
    case 0xb0:
    case 0xb1:
    case 0xb2:
    case 0xb3:
    case 0xb8:
    case 0xb9:
    case 0xba:
    case 0xbb: {
      const auto op = static_cast<Instruction::EdOpArgs::Op>(opcode & 0x3);
      const auto repeat = (opcode & 0xf0) == 0xb0;
      const auto increment = (opcode & 0x8) == 0;
      static std::array<std::string_view, 16> names = {"ldi", "cpi", "ini", "outi", "ldd", "cpd", "ind", "outd", "ldir",
          "cpir", "inir", "otir", "lddr", "cpdr", "indr", "otdr"};
      return {names[static_cast<std::size_t>(op) | (increment ? 0 : 4) | (repeat ? 8 : 0)], 2,
          Instruction::Operation::EdOp, Instruction::Operand::None, Instruction::Operand::None,
          Instruction::EdOpArgs{op, increment, repeat}};
    }
    default:
      break;
  }
  return invalid_2;
}

Instruction::Operand source_operand_for(const std::uint8_t opcode) {
  switch (opcode & 0x7) {
    case 0:
      return Instruction::Operand::B;
    case 1:
      return Instruction::Operand::C;
    case 2:
      return Instruction::Operand::D;
    case 3:
      return Instruction::Operand::E;
    case 4:
      return Instruction::Operand::H;
    case 5:
      return Instruction::Operand::L;
    case 6:
      // sometimes a constant though...
      return Instruction::Operand::HL_Indirect;
    case 7:
      return Instruction::Operand::A;
    default:
      std::unreachable();
  }
}

Instruction::Operand dest_operand_for(const std::uint8_t opcode) {
  switch (opcode & 0xf8) {
    case 0x30:
      // gotta be cleverer than this...look at z80 decoder diagram?
      if (opcode == 0x36)
        return Instruction::Operand::HL_Indirect;
      break;
    case 0x40:
      return Instruction::Operand::B;
    case 0x48:
      return Instruction::Operand::C;
    case 0x50:
      return Instruction::Operand::D;
    case 0x58:
      return Instruction::Operand::E;
    case 0x60:
      return Instruction::Operand::H;
    case 0x68:
      return Instruction::Operand::L;
    default:
      break;
  }
  throw std::runtime_error("not worked out yet");
}

Instruction decode(const std::array<std::uint8_t, 4> opcodes) {
  const auto operands = std::span{opcodes}.subspan(1);
  switch (const auto opcode = opcodes[0]) {
    case 0x00:
      return {"nop", 1, Instruction::Operation::None};
    case 0x01:
      return {
          "ld {}, {}", 3, Instruction::Operation::Load, Instruction::Operand::BC, Instruction::Operand::WordImmediate};
    case 0x02:
      return {"ld {}, {}", 1, Instruction::Operation::Load, Instruction::Operand::BC_Indirect, Instruction::Operand::A};
    case 0x03:
      return {
          "inc {}", 1, Instruction::Operation::Add16NoFlags, Instruction::Operand::BC, Instruction::Operand::Const_1};
    case 0x04:
      return {"inc {}", 1, Instruction::Operation::Add8, Instruction::Operand::B, Instruction::Operand::Const_1};
    case 0x11:
      return {
          "ld {}, {}", 3, Instruction::Operation::Load, Instruction::Operand::DE, Instruction::Operand::WordImmediate};
    case 0x18:
      return {"jr {1}", 2, Instruction::Operation::Jump, Instruction::Operand::None, Instruction::Operand::PcOffset};
    case 0x19:
      return {"add {}, {}", 1, Instruction::Operation::Add16, Instruction::Operand::HL, Instruction::Operand::DE};
    case 0x20:
      return {"jr nz {1}", 2, Instruction::Operation::Jump, Instruction::Operand::None, Instruction::Operand::PcOffset,
          Instruction::Condition::NonZero};
    case 0x21:
      return {
          "ld {}, {}", 3, Instruction::Operation::Load, Instruction::Operand::HL, Instruction::Operand::WordImmediate};
    case 0x28:
      return {"jr z {1}", 2, Instruction::Operation::Jump, Instruction::Operand::None, Instruction::Operand::PcOffset,
          Instruction::Condition::Zero};
    case 0x2a:
      return {"ld {}, {}", 3, Instruction::Operation::Load, Instruction::Operand::HL,
          Instruction::Operand::WordImmediateIndirect16};
    case 0x30:
      return {"jr nc {1}", 2, Instruction::Operation::Jump, Instruction::Operand::None, Instruction::Operand::PcOffset,
          Instruction::Condition::NoCarry};
    case 0x38:
      return {"jr c {1}", 2, Instruction::Operation::Jump, Instruction::Operand::None, Instruction::Operand::PcOffset,
          Instruction::Condition::Carry};
    case 0x22:
      return {"ld {}, {}", 3, Instruction::Operation::Load, Instruction::Operand::WordImmediateIndirect16,
          Instruction::Operand::HL};
    case 0x23:
      return {
          "inc {}", 1, Instruction::Operation::Add16NoFlags, Instruction::Operand::HL, Instruction::Operand::Const_1};
    case 0x32:
      return {"ld {}, {}", 3, Instruction::Operation::Load, Instruction::Operand::WordImmediateIndirect16,
          Instruction::Operand::A};
    case 0x35:
      return {"dec {}", 1, Instruction::Operation::Add16NoFlags, Instruction::Operand::HL_Indirect,
          Instruction::Operand::Const_ffff};
    case 0x06:
    case 0x16:
    case 0x26:
    case 0x36:
      return {
          "ld {}, {}", 2, Instruction::Operation::Load, dest_operand_for(opcode), Instruction::Operand::ByteImmediate};
    case 0x3e:
      return {
          "ld {}, {}", 2, Instruction::Operation::Load, Instruction::Operand::A, Instruction::Operand::ByteImmediate};

    case 0x40:
    case 0x41:
    case 0x42:
    case 0x43:
    case 0x44:
    case 0x45:
    case 0x46:
    case 0x47:
    case 0x48:
    case 0x49:
    case 0x4a:
    case 0x4b:
    case 0x4c:
    case 0x4d:
    case 0x4e:
    case 0x4f:
    case 0x60:
    case 0x61:
    case 0x62:
    case 0x63:
    case 0x65:
    case 0x66:
    case 0x67:
    case 0x68:
    case 0x69:
    case 0x6a:
    case 0x6b:
    case 0x6c:
    case 0x6d:
    case 0x6e:
    case 0x6f:
      return {"ld {}, {}", 1, Instruction::Operation::Load, dest_operand_for(opcode), source_operand_for(opcode)};

    case 0x80:
    case 0x81:
    case 0x82:
    case 0x83:
    case 0x84:
    case 0x85:
    case 0x86:
    case 0x87:
      return {"add {}, {}", 1, Instruction::Operation::Add8, Instruction::Operand::A, source_operand_for(opcode)};
    case 0x88:
    case 0x89:
    case 0x8a:
    case 0x8b:
    case 0x8c:
    case 0x8d:
    case 0x8e:
    case 0x8f:
      return {"adc {}, {}", 1, Instruction::Operation::Add8, Instruction::Operand::A, source_operand_for(opcode),
          Instruction::WithCarry{}};

    case 0x90:
    case 0x91:
    case 0x92:
    case 0x93:
    case 0x94:
    case 0x95:
    case 0x96:
    case 0x97:
      return {"sub {}, {}", 1, Instruction::Operation::Subtract8, Instruction::Operand::A, source_operand_for(opcode)};
    case 0x98:
    case 0x99:
    case 0x9a:
    case 0x9b:
    case 0x9c:
    case 0x9d:
    case 0x9e:
    case 0x9f:
      return {"sbc {}, {}", 1, Instruction::Operation::Subtract8, Instruction::Operand::A, source_operand_for(opcode),
          Instruction::WithCarry{}};

    case 0xa0:
    case 0xa1:
    case 0xa2:
    case 0xa3:
    case 0xa4:
    case 0xa5:
    case 0xa6:
    case 0xa7:
      return {"sub {}, {}", 1, Instruction::Operation::And, Instruction::Operand::A, source_operand_for(opcode)};
    case 0xa8:
    case 0xa9:
    case 0xaa:
    case 0xab:
    case 0xac:
    case 0xad:
    case 0xae:
    case 0xaf:
      return {"xor {}, {}", 1, Instruction::Operation::Xor, Instruction::Operand::A, source_operand_for(opcode)};

    case 0xb0:
    case 0xb1:
    case 0xb2:
    case 0xb3:
    case 0xb4:
    case 0xb5:
    case 0xb6:
    case 0xb7:
      return {"or {}, {}", 1, Instruction::Operation::Or, Instruction::Operand::A, source_operand_for(opcode)};
    case 0xb8:
    case 0xb9:
    case 0xba:
    case 0xbb:
    case 0xbc:
    case 0xbd:
    case 0xbe:
    case 0xbf:
      // hackily use "a" as the destination (e.g. other operand, but Compare doesn't actually update it).
      return {"cp {1}", 1, Instruction::Operation::Compare, Instruction::Operand::A, source_operand_for(opcode)};

    case 0x2b:
      return {"dec {}", 1, Instruction::Operation::Add16NoFlags, Instruction::Operand::HL,
          Instruction::Operand::Const_ffff};
    case 0xc3:
      return {
          "jp {1}", 3, Instruction::Operation::Jump, Instruction::Operand::None, Instruction::Operand::WordImmediate};
    case 0xd9:
      return {"exx", 1, Instruction::Operation::Exx}; // TODO arg this does not fit neatly into my world.
    case 0xcb:
      return decode_bit<RegisterSetUnshifted>(operands);
    case 0xcd:
      return {
          "call {1}", 3, Instruction::Operation::Call, Instruction::Operand::None, Instruction::Operand::WordImmediate};
    case 0xdd:
      return decode_ddfd<RegisterSetIx>(operands);
    case 0xed:
      return decode_ed(operands);
    case 0xd3:
      return {
          "out ({}), {}", 2, Instruction::Operation::Out, Instruction::Operand::ByteImmediate, Instruction::Operand::A};
    case 0xeb:
      return {"ex {}, {}", 1, Instruction::Operation::Exchange, Instruction::Operand::DE, Instruction::Operand::HL};
    case 0xf3:
      return {"di", 1, Instruction::Operation::Irq, Instruction::Operand::None, Instruction::Operand::Const_0};
    case 0xfb:
      return {"ei", 1, Instruction::Operation::Irq, Instruction::Operand::None, Instruction::Operand::Const_1};
    case 0xfd:
      return decode_ddfd<RegisterSetIy>(operands);
    case 0xf9:
      return {"ld {}, {}", 1, Instruction::Operation::Load, Instruction::Operand::SP, Instruction::Operand::HL};
    default:
      break;
  }
  return invalid;
}

} // namespace specbolt
