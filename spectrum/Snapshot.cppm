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


export module spectrum:Snapshot;

import peripherals;
import z80_common;

#include "spectrum/Snapshot.hpp"

#include "Snapshot.cpp"
