#include "z80/Decoder.hpp"

#include <array>
#include <format>
#include <span>
#include <stdexcept>
#include <utility>

namespace specbolt {

namespace {

using Operand = Instruction::Operand;
using Op = Instruction::Operation;

constexpr Instruction invalid{"??", 1, Op::Invalid};
constexpr Instruction invalid_2{"??", 2, Op::Invalid};

struct RegisterSetUnshifted {
  // static constexpr auto direct = Instruction::Operand::HL;
  static constexpr auto indirect8 = Operand::HL_Indirect8;
  static constexpr auto extra_bytes = 0u;
};

struct RegisterSetIx {
  static constexpr auto direct = Operand::IX;
  static constexpr auto direct_low = Operand::IXL;
  static constexpr auto direct_high = Operand::IXH;
  static constexpr auto indirect8 = Operand::IX_Offset_Indirect8;
  static constexpr auto extra_bytes = 1u;
};

struct RegisterSetIy {
  static constexpr auto direct = Operand::IY;
  static constexpr auto direct_low = Operand::IYL;
  static constexpr auto direct_high = Operand::IYH;
  static constexpr auto indirect8 = Operand::IY_Offset_Indirect8;
  static constexpr auto extra_bytes = 1u;
};


Instruction::Operand source_operand_for(const std::uint8_t opcode) {
  // todo unify with load_source_for
  switch (opcode & 0x7) {
    case 0: return Operand::B;
    case 1: return Operand::C;
    case 2: return Operand::D;
    case 3: return Operand::E;
    case 4: return Operand::H;
    case 5: return Operand::L;
    case 6:
      // sometimes a constant though...
      return Operand::HL_Indirect8;
    case 7: return Operand::A;
    default: std::unreachable();
  }
}

constexpr std::optional<Instruction::Operand> load_source_for(const std::uint8_t opcode) {
  if (const auto quarter = opcode >> 6; quarter == 0) {
    switch (opcode & 0x3f) {
      case 0x01:
      case 0x11:
      case 0x21:
      case 0x31: return Operand::WordImmediate;
      case 0x02:
      case 0x12: return Operand::A;
      case 0x22: return Operand::HL;
      case 0x32: return Operand::A;
      case 0x06:
      case 0x16:
      case 0x26:
      case 0x36: return Operand::ByteImmediate;
      case 0x0a: return Operand::BC_Indirect8;
      case 0x1a: return Operand::DE_Indirect8;
      case 0x2a: return Operand::WordImmediateIndirect16;
      case 0x3a: return Operand::WordImmediateIndirect8;
      case 0x0e:
      case 0x1e:
      case 0x2e:
      case 0x3e: return Operand::ByteImmediate;
      default: break;
    }
  }
  else if (quarter == 1 && opcode != 0x76 /* HALT which would be ld (hl), (hl) otherwise */) {
    static constexpr std::array sources{
        Operand::B, Operand::C, Operand::D, Operand::E, Operand::H, Operand::L, Operand::HL_Indirect8, Operand::A};
    return sources[opcode & 7];
  }
  // anything else isn't a load, unless it's the very special SP, HL load
  if (opcode == 0xf9)
    return Operand::HL;
  return std::nullopt;
}

constexpr std::optional<Instruction::Operand> load_dest_for(const std::uint8_t opcode) {
  if (const auto quarter = opcode >> 6; quarter == 0) {
    switch (opcode) {
      case 0x01: return Operand::BC;
      case 0x11: return Operand::DE;
      case 0x21: return Operand::HL;
      case 0x31: return Operand::SP;
      case 0x02: return Operand::BC_Indirect8;
      case 0x12: return Operand::DE_Indirect8;
      case 0x22: return Operand::WordImmediateIndirect16;
      case 0x32: return Operand::WordImmediateIndirect8;
      case 0x06: return Operand::B;
      case 0x16: return Operand::D;
      case 0x26: return Operand::H;
      case 0x36: return Operand::HL_Indirect8;
      case 0x0e: return Operand::C;
      case 0x1e: return Operand::E;
      case 0x2e: return Operand::L;
      case 0x0a:
      case 0x1a:
      case 0x3a:
      case 0x3e: return Operand::A;
      case 0x2a: return Operand::HL;
      default: break;
    }
  }
  else if (quarter == 1) {
    static constexpr std::array destinations{
        Operand::B, Operand::C, Operand::D, Operand::E, Operand::H, Operand::L, Operand::HL_Indirect8, Operand::A};
    return destinations[opcode >> 3 & 7];
  }
  // anything else isn't a load, unless it's the very special SP load
  if (opcode == 0xf9)
    return Operand::SP;
  return std::nullopt;
}

constexpr std::uint8_t operand_length(const Instruction::Operand operand) {
  switch (operand) {
    default: return 0;
    case Operand::WordImmediateIndirect8:
    case Operand::WordImmediateIndirect16:
    case Operand::WordImmediate: return 2;
    case Operand::IX_Offset_Indirect8:
    case Operand::IY_Offset_Indirect8:
    case Operand::ByteImmediate: return 1;
  }
}

} // namespace

template<typename RegisterSet>
Instruction decode_bit(const std::span<const std::uint8_t> opcodes) {
  const auto opcode = opcodes[0];
  static constexpr std::array registers_from_opcode = {
      Operand::B, Operand::C, Operand::D, Operand::E, Operand::H, Operand::L, RegisterSet::indirect8, Operand::A};
  const auto operand =
      (RegisterSet::indirect8 == Operand::HL_Indirect8) ? registers_from_opcode[opcode & 0x07] : RegisterSet::indirect8;
  if (opcode < 0x40) {
    // it's a shift
    const auto direction = opcode & 0x08 ? Alu::Direction::Right : Alu::Direction::Left;
    const auto type = static_cast<Instruction::ShiftArgs::Type>((opcode & 0x30) >> 4);
    std::array<std::string_view, 8> names = {
        "rlc {}", "rrc {}", "rl {}", "rr {}", "sla {}", "sra {}", "sll {}", "srl {}"};
    return {names[(static_cast<std::size_t>(type) << 1) | (direction == Alu::Direction::Right ? 1 : 0)],
        2 + RegisterSet::extra_bytes, Op::Shift, operand, operand, Instruction::ShiftArgs{direction, type, false}};
  }
  const std::size_t bit_offset = (opcode >> 3) & 0x07;
  switch (opcode >> 6) {
    default: throw std::runtime_error("Not possible");
    // WTF extra bytes crap
    case 1:
      return {"bit {1}, {0}", 2 + RegisterSet::extra_bytes, Op::Bit, operand,
          static_cast<Instruction::Operand>(static_cast<std::size_t>(Operand::Const_0) + bit_offset)};
    case 2:
      return {
          "res {1}, {0}",
          2 + RegisterSet::extra_bytes,
          Op::Reset,
          operand,
          static_cast<Instruction::Operand>(static_cast<std::size_t>(Operand::Const_0) + bit_offset),
      };
    case 3:
      return {"set {1}, {0}", 2 + RegisterSet::extra_bytes, Op::Set, operand,
          static_cast<Instruction::Operand>(static_cast<std::size_t>(Operand::Const_0) + bit_offset)};
  }
  return invalid_2;
}

template<typename RegisterSet>
Instruction decode_ddfd(const std::span<const std::uint8_t> opcodes) {
  const auto offset = static_cast<std::int8_t>(opcodes[1]);
  const auto opcode = opcodes[0];
  switch (opcode) {
    case 0x09: return {"add {}, {}", 2, Op::Add16, RegisterSet::direct, Operand::BC};
    case 0x19: return {"add {}, {}", 2, Op::Add16, RegisterSet::direct, Operand::DE};
    case 0x29: return {"add {}, {}", 2, Op::Add16, RegisterSet::direct, RegisterSet::direct};
    case 0x39: return {"add {}, {}", 2, Op::Add16, RegisterSet::direct, Operand::SP};
    case 0x35: return {"dec {}", 3, Op::Add16NoFlags, RegisterSet::indirect8, Operand::Const_ffff, {}, offset};
    case 0xe1: return {"pop {}", 2, Op::Pop, RegisterSet::direct};
    case 0xe5: return {"push {1}", 2, Op::Push, Operand::None, RegisterSet::direct};
    case 0xcb: {
      // Next byte is the offset, then finally the opcode.
      auto result = decode_bit<RegisterSet>(opcodes.subspan(2));
      result.length += 1; // TODO NO WHAT THE HECK IS THE POINT OF THE EXTRA
      result.index_offset = offset;
      return result;
    }

    default: break;
  }
  const auto transform = [](Operand op) {
    switch (op) {
      case Operand::HL: return RegisterSet::direct;
      case Operand::HL_Indirect8: return RegisterSet::indirect8;
      case Operand::HL_Indirect16: throw std::runtime_error("erk");
      case Operand::H: return RegisterSet::direct_high;
      case Operand::L: return RegisterSet::direct_low;
      default: break;
    }
    return op;
  };
  // The load group...
  if (const auto maybe_load_dest = load_dest_for(opcode); maybe_load_dest.has_value()) {
    const auto dest = transform(maybe_load_dest.value());
    const auto source_opt = load_source_for(opcode);
    if (!source_opt)
      throw std::runtime_error(std::format("Impossible sourceless load for 0x{:02x}", opcode));
    const auto source = opcode >= 0x70 && opcode < 0x78 ? source_opt.value() : transform(source_opt.value());


    return {"ld {}, {}", static_cast<std::uint8_t>(2 + operand_length(dest) + operand_length(source)), Op::Load, dest,
        source, {}, offset};
  }
  const auto make_instruction = [&](const std::string_view name, const Op op, Operand lhs, Operand rhs,
                                    const Instruction::Args args = {}) {
    lhs = transform(lhs);
    rhs = transform(rhs);
    return Instruction{
        name, static_cast<std::uint8_t>(2 + operand_length(lhs) + operand_length(rhs)), op, lhs, rhs, args, offset};
  };
  switch (opcode & 0xf8) {
    case 0x80: return make_instruction("add {}, {}", Op::Add8, Operand::A, source_operand_for(opcode));
    case 0x88:
      return make_instruction("adc {}, {}", Op::Add8, Operand::A, source_operand_for(opcode), Instruction::WithCarry{});
    case 0x90: return make_instruction("sub {}", Op::Subtract8, Operand::A, source_operand_for(opcode));
    case 0x98:
      return make_instruction(
          "sbc {1}", Op::Subtract8, Operand::A, source_operand_for(opcode), Instruction::WithCarry{});
    case 0xa0: return make_instruction("and {1}", Op::And, Operand::A, source_operand_for(opcode));
    case 0xa8: return make_instruction("xor {1}", Op::Xor, Operand::A, source_operand_for(opcode));
    case 0xb0: return make_instruction("or {1}", Op::Or, Operand::A, source_operand_for(opcode));
    case 0xb8: return make_instruction("cp {1}", Op::Compare, Operand::A, source_operand_for(opcode));
    default: break;
  }
  return invalid_2;
}

Instruction decode_ed(const std::span<const std::uint8_t> opcodes) {
  switch (const auto opcode = opcodes[0]) {
    case 0x47: return {"ld {}, {}", 2, Op::Load, Operand::I, Operand::A};
    case 0x46: return {"im 0", 2, Op::IrqMode, Operand::None, Operand::Const_0};
    case 0x56: return {"im 1", 2, Op::IrqMode, Operand::None, Operand::Const_1};
    case 0x5e: return {"im 2", 2, Op::IrqMode, Operand::None, Operand::Const_2};
    case 0x43: return {"ld {}, {}", 4, Op::Load, Operand::WordImmediateIndirect16, Operand::BC};
    case 0x44: return {"neg", 4, Op::Neg, Operand::A};
    case 0x53: return {"ld {}, {}", 4, Op::Load, Operand::WordImmediateIndirect16, Operand::DE};
    case 0x73: return {"ld {}, {}", 4, Op::Load, Operand::WordImmediateIndirect16, Operand::SP};
    case 0x4b: return {"ld {}, {}", 4, Op::Load, Operand::BC, Operand::WordImmediateIndirect16};
    case 0x5b: return {"ld {}, {}", 4, Op::Load, Operand::DE, Operand::WordImmediateIndirect16};
    case 0x7b: return {"ld {}, {}", 4, Op::Load, Operand::SP, Operand::WordImmediateIndirect16};
    case 0x42: return {"sbc {}, {}", 2, Op::Subtract16, Operand::HL, Operand::BC, Instruction::WithCarry{}};
    case 0x52: return {"sbc {}, {}", 2, Op::Subtract16, Operand::HL, Operand::DE, Instruction::WithCarry{}};
    case 0x62: return {"sbc {}, {}", 2, Op::Subtract16, Operand::HL, Operand::HL, Instruction::WithCarry{}};
    case 0x72: return {"sbc {}, {}", 2, Op::Subtract16, Operand::HL, Operand::SP, Instruction::WithCarry{}};
    case 0x4a: return {"adc {}, {}", 2, Op::Add16, Operand::HL, Operand::BC, Instruction::WithCarry{}};
    case 0x5a: return {"adc {}, {}", 2, Op::Add16, Operand::HL, Operand::DE, Instruction::WithCarry{}};
    case 0x6a: return {"adc {}, {}", 2, Op::Add16, Operand::HL, Operand::HL, Instruction::WithCarry{}};
    case 0x7a: return {"adc {}, {}", 2, Op::Add16, Operand::HL, Operand::SP, Instruction::WithCarry{}};
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
      return {names[static_cast<std::size_t>(op) | (increment ? 0 : 4) | (repeat ? 8 : 0)], 2, Op::EdOp, Operand::None,
          Operand::None, Instruction::EdOpArgs{op, increment, repeat}};
    }
    default: break;
  }
  return invalid_2;
}

