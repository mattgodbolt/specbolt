#pragma once

#ifndef SPECBOLT_MODULES
#include <cstdint>
#include <string_view>
#endif

namespace specbolt::v3::impl {

std::string_view disassemble_base(std::uint8_t opcode);
std::string_view disassemble_cb(std::uint8_t opcode);
std::string_view disassemble_dd(std::uint8_t opcode);
std::string_view disassemble_ed(std::uint8_t opcode);
std::string_view disassemble_fd(std::uint8_t opcode);
std::string_view disassemble_ddcb(std::uint8_t opcode);
std::string_view disassemble_fdcb(std::uint8_t opcode);

} // namespace specbolt::v3::impl
