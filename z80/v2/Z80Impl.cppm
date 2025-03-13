module;

#include <cstdint>
#include <functional>
#include <iostream>
#include <optional>
#include <vector>

export module z80_v2:Z80Impl;

import peripherals;
import z80_common;
import :Mnemonic;

// clang-format off
#include "z80/v2/Z80.hpp"
#include "Z80Impl.hpp"
#include "Z80Impl.cpp"
