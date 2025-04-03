#include "heatmap_renderer.hpp"

#include <functional>
#include <print>

namespace specbolt {

HeatmapRenderer::HeatmapRenderer() : last_decay_time_(std::chrono::high_resolution_clock::now()) {
  // Connect to memory tracing automatically when created
  connect();

  // Enable heatmap by default when created
  toggle_heatmap();

  std::println("Memory heatmap enabled. Controls: F2-toggle, F3-mode, F4-color scheme, F5/F6-opacity, F7-reset");
}

HeatmapRenderer::~HeatmapRenderer() { disconnect(); }

void HeatmapRenderer::connect() {
  // Connect callbacks to memory access events
  set_memory_read_callback([this](std::uint16_t address) { on_memory_read(address); });

  set_memory_write_callback([this](std::uint16_t address) { on_memory_write(address); });
}

void HeatmapRenderer::disconnect() { clear_memory_callbacks(); }

void HeatmapRenderer::on_memory_read(std::uint16_t address) { heatmap_.record_read(address); }

void HeatmapRenderer::on_memory_write(std::uint16_t address) { heatmap_.record_write(address); }

void HeatmapRenderer::toggle_heatmap() {
  visible_ = !visible_;

  if (visible_) {
    if (heatmap_.mode() == MemoryHeatmap::Mode::Disabled) {
      heatmap_.set_mode(MemoryHeatmap::Mode::ReadWrite);
    }
  }
  else {
    heatmap_.set_mode(MemoryHeatmap::Mode::Disabled);
  }

  std::println("Heatmap {}", visible_ ? "enabled" : "disabled");
}

void HeatmapRenderer::toggle_mode() {
  if (!visible_)
    return;

  // Cycle through modes
  switch (heatmap_.mode()) {
    case MemoryHeatmap::Mode::ReadWrite:
      heatmap_.set_mode(MemoryHeatmap::Mode::ReadOnly);
      std::println("Heatmap mode: Read Only");
      break;
    case MemoryHeatmap::Mode::ReadOnly:
      heatmap_.set_mode(MemoryHeatmap::Mode::WriteOnly);
      std::println("Heatmap mode: Write Only");
      break;
    case MemoryHeatmap::Mode::WriteOnly:
      heatmap_.set_mode(MemoryHeatmap::Mode::ReadWrite);
      std::println("Heatmap mode: Read/Write");
      break;
    default: break;
  }
}

void HeatmapRenderer::toggle_color_scheme() {
  if (!visible_)
    return;

  // Cycle through color schemes
  switch (heatmap_.color_scheme()) {
    case MemoryHeatmap::ColorScheme::Heat:
      heatmap_.set_color_scheme(MemoryHeatmap::ColorScheme::Spectrum);
      std::println("Heatmap color scheme: Spectrum");
      break;
    case MemoryHeatmap::ColorScheme::Spectrum:
      heatmap_.set_color_scheme(MemoryHeatmap::ColorScheme::Grayscale);
      std::println("Heatmap color scheme: Grayscale");
      break;
    case MemoryHeatmap::ColorScheme::Grayscale:
      heatmap_.set_color_scheme(MemoryHeatmap::ColorScheme::Heat);
      std::println("Heatmap color scheme: Heat");
      break;
  }
}

void HeatmapRenderer::increase_opacity() {
  if (!visible_)
    return;
  auto opacity = heatmap_.opacity() + 0.1f;
  if (opacity > 1.0f)
    opacity = 1.0f;
  heatmap_.set_opacity(opacity);
  std::println("Heatmap opacity: {}", opacity);
}

void HeatmapRenderer::decrease_opacity() {
  if (!visible_)
    return;
  auto opacity = heatmap_.opacity() - 0.1f;
  if (opacity < 0.1f)
    opacity = 0.1f;
  heatmap_.set_opacity(opacity);
  std::println("Heatmap opacity: {}", opacity);
}

bool HeatmapRenderer::process_key(SDL_Keycode key) {
  switch (key) {
    case SDLK_F2: // Toggle heatmap
      toggle_heatmap();
      return true;

    case SDLK_F3: // Toggle mode
      toggle_mode();
      return true;

    case SDLK_F4: // Toggle color scheme
      toggle_color_scheme();
      return true;

    case SDLK_F5: // Increase opacity
      increase_opacity();
      return true;

    case SDLK_F6: // Decrease opacity
      decrease_opacity();
      return true;

    case SDLK_F7: // Reset heatmap
      if (visible_) {
        reset();
        std::println("Heatmap reset");
        return true;
      }
      break;
  }

  return false;
}

void HeatmapRenderer::render(SDL_Renderer *renderer, const SDL_Rect &dest_rect) {
  if (visible_) {
    heatmap_.render(renderer, dest_rect);
  }
}

void HeatmapRenderer::reset() { heatmap_.reset(); }

void HeatmapRenderer::update() {
  // Apply decay at a reasonable rate
  auto now = std::chrono::high_resolution_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_decay_time_).count();

  // Decay every 100ms
  if (elapsed > 100) {
    heatmap_.decay();
    last_decay_time_ = now;
  }
}

} // namespace specbolt
