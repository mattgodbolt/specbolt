#pragma once

#ifndef SPECBOLT_MODULES
#include "z80/common/Z80Base.hpp"

#include <filesystem>
#endif

namespace specbolt {

export class Snapshot {
public:
  static void load_sna(const std::filesystem::path &snapshot, Z80Base &z80);
  static void load_z80(const std::filesystem::path &snapshot, Z80Base &z80);
  static void load(const std::filesystem::path &snapshot, Z80Base &z80);
};

} // namespace specbolt
