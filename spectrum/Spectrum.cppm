module;

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <span>
#include <type_traits>

#include "z80/common/Flags.hpp"
#include "z80/common/RegisterFile.hpp"

export module spectrum:Spectrum;

import peripherals;

#include "spectrum/Spectrum.hpp"
