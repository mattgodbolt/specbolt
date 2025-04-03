#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <SDL.h>

namespace specbolt {

// A class to track memory access patterns and visualize them as a heatmap
class MemoryHeatmap {
public:
  enum class Mode {
    ReadOnly, // Track only read operations
    WriteOnly, // Track only write operations
    ReadWrite, // Track both read and write operations
    Disabled // Don't track anything
  };

  enum class ColourScheme {
    Heat, // Blue (cold) to Red (hot)
    Spectrum, // Use ZX Spectrum colours
    Grayscale // Black to White
  };

  explicit MemoryHeatmap();
  ~MemoryHeatmap() = default;

  // Reset all access counts
  void reset();

  // Record a memory access
  void record_read(std::uint16_t address);
  void record_write(std::uint16_t address);

  // Visualization mode control
  void set_mode(const Mode mode) { mode_ = mode; }
  [[nodiscard]] Mode mode() const { return mode_; }

  void set_colour_scheme(const ColourScheme scheme) { colour_scheme_ = scheme; }
  [[nodiscard]] ColourScheme colour_scheme() const { return colour_scheme_; }

  // Configure heatmap appearance
  void set_opacity(const float opacity) { opacity_ = opacity; }
  [[nodiscard]] float opacity() const { return opacity_; }

  void set_decay_rate(const float rate) { decay_rate_ = rate; }
  [[nodiscard]] float decay_rate() const { return decay_rate_; }

  // Apply decay to all counters, called periodically
  void decay();

  // Render the heatmap as an overlay
  void render(SDL_Renderer *renderer, const SDL_Rect &dest_rect);

  // Get the current pixel buffer
  [[nodiscard]] const std::vector<std::uint32_t> &pixels() const { return texture_pixels_; }

private:
  // The ZX Spectrum has 64K of addressable memory
  static constexpr std::size_t MEMORY_SIZE = 64 * 1024;

  // Access counters for each memory address
  std::array<float, MEMORY_SIZE> read_counts_{};
  std::array<float, MEMORY_SIZE> write_counts_{};

  // Current mode and settings
  Mode mode_ = Mode::ReadWrite;
  ColourScheme colour_scheme_ = ColourScheme::Heat;
  float opacity_ = 0.7f;
  float decay_rate_ = 0.95f;

  // Mapping memory to visual representation
  static constexpr int HEATMAP_WIDTH = 256; // 16 addresses per pixel horizontally
  static constexpr int HEATMAP_HEIGHT = 256; // 16 addresses per pixel vertically

  // Texture and pixels for rendering
  SDL_Texture *texture_ = nullptr;
  std::vector<std::uint32_t> texture_pixels_;

  // Helper methods for visualization
  void update_texture(SDL_Renderer *renderer);
  [[nodiscard]] std::uint32_t get_colour_for_value(float value) const;
};

} // namespace specbolt
