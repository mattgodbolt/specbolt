#pragma once

#include <array>
#include <string_view>

#include "Instruction.hpp"

namespace specbolt {

const Instruction &decode(std::uint8_t opcode, std::uint8_t nextOpcode, std::uint8_t nextNextOpcode);

} // namespace specbolt
