module;

import std;

export module z80_v1:Z80;

import z80_common;
import peripherals;
import :Instruction;
import :Decoder;

#include "Z80.cpp"
