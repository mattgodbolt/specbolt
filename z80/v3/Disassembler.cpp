#ifndef SPECBOLT_MODULES
#include "z80/v3/Disassembler.hpp"
#include "DisassembleInternal.hpp"
#include "peripherals/Memory.hpp"

#include <string>
#include <string_view>
#endif

namespace specbolt::v3 {

Disassembled disassemble(const Memory &memory, std::uint16_t address) {
  // TODO this is a dreadful way of doing this, modeled on the v2 approach. the v3 could easily decode the actual
  // disassembly directly. But I'm lazy.
  const auto initial_address = address;
  auto opcode = memory.read(address++);
  auto disassembly = std::string(impl::disassemble_base(opcode));
  std::optional<std::int8_t> offset{};
  switch (opcode) {
    case 0xcb:
      opcode = memory.read(address++);
      disassembly = std::string(impl::disassemble_cb(opcode));
      break;
    case 0xdd:
      opcode = memory.read(address++);
      if (opcode == 0xcb) {
        offset = static_cast<std::int8_t>(memory.read(address++));
        opcode = memory.read(address++);
        disassembly = std::string(impl::disassemble_ddcb(opcode));
      }
      else
        disassembly = std::string(impl::disassemble_dd(opcode));
      break;
    case 0xed:
      opcode = memory.read(address++);
      disassembly = std::string(impl::disassemble_ed(opcode));
      break;
    case 0xfd:
      opcode = memory.read(address++);
      if (opcode == 0xcb) {
        offset = static_cast<std::int8_t>(memory.read(address++));
        opcode = memory.read(address++);
        disassembly = std::string(impl::disassemble_fdcb(opcode));
      }
      else
        disassembly = std::string(impl::disassemble_fd(opcode));
      break;
    default: break;
  }
  if (const auto pos = disassembly.find("$o"); pos != std::string::npos) {
    if (!offset)
      offset = static_cast<std::int8_t>(memory.read(address++));
    disassembly = std::format("{}{}0x{:02x}{}", disassembly.substr(0, pos), *offset < 0 ? "-" : "+", std::abs(*offset),
        disassembly.substr(pos + 2));
  }
  if (const auto pos = disassembly.find("$nnnn"); pos != std::string::npos) {
    disassembly =
        std::format("{}0x{:04x}{}", disassembly.substr(0, pos), memory.read16(address), disassembly.substr(pos + 5));
    address += 2;
  }
  if (const auto pos = disassembly.find("$nn"); pos != std::string::npos) {
    disassembly =
        std::format("{}0x{:02x}{}", disassembly.substr(0, pos), memory.read(address++), disassembly.substr(pos + 3));
  }
  if (const auto pos = disassembly.find("$d"); pos != std::string::npos) {
    const auto displacement = static_cast<std::int8_t>(memory.read(address++));
    disassembly = std::format("{}0x{:02x}{}", disassembly.substr(0, pos),
        static_cast<std::uint16_t>(address + displacement), disassembly.substr(pos + 2));
  }
  return {disassembly, static_cast<std::size_t>(address - initial_address)};
}

} // namespace specbolt::v3
