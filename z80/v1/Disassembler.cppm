module;

import std;

export module z80_v1:Disassembler;

import peripherals;
import z80_common;
import :Instruction;
import :Decoder;

#include "Disassembler.cpp"
