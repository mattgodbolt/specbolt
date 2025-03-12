module;

#include <array>
#include <bitset>
#include <cstdint>
#include <optional>

export module peripherals:Keyboard;

#define SPECBOLT_IN_MODULE
#define SPECBOLT_EXPORT export

#include "peripherals/Keyboard.hpp"

#include "Keyboard.cpp"
