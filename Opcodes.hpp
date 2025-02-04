#pragma once

#include "Instruction.hpp"

namespace specbolt {

Instruction decode(std::uint8_t opcode, std::uint8_t nextOpcode, std::uint8_t nextNextOpcode);

} // namespace specbolt
