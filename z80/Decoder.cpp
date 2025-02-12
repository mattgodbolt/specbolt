#include "z80/Decoder.hpp"

#include <array>
#include <format>
#include <span>
#include <stdexcept>
#include <utility>

namespace specbolt {

namespace {

constexpr Instruction invalid{"??", 1, Instruction::Operation::Invalid};
constexpr Instruction invalid_2{"??", 2, Instruction::Operation::Invalid};

struct RegisterSetUnshifted {
  // static constexpr auto direct = Instruction::Operand::HL;
  static constexpr auto indirect = Instruction::Operand::HL_Indirect8;
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
  const auto operand = (RegisterSet::indirect == Instruction::Operand::HL_Indirect8)
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
    default: throw std::runtime_error("Not possible");
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
    default: break;
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
    case 0x47: return {"ld {}, {}", 2, Instruction::Operation::Load, Instruction::Operand::I, Instruction::Operand::A};
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
    default: break;
  }
  return invalid_2;
}

Instruction::Operand source_operand_for(const std::uint8_t opcode) {
  // todo unify with load_source_for
  switch (opcode & 0x7) {
    case 0: return Instruction::Operand::B;
    case 1: return Instruction::Operand::C;
    case 2: return Instruction::Operand::D;
    case 3: return Instruction::Operand::E;
    case 4: return Instruction::Operand::H;
    case 5: return Instruction::Operand::L;
    case 6:
      // sometimes a constant though...
      return Instruction::Operand::HL_Indirect8;
    case 7: return Instruction::Operand::A;
    default: std::unreachable();
  }
}

constexpr std::optional<Instruction::Operand> load_source_for(const std::uint8_t opcode) {
  using Operand = Instruction::Operand;
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
  using Operand = Instruction::Operand;
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
    case Instruction::Operand::WordImmediateIndirect8:
    case Instruction::Operand::WordImmediateIndirect16:
    case Instruction::Operand::WordImmediate: return 2;
    case Instruction::Operand::ByteImmediate: return 1;
  }
}

