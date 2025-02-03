#include "Opcodes.hpp"

namespace specbolt {

using namespace std::literals;

constexpr Instruction invalid{"??", 1, Instruction::Operation::Invalid};

template<bool isIy>
const Instruction &decode_ddfd(const std::uint8_t opcode, [[maybe_unused]] std::uint8_t nextOpcode) {
  switch (opcode) {
    case 0x75: {
      // TODO awful, consider lefticus's approach here or something like it
#if 0
constexpr auto concat(const std::string &lhs, const std::string &rhs) {
        std::array<char, 1024> retval{}; // 0 initialize
        auto formatted_string = lhs + rhs;
        std::copy(formattted_string.begin(),
                  formatted_string.end(),
                  retval.begin()); // compile error if it overflows
        return retval;
      }

      constexpr auto do_stuff() {
        return concat("Hello", " World");
      }

      int main() {
        constexpr static auto stuff = do_stuff();
        // use const char * constructor
        constexpr auto mystring = std::string_view{stuff.data()};

        std::cout << mystring;
      }
      ///yes, your buffer is now oversized by a fair bit and that will show up in the binary.

      You have to right-size it if you care. I've done a few talks on this now
doing the constexpr 2-step
#endif
      static constexpr auto instruction = isIy ? "ld (iy+0x{:02x}), l" : "ld (ix+0x{:02x}), l";
      static constexpr Instruction ld_ind_l{instruction, 3, Instruction::Operation::Load,
          isIy ? Instruction::Operand::IY_Offset_Indirect : Instruction::Operand::IX_Offset_Indirect,
          Instruction::Operand::L};
      return ld_ind_l;
    }
    default:
      break;
  }
  return invalid;
}

const Instruction &decode_cb(const std::uint8_t opcode) {
  switch (opcode) {
    case 0x5e: {
      static constexpr Instruction instr{"bit 3, (hl)", 2, Instruction::Operation::Bit, Instruction::Operand::Const_8,
          Instruction::Operand::HL_Indirect};
      return instr;
    }
    default:
      break;
  }
  return invalid;
}

const Instruction &decode_ed(const std::uint8_t opcode) {
  switch (opcode) {
    case 0x7b: {
      static constexpr Instruction instr{"ld sp, (0x{:04x})", 4, Instruction::Operation::Load, Instruction::Operand::SP,
          Instruction::Operand::WordImmediateIndirect};
      return instr;
    }
    default:
      break;
  }
  return invalid;
}

const Instruction &decode(const std::uint8_t opcode, const std::uint8_t nextOpcode, const std::uint8_t nextNextOpcode) {
  switch (opcode) {
    case 0x00: {
      static constexpr Instruction nop{"nop", 1, Instruction::Operation::None};
      return nop;
    }
    case 0x01: {
      static constexpr Instruction ld_bc_nnnn{"ld bc, {:04x}", 3, Instruction::Operation::Load,
          Instruction::Operand::BC, Instruction::Operand::WordImmediate};
      return ld_bc_nnnn;
    }
    case 0x02: {
      static constexpr Instruction ld_bc_a{
          "ld (bc), a", 1, Instruction::Operation::Load, Instruction::Operand::BC_Indirect, Instruction::Operand::A};
      return ld_bc_a;
    }
    case 0x03: {
      static constexpr Instruction inc_bc{
          "inc bc", 1, Instruction::Operation::Add, Instruction::Operand::BC, Instruction::Operand::Const_1};
      return inc_bc;
    }
    case 0x11: {
      static constexpr Instruction instr{"ld de, 0x{:04x}", 3, Instruction::Operation::Load, Instruction::Operand::DE,
          Instruction::Operand::WordImmediate};
      return instr;
    }
    case 0x18: {
      static constexpr Instruction jr_addr{
          "jr 0x{:04x}", 2, Instruction::Operation::Jump, Instruction::Operand::None, Instruction::Operand::PcOffset};
      return jr_addr;
    }
    case 0x22: {
      static constexpr Instruction ld_nnnn_hl{"ld (0x{:04x}), hl", 3, Instruction::Operation::Load,
          Instruction::Operand::WordImmediate, Instruction::Operand::HL};
      return ld_nnnn_hl;
    }
    case 0x3e: {
      static constexpr Instruction ld_a_nn{"ld a, 0x{:02x}", 2, Instruction::Operation::Load, Instruction::Operand::A,
          Instruction::Operand::ByteImmediate};
      return ld_a_nn;
    }
    case 0xaf: {
      static constexpr Instruction instr{
          "xor a", 1, Instruction::Operation::Xor, Instruction::Operand::A, Instruction::Operand::A};
      return instr;
    }
    case 0xcb:
      return decode_cb(nextOpcode);
    case 0xdd:
      return decode_ddfd<false>(nextOpcode, nextNextOpcode);
    case 0xed:
      return decode_ed(nextOpcode);
    case 0xf3: {
      static constexpr Instruction instr{
          "di", 1, Instruction::Operation::Irq, Instruction::Operand::None, Instruction::Operand::Const_0};
      return instr;
    }
    case 0xfd:
      return decode_ddfd<true>(nextOpcode, nextNextOpcode);
    default:
      break;
  }
  return invalid;
}

} // namespace specbolt
