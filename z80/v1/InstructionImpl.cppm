module;

#include <cstdint>
#include <functional>
#include <iostream>
#include <optional>
#include <variant>
#include <vector>

export module z80v1:InstructionImpl;

import z80_common;
import peripherals;
import :Z80;
import :Instruction;

#include "Instruction.cpp"
