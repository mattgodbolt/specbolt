#include "Disassembler.hpp"

#include "Instruction.hpp"
#include "Memory.hpp"
#include "Opcodes.hpp"

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
  const auto arg = immediate_operand();
  const auto disassembled = std::vformat(instruction.opcode, std::make_format_args(arg));
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

std::uint16_t Disassembler::Disassembled::immediate_operand() const {
  if (instruction.source == Instruction::Operand::WordImmediate ||
      instruction.dest == Instruction::Operand::WordImmediate ||
      instruction.source == Instruction::Operand::WordImmediateIndirect ||
      instruction.dest == Instruction::Operand::WordImmediateIndirect) {
    return static_cast<uint16_t>(bytes[instruction.length - 1] << 8) | bytes[instruction.length - 2];
  }
  if (instruction.source == Instruction::Operand::ByteImmediate ||
      instruction.dest == Instruction::Operand::ByteImmediate ||
      instruction.source == Instruction::Operand::IX_Offset_Indirect ||
      instruction.dest == Instruction::Operand::IX_Offset_Indirect ||
      instruction.source == Instruction::Operand::IY_Offset_Indirect ||
      instruction.dest == Instruction::Operand::IY_Offset_Indirect) {
    return bytes[instruction.length - 1];
  }
  if (instruction.source == Instruction::Operand::PcOffset) {
    return static_cast<uint16_t>(address + instruction.length + static_cast<int8_t>(bytes[instruction.length - 1]));
  }
  return 0;
}

} // namespace specbolt
