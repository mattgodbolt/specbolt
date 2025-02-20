#pragma once

#include "z80/Z80.hpp"

#include <filesystem>

namespace specbolt {

class Snapshot {
public:
  static void load(const std::filesystem::path &snapshot, Z80 &z80);
};

} // namespace specbolt