Instruction decode(const std::array<std::uint8_t, 4> opcodes) {
  const auto opcode = opcodes[0];
  if (const auto maybe_load_dest = load_dest_for(opcode); maybe_load_dest.has_value()) {
    const auto dest = maybe_load_dest.value();
    const auto source = load_source_for(opcode);
    if (!source)
      throw std::runtime_error(std::format("Impossible sourceless load for 0x{:02x}", opcode));

    return {"ld {}, {}", static_cast<std::uint8_t>(1 + operand_length(dest) + operand_length(*source)),
        Instruction::Operation::Load, dest, *source};
  }

  const auto operands = std::span{opcodes}.subspan(1);
  switch (opcode) {
    case 0x00: return {"nop", 1, Instruction::Operation::None};
    case 0x03:
      return {
          "inc {}", 1, Instruction::Operation::Add16NoFlags, Instruction::Operand::BC, Instruction::Operand::Const_1};
    case 0x04:
      return {"inc {}", 1, Instruction::Operation::Add8, Instruction::Operand::B, Instruction::Operand::Const_1};
    case 0x10:
      return {
          "djnz {1}",
          2,
          Instruction::Operation::Djnz,
          Instruction::Operand::None,
          Instruction::Operand::PcOffset,
      };
    case 0x18:
      return {"jr {1}", 2, Instruction::Operation::Jump, Instruction::Operand::None, Instruction::Operand::PcOffset};
    case 0x20:
      return {"jr nz {1}", 2, Instruction::Operation::Jump, Instruction::Operand::None, Instruction::Operand::PcOffset,
          Instruction::Condition::NonZero};
    case 0x28:
      return {"jr z {1}", 2, Instruction::Operation::Jump, Instruction::Operand::None, Instruction::Operand::PcOffset,
          Instruction::Condition::Zero};
    case 0x30:
      return {"jr nc {1}", 2, Instruction::Operation::Jump, Instruction::Operand::None, Instruction::Operand::PcOffset,
          Instruction::Condition::NoCarry};
    case 0x38:
      return {"jr c {1}", 2, Instruction::Operation::Jump, Instruction::Operand::None, Instruction::Operand::PcOffset,
          Instruction::Condition::Carry};
    case 0x23:
      return {
          "inc {}", 1, Instruction::Operation::Add16NoFlags, Instruction::Operand::HL, Instruction::Operand::Const_1};

    // Sure would be nice to generalise these...
    case 0x05:
      return {"dec {}", 1, Instruction::Operation::Add8, Instruction::Operand::B, Instruction::Operand::Const_ffff};
    case 0x15:
      return {"dec {}", 1, Instruction::Operation::Add8, Instruction::Operand::D, Instruction::Operand::Const_ffff};
    case 0x25:
      return {"dec {}", 1, Instruction::Operation::Add8, Instruction::Operand::H, Instruction::Operand::Const_ffff};
    case 0x35:
      return {"dec {}", 1, Instruction::Operation::Add8, Instruction::Operand::HL_Indirect8,
          Instruction::Operand::Const_ffff};
    case 0x0d:
      return {"dec {}", 1, Instruction::Operation::Add8, Instruction::Operand::C, Instruction::Operand::Const_ffff};
    case 0x1d:
      return {"dec {}", 1, Instruction::Operation::Add8, Instruction::Operand::E, Instruction::Operand::Const_ffff};
    case 0x2d:
      return {"dec {}", 1, Instruction::Operation::Add8, Instruction::Operand::L, Instruction::Operand::Const_ffff};
    case 0x3d:
      return {"dec {}", 1, Instruction::Operation::Add8, Instruction::Operand::A, Instruction::Operand::Const_ffff};

    case 0x76: return {"halt", 1, Instruction::Operation::Invalid};

    case 0x09:
      return {"add {}, {}", 1, Instruction::Operation::Add16, Instruction::Operand::HL, Instruction::Operand::BC};
    case 0x19:
      return {"add {}, {}", 1, Instruction::Operation::Add16, Instruction::Operand::HL, Instruction::Operand::DE};
    case 0x29:
      return {"add {}, {}", 1, Instruction::Operation::Add16, Instruction::Operand::HL, Instruction::Operand::HL};
    case 0x39:
      return {"add {}, {}", 1, Instruction::Operation::Add16, Instruction::Operand::HL, Instruction::Operand::SP};

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
    case 0xb7: return {"or {}, {}", 1, Instruction::Operation::Or, Instruction::Operand::A, source_operand_for(opcode)};
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

    case 0xc5:
      return {"push {}", 1, Instruction::Operation::Push, Instruction::Operand::None, Instruction::Operand::BC};
    case 0xd5:
      return {"push {}", 1, Instruction::Operation::Push, Instruction::Operand::None, Instruction::Operand::DE};
    case 0xe5:
      return {"push {}", 1, Instruction::Operation::Push, Instruction::Operand::None, Instruction::Operand::HL};
    case 0xf5:
      return {"push {}", 1, Instruction::Operation::Push, Instruction::Operand::None, Instruction::Operand::AF};
    case 0xc1: return {"pop {}", 1, Instruction::Operation::Pop, Instruction::Operand::BC};
    case 0xd1: return {"pop {}", 1, Instruction::Operation::Pop, Instruction::Operand::DE};
    case 0xe1: return {"pop {}", 1, Instruction::Operation::Pop, Instruction::Operand::HL};
    case 0xf1: return {"pop {}", 1, Instruction::Operation::Pop, Instruction::Operand::AF};

    case 0x2b:
      return {"dec {}", 1, Instruction::Operation::Add16NoFlags, Instruction::Operand::HL,
          Instruction::Operand::Const_ffff};
    case 0xc3:
      return {
          "jp {1}", 3, Instruction::Operation::Jump, Instruction::Operand::None, Instruction::Operand::WordImmediate};
    case 0xd9: return {"exx", 1, Instruction::Operation::Exx}; // TODO arg this does not fit neatly into my world.
    case 0xcb: return decode_bit<RegisterSetUnshifted>(operands);
    case 0xcd:
      return {
          "call {1}", 3, Instruction::Operation::Call, Instruction::Operand::None, Instruction::Operand::WordImmediate};
    case 0xc9: return {"ret", 1, Instruction::Operation::Return};

    case 0xdd: return decode_ddfd<RegisterSetIx>(operands);
    case 0xed: return decode_ed(operands);
    case 0xd3:
      return {
          "out ({}), {}", 2, Instruction::Operation::Out, Instruction::Operand::ByteImmediate, Instruction::Operand::A};
    case 0xeb:
      return {"ex {}, {}", 1, Instruction::Operation::Exchange, Instruction::Operand::DE, Instruction::Operand::HL};
    case 0xf3: return {"di", 1, Instruction::Operation::Irq, Instruction::Operand::None, Instruction::Operand::Const_0};
    case 0xfb: return {"ei", 1, Instruction::Operation::Irq, Instruction::Operand::None, Instruction::Operand::Const_1};
    case 0xfd: return decode_ddfd<RegisterSetIy>(operands);
    default: break;
  }
  return invalid;
}

} // namespace specbolt
