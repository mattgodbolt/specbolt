#pragma once

#include "memory_heatmap.hpp"
#include "peripherals/Memory.hpp"

namespace specbolt {

// Implements the MemoryListener interface to connect the heatmap to memory access events
class HeatmapMemoryListener : public MemoryListener {
public:
  explicit HeatmapMemoryListener(MemoryHeatmap &heatmap) : heatmap_(heatmap) {}
  ~HeatmapMemoryListener() override = default;

  // Called when memory is read
  void on_memory_read(std::uint16_t address) override { heatmap_.record_read(address); }

  // Called when memory is written
  void on_memory_write(std::uint16_t address) override { heatmap_.record_write(address); }

private:
  MemoryHeatmap &heatmap_;
};

} // namespace specbolt
