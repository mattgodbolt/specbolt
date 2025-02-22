#pragma once

#include <array>
#include <string_view>

namespace specbolt {

class Z80;

const std::array<std::string_view, 256> &base_opcode_names();
using Z80_Op = size_t(Z80 &);
const std::array<Z80_Op *, 256> &base_opcode_ops();

} // namespace specbolt
