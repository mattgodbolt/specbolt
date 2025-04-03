#include "memory_heatmap.hpp"

#include <algorithm>
#include <cmath>
#include <print>
#include <ranges>

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
    std::ranges::for_each(read_counts_, [this](auto &count) { count *= decay_rate_; });
    std::ranges::for_each(write_counts_, [this](auto &count) { count *= decay_rate_; });
  }
}

std::uint32_t MemoryHeatmap::get_colour_for_value(float value) const {
  // Clamp value between 0 and 1
  value = std::clamp(value, 0.0f, 1.0f);

  // Apply opacity
  const auto alpha = static_cast<std::uint32_t>(opacity_ * 255);

  switch (colour_scheme_) {
    case ColourScheme::Heat: {
      // Blue (cold) to Red (hot) colour scheme
      const std::uint32_t r = value < 0.5f
                                  ? static_cast<std::uint32_t>(value * 2 * 255) // Blue to Purple: increasing red
                                  : 255; // Purple to Red: max red
      constexpr std::uint32_t g = 0;
      const std::uint32_t b = value < 0.5f ? 255 // Blue to Purple: max blue
                                           : static_cast<std::uint32_t>(
                                                 (1.0f - (value - 0.5f) * 2) * 255); // Purple to Red: decreasing blue
      return (alpha << 24) | (r << 16) | (g << 8) | b;
    }

    case ColourScheme::Spectrum: {
      // Use ZX Spectrum colours
      constexpr std::array<std::uint32_t, 8> spectrum_colours{
          0x000000, // Black
          0x0000D7, // Blue
          0xD70000, // Red
          0xD700D7, // Magenta
          0x00D700, // Green
          0x00D7D7, // Cyan
          0xD7D700, // Yellow
          0xD7D7D7 // White
      };

      // Map the value to one of the 8 colours
      const auto index = static_cast<std::size_t>(value * (spectrum_colours.size() - 1));
      const std::uint32_t colour = spectrum_colours[index];

      // Add alpha
      return (alpha << 24) | colour;
    }

    case ColourScheme::Grayscale:
    default: {
      // Black to White
      const auto intensity = static_cast<std::uint32_t>(value * 255);
      const std::uint32_t colour = (intensity << 16) | (intensity << 8) | intensity;
      return (alpha << 24) | colour;
    }
  }
}

void MemoryHeatmap::update_texture(SDL_Renderer *renderer) {
  // Find the maximum count for normalization
  constexpr float epsilon = 0.1f; // Small epsilon to avoid division by zero
  float max_count = epsilon;

  // Calculate the maximum based on current mode
  if (mode_ == Mode::ReadOnly) {
    max_count = std::ranges::max(read_counts_);
  }
  else if (mode_ == Mode::WriteOnly) {
    max_count = std::ranges::max(write_counts_);
  }
  else if (mode_ == Mode::ReadWrite) {
    const auto max_read = std::ranges::max(read_counts_);
    const auto max_write = std::ranges::max(write_counts_);
    max_count = std::max(max_read, max_write);
  }

  // Ensure we have a valid maximum
  max_count = std::max(max_count, epsilon);

  // Update the pixel buffer
  for (std::size_t address = 0; address < MEMORY_SIZE; address++) {
    // Calculate the pixel position
    const std::size_t x = address & 0xFF; // Low byte determines X
    const std::size_t y = address >> 8; // High byte determines Y

    // Get the normalized value based on the current mode
    float value;
    if (mode_ == Mode::ReadOnly) {
      value = read_counts_[address] / max_count;
    }
    else if (mode_ == Mode::WriteOnly) {
      value = write_counts_[address] / max_count;
    }
    else if (mode_ == Mode::ReadWrite) {
      value = (read_counts_[address] + write_counts_[address]) / max_count;
    }
    else {
      value = 0.0f;
    }

    // Apply logarithmic scaling to better visualize the range
    constexpr float log_scale_factor = 9.0f;
    constexpr float log_base = 10.0f;
    value = std::log1p(value * log_scale_factor) / std::log(log_base);

    // Set the pixel colour
    const std::size_t pixel_index = y * HEATMAP_WIDTH + x;
    texture_pixels_[pixel_index] = get_colour_for_value(value);
  }

  // Create or recreate the texture if needed
  if (texture_ == nullptr) {
    constexpr Uint32 pixel_format = SDL_PIXELFORMAT_ARGB8888;
    constexpr int access = SDL_TEXTUREACCESS_STREAMING;

    texture_ = SDL_CreateTexture(renderer, pixel_format, access, HEATMAP_WIDTH, HEATMAP_HEIGHT);
    if (texture_ == nullptr) {
      std::println(stderr, "Failed to create heatmap texture: {}", SDL_GetError());
      return;
    }

    // Enable alpha blending
    SDL_SetTextureBlendMode(texture_, SDL_BLENDMODE_BLEND);
  }

  // Update the texture with the new pixel data
  constexpr int pitch = HEATMAP_WIDTH * sizeof(std::uint32_t);
  SDL_UpdateTexture(texture_, nullptr, texture_pixels_.data(), pitch);
}

void MemoryHeatmap::render(SDL_Renderer *renderer, const SDL_Rect &dest_rect) {
  if (mode_ == Mode::Disabled) {
    return;
  }

  // Update the texture with the latest data
  update_texture(renderer);

  // Render the texture if it was created successfully
  if (texture_ != nullptr) {
    SDL_RenderCopy(renderer, texture_, nullptr, &dest_rect);
  }
}

} // namespace specbolt
