#pragma once

#include "z80/Instruction.hpp"

#include <array>

namespace specbolt {

Instruction decode(std::array<std::uint8_t, 4> opcodes);

} // namespace specbolt
