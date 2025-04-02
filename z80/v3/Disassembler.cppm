module;

#include <cstdint>
#include <functional>
#include <iostream>
#include <optional>
#include <vector>

export module z80_v3:Disassembler;

import peripherals;
import z80_common;

#include "DisassembleInternal.hpp"
#include "z80/v3/Disassembler.hpp"

#include "Disassembler.cpp"
