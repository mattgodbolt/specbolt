module;

#include <cstdint>
#include <functional>
#include <iostream>
#include <optional>
#include <vector>

export module z80_v3:Z80;

import peripherals;
import z80_common;
import :Z80Impl;

#include "Z80.cpp"