Instruction decode(const std::array<std::uint8_t, 4> opcodes) {
  const auto opcode = opcodes[0];
  if (const auto maybe_load_dest = load_dest_for(opcode); maybe_load_dest.has_value()) {
    const auto dest = maybe_load_dest.value();
    const auto source = load_source_for(opcode);
    if (!source)
      throw std::runtime_error(std::format("Impossible sourceless load for 0x{:02x}", opcode));

    return {"ld {}, {}", static_cast<std::uint8_t>(1 + operand_length(dest) + operand_length(*source)), Op::Load, dest,
        *source};
  }

  const auto operands = std::span{opcodes}.subspan(1);
  switch (opcode) {
    case 0x00: return {"nop", 1, Op::None};
    case 0x08: return {"ex af, af'", 1, Op::Exchange, Operand::AF, Operand::AF_};
    case 0x10:
      return {
          "djnz {1}",
          2,
          Op::Djnz,
          Operand::None,
          Operand::PcOffset,
      };
    case 0x18: return {"jr {1}", 2, Op::Jump, Operand::None, Operand::PcOffset};
    case 0x20: return {"jr nz {1}", 2, Op::Jump, Operand::None, Operand::PcOffset, Instruction::Condition::NonZero};
    case 0x28: return {"jr z {1}", 2, Op::Jump, Operand::None, Operand::PcOffset, Instruction::Condition::Zero};
    case 0x30: return {"jr nc {1}", 2, Op::Jump, Operand::None, Operand::PcOffset, Instruction::Condition::NoCarry};
    case 0x38: return {"jr c {1}", 2, Op::Jump, Operand::None, Operand::PcOffset, Instruction::Condition::Carry};

    case 0x03: return {"inc {}", 1, Op::Add16NoFlags, Operand::BC, Operand::Const_1};
    case 0x13: return {"inc {}", 1, Op::Add16NoFlags, Operand::DE, Operand::Const_1};
    case 0x23: return {"inc {}", 1, Op::Add16NoFlags, Operand::HL, Operand::Const_1};
    case 0x33: return {"inc {}", 1, Op::Add16NoFlags, Operand::SP, Operand::Const_1};

    case 0x04: return {"inc {}", 1, Op::Inc8, Operand::B};
    case 0x14: return {"inc {}", 1, Op::Inc8, Operand::D};
    case 0x24: return {"inc {}", 1, Op::Inc8, Operand::H};
    case 0x34: return {"inc {}", 1, Op::Inc8, Operand::HL_Indirect8};
    case 0x0c: return {"inc {}", 1, Op::Inc8, Operand::C};
    case 0x1c: return {"inc {}", 1, Op::Inc8, Operand::E};
    case 0x2c: return {"inc {}", 1, Op::Inc8, Operand::L};
    case 0x3c: return {"inc {}", 1, Op::Inc8, Operand::A};

    // Sure would be nice to generalise these...
    case 0x05: return {"dec {}", 1, Op::Dec8, Operand::B};
    case 0x15: return {"dec {}", 1, Op::Dec8, Operand::D};
    case 0x25: return {"dec {}", 1, Op::Dec8, Operand::H};
    case 0x35: return {"dec {}", 1, Op::Dec8, Operand::HL_Indirect8};
    case 0x0d: return {"dec {}", 1, Op::Dec8, Operand::C};
    case 0x1d: return {"dec {}", 1, Op::Dec8, Operand::E};
    case 0x2d: return {"dec {}", 1, Op::Dec8, Operand::L};
    case 0x3d: return {"dec {}", 1, Op::Dec8, Operand::A};
    case 0x0b: return {"dec {}", 1, Op::Add16NoFlags, Operand::BC, Operand::Const_ffff};
    case 0x1b: return {"dec {}", 1, Op::Add16NoFlags, Operand::DE, Operand::Const_ffff};
    case 0x2b: return {"dec {}", 1, Op::Add16NoFlags, Operand::HL, Operand::Const_ffff};
    case 0x3b: return {"dec {}", 1, Op::Add16NoFlags, Operand::SP, Operand::Const_ffff};

    case 0x76: return {"halt", 1, Op::Invalid};

    case 0x09: return {"add {}, {}", 1, Op::Add16, Operand::HL, Operand::BC};
    case 0x19: return {"add {}, {}", 1, Op::Add16, Operand::HL, Operand::DE};
    case 0x29: return {"add {}, {}", 1, Op::Add16, Operand::HL, Operand::HL};
    case 0x39: return {"add {}, {}", 1, Op::Add16, Operand::HL, Operand::SP};

    case 0x0f:
      return {"rrca", 1, Op::Shift, Operand::A, Operand::None,
          Instruction::ShiftArgs{Alu::Direction::Right, Instruction::ShiftArgs::Type::RotateCircular, true}};
    case 0x1f:
      return {"rra", 1, Op::Shift, Operand::A, Operand::None,
          Instruction::ShiftArgs{Alu::Direction::Right, Instruction::ShiftArgs::Type::Rotate, true}};
    case 0x07:
      return {"rlca", 1, Op::Shift, Operand::A, Operand::None,
          Instruction::ShiftArgs{Alu::Direction::Left, Instruction::ShiftArgs::Type::RotateCircular, true}};
    case 0x17:
      return {"rla", 1, Op::Shift, Operand::A, Operand::None,
          Instruction::ShiftArgs{Alu::Direction::Left, Instruction::ShiftArgs::Type::Rotate, true}};

    // TODO this is the same table as the IX/IY stuff, so maybe unify it
    case 0x80:
    case 0x81:
    case 0x82:
    case 0x83:
    case 0x84:
    case 0x85:
    case 0x86:
    case 0x87: return {"add {}, {}", 1, Op::Add8, Operand::A, source_operand_for(opcode)};
    case 0xc6: return {"add {1}", 2, Op::Add8, Operand::A, Operand::ByteImmediate};
    case 0x88:
    case 0x89:
    case 0x8a:
    case 0x8b:
    case 0x8c:
    case 0x8d:
    case 0x8e:
    case 0x8f: return {"adc {}, {}", 1, Op::Add8, Operand::A, source_operand_for(opcode), Instruction::WithCarry{}};
    case 0xce: return {"adc {}, {}", 2, Op::Add8, Operand::A, Operand::ByteImmediate, Instruction::WithCarry{}};

    case 0x90:
    case 0x91:
    case 0x92:
    case 0x93:
    case 0x94:
    case 0x95:
    case 0x96:
    case 0x97: return {"sub {}, {}", 1, Op::Subtract8, Operand::A, source_operand_for(opcode)};
    case 0xd6: return {"sub {1}", 2, Op::Subtract8, Operand::A, Operand::ByteImmediate};
    case 0x98:
    case 0x99:
    case 0x9a:
    case 0x9b:
    case 0x9c:
    case 0x9d:
    case 0x9e:
    case 0x9f:
      return {"sbc {}, {}", 1, Op::Subtract8, Operand::A, source_operand_for(opcode), Instruction::WithCarry{}};
    case 0xde: return {"sbc {}, {}", 2, Op::Subtract8, Operand::A, Operand::ByteImmediate, Instruction::WithCarry{}};

    case 0xa0:
    case 0xa1:
    case 0xa2:
    case 0xa3:
    case 0xa4:
    case 0xa5:
    case 0xa6:
    case 0xa7: return {"and {}, {}", 1, Op::And, Operand::A, source_operand_for(opcode)};
    case 0xe6: return {"and {1}", 2, Op::And, Operand::A, Operand::ByteImmediate};
    case 0xa8:
    case 0xa9:
    case 0xaa:
    case 0xab:
    case 0xac:
    case 0xad:
    case 0xae:
    case 0xaf: return {"xor {}, {}", 1, Op::Xor, Operand::A, source_operand_for(opcode)};
    case 0xee: return {"xor {1}", 2, Op::Xor, Operand::A, Operand::ByteImmediate};

    case 0xb0:
    case 0xb1:
    case 0xb2:
    case 0xb3:
    case 0xb4:
    case 0xb5:
    case 0xb6:
    case 0xb7: return {"or {}, {}", 1, Op::Or, Operand::A, source_operand_for(opcode)};
    case 0xf6: return {"or {1}", 2, Op::Or, Operand::A, Operand::ByteImmediate};

    case 0xb8:
    case 0xb9:
    case 0xba:
    case 0xbb:
    case 0xbc:
    case 0xbd:
    case 0xbe:
    case 0xbf:
      // hackily use "a" as the destination (e.g. other operand, but Compare doesn't actually update it).
      return {"cp {1}", 1, Op::Compare, Operand::A, source_operand_for(opcode)};
    case 0xfe: return {"cp {1}", 2, Op::Compare, Operand::A, Operand::ByteImmediate};

    case 0xc5: return {"push {1}", 1, Op::Push, Operand::None, Operand::BC};
    case 0xd5: return {"push {1}", 1, Op::Push, Operand::None, Operand::DE};
    case 0xe5: return {"push {1}", 1, Op::Push, Operand::None, Operand::HL};
    case 0xf5: return {"push {1}", 1, Op::Push, Operand::None, Operand::AF};
    case 0xc1: return {"pop {}", 1, Op::Pop, Operand::BC};
    case 0xd1: return {"pop {}", 1, Op::Pop, Operand::DE};
    case 0xe1: return {"pop {}", 1, Op::Pop, Operand::HL};
    case 0xf1: return {"pop {}", 1, Op::Pop, Operand::AF};

    case 0xc3: return {"jp {1}", 3, Op::Jump, Operand::None, Operand::WordImmediate};
    case 0xe9: return {"jp (hl)", 3, Op::Jump, Operand::None, Operand::HL};
    case 0xd9: return {"exx", 1, Op::Exx}; // TODO arg this does not fit neatly into my world.
    case 0xcb: return decode_bit<RegisterSetUnshifted>(operands);
    case 0xcd: return {"call {1}", 3, Op::Call, Operand::None, Operand::WordImmediate};
    case 0xc4:
      return {"call nz {1}", 3, Op::Call, Operand::None, Operand::WordImmediate, Instruction::Condition::NonZero};
    case 0xcc: return {"call z {1}", 3, Op::Call, Operand::None, Operand::WordImmediate, Instruction::Condition::Zero};
    case 0xd4:
      return {"call nc {1}", 3, Op::Call, Operand::None, Operand::WordImmediate, Instruction::Condition::NoCarry};
    case 0xdc: return {"call c {1}", 3, Op::Call, Operand::None, Operand::WordImmediate, Instruction::Condition::Carry};
    case 0xe4:
      return {"call po {1}", 3, Op::Call, Operand::None, Operand::WordImmediate, Instruction::Condition::NoParity};
    case 0xec:
      return {"call pe {1}", 3, Op::Call, Operand::None, Operand::WordImmediate, Instruction::Condition::Parity};
    case 0xf4:
      return {"call p {1}", 3, Op::Call, Operand::None, Operand::WordImmediate, Instruction::Condition::Positive};
    case 0xfc:
      return {"call m {1}", 3, Op::Call, Operand::None, Operand::WordImmediate, Instruction::Condition::Negative};
    case 0xc8: return {"ret z", 1, Op::Return, Operand::None, Operand::None, Instruction::Condition::Zero};
    case 0xc9: return {"ret", 1, Op::Return};
    case 0xd8: return {"ret c", 1, Op::Return, Operand::None, Operand::None, Instruction::Condition::Carry};
    case 0xc0: return {"ret nz", 1, Op::Return, Operand::None, Operand::None, Instruction::Condition::NonZero};
    case 0xd0: return {"ret nc", 1, Op::Return, Operand::None, Operand::None, Instruction::Condition::NoCarry};

    case 0x27: return {"daa", 1, Op::Daa, Operand::A};
    case 0x2f: return {"cpl", 1, Op::Cpl, Operand::A};
    case 0x37: return {"scf", 1, Op::Scf};
    case 0x3f: return {"ccf", 1, Op::Ccf};

    case 0xc2:
      return {"jp nz, {1}", 3, Op::Jump, Operand::None, Operand::WordImmediate, Instruction::Condition::NonZero};
    case 0xd2:
      return {"jp nc, {1}", 3, Op::Jump, Operand::None, Operand::WordImmediate, Instruction::Condition::NoCarry};
    case 0xe2:
      return {"jp po, {1}", 3, Op::Jump, Operand::None, Operand::WordImmediate, Instruction::Condition::NoParity};
    case 0xf2:
      return {"jp p, {1}", 3, Op::Jump, Operand::None, Operand::WordImmediate, Instruction::Condition::Positive};
    case 0xca: return {"jp z, {1}", 3, Op::Jump, Operand::None, Operand::WordImmediate, Instruction::Condition::Zero};
    case 0xda: return {"jp c, {1}", 3, Op::Jump, Operand::None, Operand::WordImmediate, Instruction::Condition::Carry};
    case 0xea:
      return {"jp pe, {1}", 3, Op::Jump, Operand::None, Operand::WordImmediate, Instruction::Condition::Parity};
    case 0xfa:
      return {"jp m, {1}", 3, Op::Jump, Operand::None, Operand::WordImmediate, Instruction::Condition::Negative};

    case 0xdd: return decode_ddfd<RegisterSetIx>(operands);
    case 0xed: return decode_ed(operands);
    case 0xd3: return {"out ({}), {}", 2, Op::Out, Operand::ByteImmediate, Operand::A};
    case 0xe3: return {"ex {}, {}", 1, Op::Exchange, Operand::SP_Indirect16, Operand::HL};
    case 0xeb: return {"ex {}, {}", 1, Op::Exchange, Operand::DE, Operand::HL};
    case 0xf3: return {"di", 1, Op::Irq, Operand::None, Operand::Const_0};
    case 0xfb: return {"ei", 1, Op::Irq, Operand::None, Operand::Const_1};
    case 0xfd: return decode_ddfd<RegisterSetIy>(operands);
    case 0xc7: return {"rst 0x00", 1, Op::Call, Operand::None, Operand::Const_0};
    case 0xcf: return {"rst 0x08", 1, Op::Call, Operand::None, Operand::Const_8};
    case 0xd7: return {"rst 0x10", 1, Op::Call, Operand::None, Operand::Const_16};
    case 0xdf: return {"rst 0x18", 1, Op::Call, Operand::None, Operand::Const_24};
    case 0xe7: return {"rst 0x20", 1, Op::Call, Operand::None, Operand::Const_32};
    case 0xef: return {"rst 0x28", 1, Op::Call, Operand::None, Operand::Const_40};
    case 0xf7: return {"rst 0x30", 1, Op::Call, Operand::None, Operand::Const_48};
    case 0xff: return {"rst 0x38", 1, Op::Call, Operand::None, Operand::Const_52};
    default: break;
  }
  return invalid;
}

} // namespace specbolt
