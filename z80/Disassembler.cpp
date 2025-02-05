#include "z80/Disassembler.hpp"

#include "z80/Instruction.hpp"
#include "peripherals/Memory.hpp"
#include "z80/Opcodes.hpp"

#include <format>

namespace specbolt {

Disassembler::Disassembled Disassembler::disassemble(const std::uint16_t address) const {
  const auto first = memory_.read(address);
  const auto second = memory_.read(address + 1);
  const auto third = memory_.read(address + 2);
  const auto &instruction = decode(first, second, third);
  return {instruction, address, {first, second, third, memory_.read(address + 3)}};
}

std::string Disassembler::Disassembled::to_string() const {
  const auto dest = operand_name(instruction.dest);
  const auto source = operand_name(instruction.source);
  const auto disassembled = std::vformat(instruction.opcode, std::make_format_args(dest, source));
  switch (instruction.length) {
    // todo something clever with spans and widths, and handle prefixes...
    case 1:
      return std::format("{:04x}  {:02x}           {}", address, bytes[0], disassembled);
    case 2:
      return std::format("{:04x}  {:02x} {:02x}        {}", address, bytes[0], bytes[1], disassembled);
    case 3:
      return std::format("{:04x}  {:02x} {:02x} {:02x}     {}", address, bytes[0], bytes[1], bytes[2], disassembled);
    case 4:
      return std::format(
          "{:04x}  {:02x} {:02x} {:02x} {:02x}  {}", address, bytes[0], bytes[1], bytes[2], bytes[3], disassembled);
    default:
      throw std::runtime_error("Should be impossible");
  }
}
std::string Disassembler::Disassembled::operand_name(Instruction::Operand operand) const {
  switch (operand) {
    case Instruction::Operand::None:
      return "";
    case Instruction::Operand::ByteImmediate:
      return std::format("0x{:02x}", bytes[instruction.length - 1]);
    case Instruction::Operand::WordImmediate:
      return std::format(
          "0x{:04x}", static_cast<uint16_t>(bytes[instruction.length - 1] << 8) | bytes[instruction.length - 2]);
    case Instruction::Operand::WordImmediateIndirect:
      return std::format(
          "(0x{:04x})", static_cast<uint16_t>(bytes[instruction.length - 1] << 8) | bytes[instruction.length - 2]);
    case Instruction::Operand::A:
      return "a";
    case Instruction::Operand::B:
      return "b";
    case Instruction::Operand::C:
      return "c";
    case Instruction::Operand::D:
      return "d";
    case Instruction::Operand::E:
      return "e";
    case Instruction::Operand::H:
      return "h";
    case Instruction::Operand::L:
      return "l";
    case Instruction::Operand::I:
      return "i";
    case Instruction::Operand::AF:
      return "af";
    case Instruction::Operand::BC:
      return "bc";
    case Instruction::Operand::DE:
      return "de";
    case Instruction::Operand::HL:
      return "hl";
    case Instruction::Operand::AF_:
      return "af'";
    case Instruction::Operand::IX:
      return "ix";
    case Instruction::Operand::IY:
      return "iy";
    case Instruction::Operand::BC_Indirect:
      return "(bc)";
    case Instruction::Operand::DE_Indirect:
      return "(de)";
    case Instruction::Operand::HL_Indirect:
      return "(hl)";
    case Instruction::Operand::SP:
      return "sp";
    case Instruction::Operand::PcOffset:
      return std::format("0x{:04x}", address + instruction.length + static_cast<int8_t>(bytes[instruction.length - 1]));
    case Instruction::Operand::IX_Offset_Indirect:
      return std::format("(ix+0x{:02x})", bytes[instruction.length - 1]);
    case Instruction::Operand::IY_Offset_Indirect:
      return std::format("(iy+0x{:02x})", bytes[instruction.length - 1]);
    case Instruction::Operand::Const_0:
      return "0";
    case Instruction::Operand::Const_1:
      return "1";
    case Instruction::Operand::Const_2:
      return "2";
    case Instruction::Operand::Const_4:
      return "4";
    case Instruction::Operand::Const_8:
      return "8";
    case Instruction::Operand::Const_16:
      return "16";
    case Instruction::Operand::Const_32:
      return "32";
    case Instruction::Operand::Const_64:
      return "64";
    case Instruction::Operand::Const_128:
      return "128";
  }
  return "??";
}


// std::uint16_t Disassembler::Disassembled::immediate_operand() const {
//   if (instruction.source == Instruction::Operand::WordImmediate ||
//       instruction.dest == Instruction::Operand::WordImmediate ||
//       instruction.source == Instruction::Operand::WordImmediateIndirect ||
//       instruction.dest == Instruction::Operand::WordImmediateIndirect) {
//     return static_cast<uint16_t>(bytes[instruction.length - 1] << 8) | bytes[instruction.length - 2];
//   }
//   if (instruction.source == Instruction::Operand::ByteImmediate ||
//       instruction.dest == Instruction::Operand::ByteImmediate ||
//       instruction.source == Instruction::Operand::IX_Offset_Indirect ||
//       instruction.dest == Instruction::Operand::IX_Offset_Indirect ||
//       instruction.source == Instruction::Operand::IY_Offset_Indirect ||
//       instruction.dest == Instruction::Operand::IY_Offset_Indirect) {
//     return bytes[instruction.length - 1];
//   }
//   if (instruction.source == Instruction::Operand::PcOffset) {
//     return static_cast<uint16_t>(address + instruction.length + static_cast<int8_t>(bytes[instruction.length - 1]));
//   }
//   return 0;
// }

} // namespace specbolt
