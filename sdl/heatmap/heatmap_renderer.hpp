#pragma once

#include "heatmap_memory_listener.hpp"
#include "memory_heatmap.hpp"
#ifndef SPECBOLT_MODULES
#include "peripherals/Memory.hpp"
#else
import peripherals;
#endif

#include <SDL.h>
#include <chrono>

namespace specbolt {

// Class that connects the memory access tracing to the heatmap visualization
class HeatmapRenderer {
public:
  // Constructor now connects to memory tracing automatically
  explicit HeatmapRenderer(Memory &memory);
  ~HeatmapRenderer();

  // Setup connections to memory tracing
  void connect(Memory &memory);
  void disconnect();

  // Toggle heatmap visibility and modes
  void toggle_heatmap();
  void toggle_mode();
  void toggle_colour_scheme();

  // Configure appearance
  void increase_opacity();
  void decrease_opacity();

  // Process keyboard input for controlling the heatmap
  bool process_key(SDL_Keycode key);

  // Render the heatmap overlay
  void render(SDL_Renderer *renderer, const SDL_Rect &dest_rect);

  // Reset heatmap data
  void reset();

  // Periodic update (decay, etc)
  void update();

private:
  // The actual heatmap instance
  MemoryHeatmap heatmap_;

  // Memory listener to capture memory accesses (owned by this class)
  HeatmapMemoryListener memory_listener_;

  // Last update time for decay operations
  std::chrono::time_point<std::chrono::high_resolution_clock> last_decay_time_;

  // Whether the heatmap is currently visible
  bool visible_ = false;
};

} // namespace specbolt
