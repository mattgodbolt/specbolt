#include "memory_heatmap.hpp"

#include <algorithm>
#include <cmath>
#include <print>

namespace specbolt {

MemoryHeatmap::MemoryHeatmap() {
  // Initialize the pixel buffer
  texture_pixels_.resize(HEATMAP_WIDTH * HEATMAP_HEIGHT, 0);
}

void MemoryHeatmap::reset() {
  // Reset all counters to zero
  read_counts_.fill(0.0f);
  write_counts_.fill(0.0f);
}

void MemoryHeatmap::record_read(const std::uint16_t address) {
  if (mode_ == Mode::ReadOnly || mode_ == Mode::ReadWrite) {
    read_counts_[address] += 1.0f;
  }
}

void MemoryHeatmap::record_write(const std::uint16_t address) {
  if (mode_ == Mode::WriteOnly || mode_ == Mode::ReadWrite) {
    write_counts_[address] += 1.0f;
  }
}

void MemoryHeatmap::decay() {
  // Apply decay to all counters
  if (decay_rate_ < 1.0f) {
    for (auto &count: read_counts_) {
      count *= decay_rate_;
    }
    for (auto &count: write_counts_) {
      count *= decay_rate_;
    }
  }
}

std::uint32_t MemoryHeatmap::get_color_for_value(float value) const {
  // Clamp value between 0 and 1
  value = std::clamp(value, 0.0f, 1.0f);

  // Apply opacity
  const auto alpha = static_cast<std::uint32_t>(opacity_ * 255);

  switch (color_scheme_) {
    case ColorScheme::Heat: {
      // Blue (cold) to Red (hot) color scheme
      std::uint32_t r, g, b;
      if (value < 0.5f) {
        // Blue to Purple: increasing red as we get hotter
        r = static_cast<std::uint32_t>(value * 2 * 255);
        g = 0;
        b = 255;
      }
      else {
        // Purple to Red: decreasing blue as we get hotter
        r = 255;
        g = 0;
        b = static_cast<std::uint32_t>((1.0f - (value - 0.5f) * 2) * 255);
      }
      return (alpha << 24) | (r << 16) | (g << 8) | b;
    }

    case ColorScheme::Spectrum: {
      // Use ZX Spectrum colors
      const std::array<std::uint32_t, 8> spectrum_colors{
          0x000000, // Black
          0x0000D7, // Blue
          0xD70000, // Red
          0xD700D7, // Magenta
          0x00D700, // Green
          0x00D7D7, // Cyan
          0xD7D700, // Yellow
          0xD7D7D7 // White
      };

      // Map the value to one of the 8 colors
      const auto index = static_cast<std::size_t>(value * (spectrum_colors.size() - 1));
      std::uint32_t color = spectrum_colors[index];

      // Add alpha
      return (alpha << 24) | color;
    }

    case ColorScheme::Grayscale:
    default: {
      // Black to White
      const auto intensity = static_cast<std::uint32_t>(value * 255);
      return (alpha << 24) | (intensity << 16) | (intensity << 8) | intensity;
    }
  }
}

void MemoryHeatmap::update_texture(SDL_Renderer *renderer) {
  // Find the maximum count for normalization
  float max_count = 0.1f; // Small epsilon to avoid division by zero

  // Calculate the maximum based on current mode
  if (mode_ == Mode::ReadOnly) {
    max_count = *std::max_element(read_counts_.begin(), read_counts_.end());
  }
  else if (mode_ == Mode::WriteOnly) {
    max_count = *std::max_element(write_counts_.begin(), write_counts_.end());
  }
  else if (mode_ == Mode::ReadWrite) {
    const auto max_read = *std::max_element(read_counts_.begin(), read_counts_.end());
    const auto max_write = *std::max_element(write_counts_.begin(), write_counts_.end());
    max_count = std::max(max_read, max_write);
  }

  // Ensure we have a valid maximum
  max_count = std::max(max_count, 0.1f);

  // Update the pixel buffer
  for (std::size_t address = 0; address < MEMORY_SIZE; address++) {
    // Calculate the pixel position
    const std::size_t x = address & 0xFF; // Low byte determines X
    const std::size_t y = address >> 8; // High byte determines Y

    // Get the normalized value based on the current mode
    float value = 0.0f;
    if (mode_ == Mode::ReadOnly) {
      value = read_counts_[address] / max_count;
    }
    else if (mode_ == Mode::WriteOnly) {
      value = write_counts_[address] / max_count;
    }
    else if (mode_ == Mode::ReadWrite) {
      value = (read_counts_[address] + write_counts_[address]) / max_count;
    }

    // Apply logarithmic scaling to better visualize the range
    value = std::log1p(value * 9.0f) / std::log(10.0f);

    // Set the pixel color
    texture_pixels_[y * HEATMAP_WIDTH + x] = get_color_for_value(value);
  }

  // Create or recreate the texture if needed
  if (texture_ == nullptr) {
    texture_ = SDL_CreateTexture(
        renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, HEATMAP_WIDTH, HEATMAP_HEIGHT);
    if (texture_ == nullptr) {
      std::println(stderr, "Failed to create heatmap texture: {}", SDL_GetError());
      return;
    }

    // Enable alpha blending
    SDL_SetTextureBlendMode(texture_, SDL_BLENDMODE_BLEND);
  }

  // Update the texture with the new pixel data
  SDL_UpdateTexture(texture_, nullptr, texture_pixels_.data(), HEATMAP_WIDTH * sizeof(std::uint32_t));
}

void MemoryHeatmap::render(SDL_Renderer *renderer, const SDL_Rect &dest_rect) {
  if (mode_ == Mode::Disabled) {
    return;
  }

  // Update the texture with the latest data
  update_texture(renderer);

  // Render the texture
  if (texture_ != nullptr) {
    SDL_RenderCopy(renderer, texture_, nullptr, &dest_rect);
  }
}

} // namespace specbolt
