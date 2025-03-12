module;

#include <array>
#include <cstdint>
#include <iostream>
#include <string_view>

export module z80_common:RegisterFile;

import :Flags;

#include "z80/common/RegisterFile.hpp"

#include "RegisterFile.cpp"
