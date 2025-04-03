#pragma once

#include "memory_heatmap.hpp"
#ifndef SPECBOLT_MODULES
#include "peripherals/Memory.hpp"
#else
import peripherals;
#endif

namespace specbolt {

// Implements the Memory::Listener interface to connect the heatmap to memory access events
class HeatmapMemoryListener final : public Memory::Listener {
public:
  explicit HeatmapMemoryListener(MemoryHeatmap &heatmap) : heatmap_(heatmap) {}
  ~HeatmapMemoryListener() override = default;

  // Called when memory is read
  void on_memory_read(const std::uint16_t address) override { heatmap_.record_read(address); }

  // Called when memory is written
  void on_memory_write(const std::uint16_t address) override { heatmap_.record_write(address); }

private:
  MemoryHeatmap &heatmap_;
};

} // namespace specbolt
