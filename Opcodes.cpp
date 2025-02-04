#include "Opcodes.hpp"

#include <stdexcept>
#include <utility>

namespace specbolt {

using namespace std::literals;

constexpr Instruction invalid{"??", 1, Instruction::Operation::Invalid};
constexpr Instruction invalid_2{"??", 2, Instruction::Operation::Invalid};

template<bool isIy>
Instruction decode_ddfd(const std::uint8_t opcode, [[maybe_unused]] std::uint8_t nextOpcode) {
  switch (opcode) {
    case 0x75:
      return {"ld {}, {}", 3, Instruction::Operation::Load,
          isIy ? Instruction::Operand::IY_Offset_Indirect : Instruction::Operand::IX_Offset_Indirect,
          Instruction::Operand::L};
    default:
      break;
  }
  return invalid_2;
}

Instruction decode_cb(const std::uint8_t opcode) {
  switch (opcode) {
    case 0x5e:
      // TODO is this a good way to bit?
      return {"bit 3, {1}", 2, Instruction::Operation::Bit, Instruction::Operand::Const_8,
          Instruction::Operand::HL_Indirect};
    default:
      break;
  }
  return invalid_2;
}

const Instruction &decode_ed(const std::uint8_t opcode) {
  switch (opcode) {
    case 0x47: {
      static constexpr Instruction instr{
          "ld {}, {}", 2, Instruction::Operation::Load, Instruction::Operand::I, Instruction::Operand::A};
      return instr;
    }
    case 0x7b: {
      static constexpr Instruction instr{"ld {}, {}", 4, Instruction::Operation::Load, Instruction::Operand::SP,
          Instruction::Operand::WordImmediateIndirect};
      return instr;
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

Instruction decode(const std::uint8_t opcode, const std::uint8_t nextOpcode, const std::uint8_t nextNextOpcode) {
  switch (opcode) {
    case 0x00:
      return {"nop", 1, Instruction::Operation::None};
    case 0x01:
      return {
          "ld {}, {}", 3, Instruction::Operation::Load, Instruction::Operand::BC, Instruction::Operand::WordImmediate};
    case 0x02:
      return {"ld {}, {}", 1, Instruction::Operation::Load, Instruction::Operand::BC_Indirect, Instruction::Operand::A};
    case 0x03:
      // TODO: inc dec 16 doesn't affect flags, but this will if we're not careful...maybe add16/sub16 with const 1 is
      // dumb
      return {"inc {}", 1, Instruction::Operation::Add16, Instruction::Operand::BC, Instruction::Operand::Const_1};
    case 0x11:
      return {
          "ld {}, {}", 3, Instruction::Operation::Load, Instruction::Operand::DE, Instruction::Operand::WordImmediate};
    case 0x18:
      return {"jr {1}", 2, Instruction::Operation::Jump, Instruction::Operand::None, Instruction::Operand::PcOffset};
    case 0x22:
      return {"ld {}, {}", 3, Instruction::Operation::Load, Instruction::Operand::WordImmediateIndirect,
          Instruction::Operand::HL};
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
      return {
          "adc {}, {}", 1, Instruction::Operation::Add8WithCarry, Instruction::Operand::A, source_operand_for(opcode)};

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
      return {"sbc {}, {}", 1, Instruction::Operation::Subtract8WithCarry, Instruction::Operand::A,
          source_operand_for(opcode)};

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
      return {"cp {1}", 1, Instruction::Operation::Compare, Instruction::Operand::None, source_operand_for(opcode)};

    case 0x2b:
      return {"dec {}", 1, Instruction::Operation::Subtract16, Instruction::Operand::HL, Instruction::Operand::Const_1};
    case 0xc3:
      return {
          "jp {1}", 3, Instruction::Operation::Jump, Instruction::Operand::None, Instruction::Operand::WordImmediate};
    case 0xcb:
      return decode_cb(nextOpcode);
    case 0xdd:
      return decode_ddfd<false>(nextOpcode, nextNextOpcode);
    case 0xed:
      return decode_ed(nextOpcode);
    case 0xd3:
      return {
          "out ({}), {}", 2, Instruction::Operation::Out, Instruction::Operand::ByteImmediate, Instruction::Operand::A};
    case 0xf3:
      return {"di", 1, Instruction::Operation::Irq, Instruction::Operand::None, Instruction::Operand::Const_0};
    case 0xfd:
      return decode_ddfd<true>(nextOpcode, nextNextOpcode);
    default:
      break;
  }
  return invalid;
}

} // namespace specbolt
