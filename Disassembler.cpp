#include "Disassembler.hpp"

#include <format>

#include "Opcodes.hpp"

namespace specbolt {

namespace {

uint8_t parse_length(std::string_view opcode) {
  if (opcode.contains("nnnn"))
    return 3;
  if (opcode.contains("nn"))
    return 2;
  if (opcode.contains("offset"))
    return 2;
  return 1;
}

} // namespace

Disassembler::Instruction Disassembler::disassemble(const std::uint16_t address) const {
  const auto opcode_byte = memory_[address];
  const auto opcode = Opcodes[opcode_byte];
  const auto length = parse_length(opcode);
  return {address, length,
      {opcode_byte, static_cast<std::uint8_t>(length > 1 ? memory_[(address + 1) & 0xffff] : 0),
          static_cast<std::uint8_t>(length > 2 ? memory_[(address + 2) & 0xffff] : 0)}};
}

std::string Disassembler::Instruction::to_string() const {
  // TODO prefixes
  const auto opcode = Opcodes[bytes[0]];
  std::string disassembled(opcode);
  if (const auto nn_pos = opcode.find("nn"); nn_pos != std::string_view::npos) {
    if (opcode.contains("nnnn")) {
      disassembled = std::format("{}0x{:04x}{}", opcode.substr(0, nn_pos), bytes[length - 1] << 8 | bytes[length - 2],
          opcode.substr(nn_pos + 4));
    }
    else {
      disassembled =
          std::format("{}0x{:02x}{}", opcode.substr(0, nn_pos), bytes[length - 1], opcode.substr(nn_pos + 2));
    }
  }

  switch (length) {
    // todo something clever with spans and widths, and handle prefixes...
    case 1:
      return std::format("{:04x}  {:02x}        {}", address, bytes[0], disassembled);
    case 2:
      return std::format("{:04x}  {:02x} {:02x}     {}", address, bytes[0], bytes[1], disassembled);
    case 3:
      return std::format("{:04x}  {:02x} {:02x} {:02x}  {}", address, bytes[0], bytes[1], bytes[2], disassembled);
    default:
      throw std::runtime_error("Should be impossible");
  }
}

} // namespace specbolt
