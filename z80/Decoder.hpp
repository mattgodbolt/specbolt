#pragma once

#ifndef SPECBOLT_IN_MODULE
#include <array>
#endif

#include "Instruction.hpp"

namespace specbolt {

Instruction decode(std::array<std::uint8_t, 4> opcodes);

} // namespace specbolt
