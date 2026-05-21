#pragma once

// The v4 instruction set lives as data: one entry per opcode, with the
// behaviour expressed as a stored token sequence. At class-injection time
// the dispatch function is synthesised by splicing each body into a switch
// case. This is the floooh-style "table is the source of truth" design,
// expressed inside C++ rather than in a separate codegen tool.

#include <array>
#include <cstdint>
#include <meta>
#include <string_view>

namespace specbolt::v4 {

struct InsnDef {
  std::uint8_t opcode;
  std::string_view mnemonic;
  std::meta::token_sequence body;
};

// Tiny initial set: enough to demonstrate the mechanism and run a couple
// of opcodes against the existing harness. Real coverage is the next step.
inline constexpr auto base_instructions = std::array{
    InsnDef{0x00, "nop", ^^{}},
    InsnDef{0x76, "halt", ^^{ halt(); }},
    InsnDef{0xf3, "di", ^^{ iff1(false); iff2(false); }},
    InsnDef{0xfb, "ei", ^^{ iff1(true);  iff2(true); }},
    InsnDef{0xc3, "jp $nnnn", ^^{ regs().pc(read_immediate16()); }},
};

} // namespace specbolt::v4
