#pragma once

#include "z80/v1/Instruction.hpp"

#include <array>

namespace specbolt::v1 {

Instruction decode(std::array<std::uint8_t, 4> opcodes);

} // namespace specbolt::v1
