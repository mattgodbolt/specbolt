module;

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "z80/common/Flags.hpp"
#include "z80/common/Z80Base.hpp"

export module spectrum:Snapshot;

import peripherals;

#include "spectrum/Snapshot.hpp"

#include "Snapshot.cpp"
