#include "memory_hooks.hpp"

namespace specbolt {

// Global callbacks for memory access
std::function<void(std::uint16_t)> g_read_callback;
std::function<void(std::uint16_t)> g_write_callback;

// Set memory access callbacks
void set_memory_read_callback(std::function<void(std::uint16_t)> callback) { g_read_callback = std::move(callback); }

void set_memory_write_callback(std::function<void(std::uint16_t)> callback) { g_write_callback = std::move(callback); }

// Clear callbacks
void clear_memory_callbacks() {
  g_read_callback = nullptr;
  g_write_callback = nullptr;
}

} // namespace specbolt

// These functions are defined in global namespace intentionally
// so they can be linked to from any module
void notify_memory_read(std::uint16_t addr) {
  if (specbolt::g_read_callback) {
    specbolt::g_read_callback(addr);
  }
}

void notify_memory_write(std::uint16_t addr) {
  if (specbolt::g_write_callback) {
    specbolt::g_write_callback(addr);
  }
}
