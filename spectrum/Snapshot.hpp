#pragma once

#include "z80/common/Z80Base.hpp"

#include <filesystem>

namespace specbolt {

class Snapshot {
public:
  static void load(const std::filesystem::path &snapshot, Z80Base &z80);
};

} // namespace specbolt
