#pragma once

#ifndef SPECBOLT_MODULES
#include "z80/common/Z80Base.hpp"

#include <filesystem>
#endif

namespace specbolt {

SPECBOLT_EXPORT
class Snapshot {
public:
  static void load(const std::filesystem::path &snapshot, Z80Base &z80);
};

} // namespace specbolt
