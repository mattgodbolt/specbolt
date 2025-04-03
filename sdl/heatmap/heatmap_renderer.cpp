#include "heatmap_renderer.hpp"

#include <chrono>
#include <print>

using namespace std::chrono_literals;

namespace {
// Configuration constants
constexpr auto DECAY_INTERVAL = 100ms; // How often to apply decay to the heatmap
} // namespace

namespace specbolt {

HeatmapRenderer::HeatmapRenderer(Memory &memory) :
    memory_listener_(heatmap_), last_decay_time_(std::chrono::high_resolution_clock::now()) {
  // Connect to memory tracing automatically when created
  connect(memory);

  // Enable heatmap by default when created
  toggle_heatmap();

  std::println("Memory heatmap enabled. Controls: F2-toggle, F3-mode, F4-colour scheme, F5/F6-opacity, F7-reset");
}

HeatmapRenderer::~HeatmapRenderer() { disconnect(); }

void HeatmapRenderer::connect(Memory &memory) {
  // Connect our listener to the memory
  memory.set_listener(&memory_listener_);
}

void HeatmapRenderer::disconnect() {
  // Nothing to do - the Memory object is responsible for ignoring the listener
  // once it's gone, and our memory_listener_ is owned by this class
}

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

void HeatmapRenderer::toggle_colour_scheme() {
  if (!visible_)
    return;

  // Cycle through colour schemes
  switch (heatmap_.colour_scheme()) {
    case MemoryHeatmap::ColourScheme::Heat:
      heatmap_.set_colour_scheme(MemoryHeatmap::ColourScheme::Spectrum);
      std::println("Heatmap colour scheme: Spectrum");
      break;
    case MemoryHeatmap::ColourScheme::Spectrum:
      heatmap_.set_colour_scheme(MemoryHeatmap::ColourScheme::Grayscale);
      std::println("Heatmap colour scheme: Grayscale");
      break;
    case MemoryHeatmap::ColourScheme::Grayscale:
      heatmap_.set_colour_scheme(MemoryHeatmap::ColourScheme::Heat);
      std::println("Heatmap colour scheme: Heat");
      break;
  }
}

void HeatmapRenderer::increase_opacity() {
  if (!visible_)
    return;

  if (float opacity = heatmap_.opacity() + 0.1f; opacity > 1.0f) {
    heatmap_.set_opacity(1.0f);
    std::println("Heatmap opacity: {}", 1.0f);
  }
  else {
    heatmap_.set_opacity(opacity);
    std::println("Heatmap opacity: {}", opacity);
  }
}

void HeatmapRenderer::decrease_opacity() {
  if (!visible_)
    return;

  if (float opacity = heatmap_.opacity() - 0.1f; opacity < 0.1f) {
    heatmap_.set_opacity(0.1f);
    std::println("Heatmap opacity: {}", 0.1f);
  }
  else {
    heatmap_.set_opacity(opacity);
    std::println("Heatmap opacity: {}", opacity);
  }
}

bool HeatmapRenderer::process_key(SDL_Keycode key) {
  switch (key) {
    case SDLK_F2: // Toggle heatmap
      toggle_heatmap();
      return true;

    case SDLK_F3: // Toggle mode
      toggle_mode();
      return true;

    case SDLK_F4: // Toggle colour scheme
      toggle_colour_scheme();
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
  // Check if it's time to apply decay based on elapsed time
  const auto now = std::chrono::high_resolution_clock::now();

  // Apply decay at regular intervals
  if (now - last_decay_time_ > DECAY_INTERVAL) {
    heatmap_.decay();
    last_decay_time_ = now;
  }
}

} // namespace specbolt
