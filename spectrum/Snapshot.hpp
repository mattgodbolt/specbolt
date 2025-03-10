#pragma once

#ifndef SPECBOLT_IN_MODULE
#include <filesystem>
#include "z80/Z80.hpp"
#endif

#include "module.hpp"

namespace specbolt {

SPECBOLT_EXPORT class Snapshot {
public:
  static void load(const std::filesystem::path &snapshot, Z80 &z80);
};

} // namespace specbolt
