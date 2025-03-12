module;

#include <cstdint>
#include <functional>
#include <iostream>
#include <optional>
#include <vector>

export module z80_common:Z80Base;

import peripherals;
import :Flags;
import :RegisterFile;

#include "z80/common/Z80Base.hpp"

#include "Z80Base.cpp"
