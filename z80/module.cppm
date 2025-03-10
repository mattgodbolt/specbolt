module;

#include <array>
#include <cstdint>
#include <format>
#include <functional>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

export module z80;

export import peripherals;

#define SPECBOLT_IN_MODULE
#define SPECBOLT_EXPORT export

#include "Alu.hpp"
#include "Decoder.hpp"
#include "Disassembler.hpp"
#include "Flags.hpp"
#include "Generated.hpp"
#include "Instruction.hpp"
#include "RegisterFile.hpp"
#include "Z80.hpp"

#include "Alu.cpp"
#include "Decoder.cpp"
#include "Disassembler.cpp"
#include "Flags.cpp"
#include "Generated.cpp"
#include "Instruction.cpp"
#include "RegisterFile.cpp"
#include "Z80.cpp"
