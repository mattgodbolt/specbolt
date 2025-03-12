module;

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <type_traits>

import peripherals:Memory;

export module peripherals:Video;

#define SPECBOLT_IN_MODULE
#define SPECBOLT_EXPORT export

#include "peripherals/Video.hpp"

#include "Video.cpp"
