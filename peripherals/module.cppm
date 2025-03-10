module;

#include <algorithm>
#include <array>
#include <bitset>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <ranges>
#include <span>
#include <type_traits>
#include <vector>

export module peripherals;

#define SPECBOLT_EXPORT export
#define SPECBOLT_IN_MODULE

#include "Audio.hpp"
#include "Keyboard.hpp"
#include "Memory.hpp"
#include "Video.hpp"

#include "Audio.cpp"
#include "Keyboard.cpp"
#include "Memory.cpp"
#include "Video.cpp"
