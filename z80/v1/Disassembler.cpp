#include "z80/v1/Disassembler.hpp"

#ifndef SPECBOLT_MODULES
#include "peripherals/Memory.hpp"
#include "z80/v1/Decoder.hpp"
#include "z80/v1/Instruction.hpp"

#include <array>
#include <format>
#endif


namespace specbolt::v1 {

Disassembler::Disassembled Disassembler::disassemble(const std::uint16_t address) const {
  const std::array opcodes{
      memory_.read(address), memory_.read(address + 1), memory_.read(address + 2), memory_.read(address + 3)};
  return {impl::decode(opcodes), address, opcodes};
}

std::string Disassembler::Disassembled::to_string() const {
  const auto lhs = operand_name(instruction.lhs, instruction.index_offset);
  const auto rhs = operand_name(instruction.rhs, instruction.index_offset);
  const auto disassembled = std::vformat(instruction.opcode, std::make_format_args(lhs, rhs));
  switch (instruction.length) {
    case 1: return std::format("{:04x}  {:02x}           {}", address, bytes[0], disassembled);
    case 2: return std::format("{:04x}  {:02x} {:02x}        {}", address, bytes[0], bytes[1], disassembled);
    case 3:
      return std::format("{:04x}  {:02x} {:02x} {:02x}     {}", address, bytes[0], bytes[1], bytes[2], disassembled);
    case 4:
      return std::format(
          "{:04x}  {:02x} {:02x} {:02x} {:02x}  {}", address, bytes[0], bytes[1], bytes[2], bytes[3], disassembled);
    default: throw std::runtime_error("Should be impossible");
  }
}
std::string Disassembler::Disassembled::operand_name(
    const Instruction::Operand operand, const std::int8_t offset) const {
  switch (operand) {
    case Instruction::Operand::None: return "";
    case Instruction::Operand::ByteImmediate_A:
    case Instruction::Operand::ByteImmediate: return std::format("0x{:02x}", bytes[instruction.length - 1]);
    case Instruction::Operand::WordImmediate:
      return std::format(
          "0x{:04x}", static_cast<std::uint16_t>(bytes[instruction.length - 1] << 8) | bytes[instruction.length - 2]);
    case Instruction::Operand::WordImmediateIndirect8:
    case Instruction::Operand::WordImmediateIndirect16:
      return std::format(
          "(0x{:04x})", static_cast<std::uint16_t>(bytes[instruction.length - 1] << 8) | bytes[instruction.length - 2]);
    case Instruction::Operand::A: return "a";
    case Instruction::Operand::B: return "b";
    case Instruction::Operand::C: return "c";
    case Instruction::Operand::D: return "d";
    case Instruction::Operand::E: return "e";
    case Instruction::Operand::H: return "h";
    case Instruction::Operand::L: return "l";
    case Instruction::Operand::I: return "i";
    case Instruction::Operand::R: return "r";
    case Instruction::Operand::AF: return "af";
    case Instruction::Operand::BC: return "bc";
    case Instruction::Operand::DE: return "de";
    case Instruction::Operand::HL: return "hl";
    case Instruction::Operand::AF_: return "af'";
    case Instruction::Operand::IX: return "ix";
    case Instruction::Operand::IY: return "iy";
    case Instruction::Operand::IXH: return "ixh";
    case Instruction::Operand::IYH: return "iyh";
    case Instruction::Operand::IXL: return "ixl";
    case Instruction::Operand::IYL: return "iyl";
    case Instruction::Operand::BC_Indirect8: return "(bc)";
    case Instruction::Operand::DE_Indirect8: return "(de)";
    case Instruction::Operand::HL_Indirect16:
    case Instruction::Operand::HL_Indirect8: return "(hl)";
    case Instruction::Operand::SP: return "sp";
    case Instruction::Operand::SP_Indirect16: return "(sp)";
    case Instruction::Operand::PcOffset:
      return std::format(
          "0x{:04x}", address + instruction.length + static_cast<std::int8_t>(bytes[instruction.length - 1]));
    case Instruction::Operand::IX_Offset_Indirect8:
    case Instruction::Operand::IY_Offset_Indirect8: {
      return std::format("({}{}0x{:02x})", operand == Instruction::Operand::IX_Offset_Indirect8 ? "ix" : "iy",
          offset < 0 ? "-" : "+", offset < 0 ? -offset : offset);
    }
    case Instruction::Operand::Const_0: return "0";
    case Instruction::Operand::Const_1: return "1";
    case Instruction::Operand::Const_2: return "2";
    case Instruction::Operand::Const_3: return "3";
    case Instruction::Operand::Const_4: return "4";
    case Instruction::Operand::Const_5: return "5";
    case Instruction::Operand::Const_6: return "6";
    case Instruction::Operand::Const_7: return "7";
    case Instruction::Operand::Const_8: return "8";
    case Instruction::Operand::Const_16: return "16";
    case Instruction::Operand::Const_24: return "24";
    case Instruction::Operand::Const_32: return "32";
    case Instruction::Operand::Const_40: return "40";
    case Instruction::Operand::Const_48: return "48";
    case Instruction::Operand::Const_52: return "52";
    case Instruction::Operand::Const_ffff: return "0xffff";
  }
  return "??";
}

} // namespace specbolt::v1
