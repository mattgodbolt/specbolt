module;

#include <cstdint>
#include <functional>
#include <iostream>
#include <optional>
#include <vector>

export module z80v1:Z80;

import z80_common;
import peripherals;
import :Instruction;
import :Decoder;

#include "Z80.cpp"
