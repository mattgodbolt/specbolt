#pragma once

#include <cstdint>
#include <string_view>

namespace specbolt::v3::impl {

std::string_view disassemble_base(std::uint8_t opcode);

}
