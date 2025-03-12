#pragma once

#ifndef SPECBOLT_MODULES
#include <array>
#include "z80/v1/Instruction.hpp"
#endif

namespace specbolt::v1::impl {

Instruction decode(std::array<std::uint8_t, 4> opcodes);

} // namespace specbolt::v1::impl
