module;

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <type_traits>

export module peripherals:Memory;

#define SPECBOLT_IN_MODULE
#define SPECBOLT_EXPORT export

#include "peripherals/Memory.hpp"

#include "Memory.cpp"
