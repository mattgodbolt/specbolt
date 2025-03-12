module;

#include <cstdint>
#include <functional>
#include <iostream>
#include <optional>
#include <vector>

export module z80v2:Disassembler;

import peripherals;
import z80_common;
import :Z80Impl;

// clang-format off
#include "z80/v2/Disassembler.hpp"
#include "Disassembler.cpp"
