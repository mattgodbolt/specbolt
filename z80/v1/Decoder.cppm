module;

#include <cstdint>
#include <functional>
#include <iostream>
#include <optional>
#include <variant>
#include <vector>

export module z80_v1:Decoder;

import z80_common;
import :Instruction;

#include "Decoder.cpp"
