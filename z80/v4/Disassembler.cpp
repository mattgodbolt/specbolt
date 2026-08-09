#ifndef SPECBOLT_MODULES
#include "z80/v4/Disassembler.hpp"
#endif

namespace specbolt::v4 {

/// TODO: the reflection-based decoder. Returns a length of zero so every
/// expectation in DisassemblerTest.cpp fails until this does something.
Disassembled disassemble(const Memory &, const std::uint16_t) { return {"<v4 unimplemented>", 0}; }

} // namespace specbolt::v4
