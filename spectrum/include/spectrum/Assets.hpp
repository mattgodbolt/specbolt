#pragma once

#ifndef SPECBOLT_MODULES
#include <filesystem>
#endif

namespace specbolt {
SPECBOLT_EXPORT
std::filesystem::path get_asset_dir();
}; // namespace specbolt
