#pragma once

#include <array>
#include <string_view>

namespace specbolt {

class Z80;

const std::array<std::string_view, 256> &base_opcode_names();
using Z80_Op = void(Z80 &);

void decode_and_run(Z80 &z80);

} // namespace specbolt
