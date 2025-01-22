#include "Disassembler.hpp"

#include "Memory.hpp"
#include "Opcodes.hpp"

#include <format>

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
  const auto first_byte = memory_.read(address);
  if (first_byte == 0xdd || first_byte == 0xfd || first_byte == 0xed || first_byte == 0xcb) {
    const auto second_byte = memory_.read(address + 1);
    const auto opcode = first_byte == 0xdd   ? GetOpcodeDD(second_byte)
                        : first_byte == 0xfd ? GetOpcodeFD(second_byte)
                        : first_byte == 0xed ? GetOpcodeED(second_byte)
                                             : OpcodesCB[second_byte];
    const auto length = static_cast<uint8_t>(parse_length(opcode) + 1);
    return {address, length,
        {first_byte, second_byte, static_cast<std::uint8_t>(length > 2 ? memory_.read(address + 2) : 0),
            static_cast<std::uint8_t>(length > 3 ? memory_.read(address + 3) : 0)}};
  }
  const auto opcode = Opcodes[first_byte];
  const auto length = parse_length(opcode);
  return {address, length,
      {first_byte, static_cast<std::uint8_t>(length > 1 ? memory_.read(address + 1) : 0),
          static_cast<std::uint8_t>(length > 2 ? memory_.read(address + 2) : 0)}};
}

std::string Disassembler::Instruction::to_string() const {
  std::string_view opcode;
  switch (bytes[0]) {
    case 0xed:
      opcode = GetOpcodeED(bytes[1]);
      break;
    case 0xdd:
      opcode = GetOpcodeDD(bytes[1]);
      break;
    case 0xfd:
      opcode = GetOpcodeFD(bytes[1]);
      break;
    case 0xcb:
      opcode = OpcodesCB[bytes[1]];
      break;
    default:
      opcode = Opcodes[bytes[0]];
      break;
  }
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
  else if (const auto offset_pos = opcode.find("offset"); offset_pos != std::string_view::npos) {
    const auto dest = static_cast<std::uint16_t>(address + length + static_cast<std::int8_t>(bytes[length - 1]));
    disassembled = std::format("{}0x{:04x}{}", opcode.substr(0, offset_pos), dest, opcode.substr(offset_pos + 6));
  }

  switch (length) {
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

} // namespace specbolt
