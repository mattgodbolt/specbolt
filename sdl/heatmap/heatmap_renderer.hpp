#pragma once

#include "memory_heatmap.hpp"
#include "memory_hooks.hpp"

#include <SDL.h>
#include <chrono>
#include <memory>

namespace specbolt {

// Class that connects the memory access tracing to the heatmap visualization
class HeatmapRenderer {
public:
  // Constructor now connects to memory tracing automatically
  HeatmapRenderer();
  ~HeatmapRenderer();

  // Setup connections to memory tracing - now called from constructor
  void connect();
  void disconnect();

  // Toggle heatmap visibility and modes
  void toggle_heatmap();
  void toggle_mode();
  void toggle_color_scheme();

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

  // Last update time for decay operations
  std::chrono::time_point<std::chrono::high_resolution_clock> last_decay_time_;

  // Callbacks for memory access
  void on_memory_read(std::uint16_t address);
  void on_memory_write(std::uint16_t address);

  // Whether the heatmap is currently visible
  bool visible_ = false;
};

} // namespace specbolt
