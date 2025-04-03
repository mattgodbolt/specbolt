#pragma once

#include <cstdint>
#include <functional>

namespace specbolt {

// Set memory access callbacks
void set_memory_read_callback(std::function<void(std::uint16_t)> callback);
void set_memory_write_callback(std::function<void(std::uint16_t)> callback);

// Clear callbacks
void clear_memory_callbacks();

// External linkage to the callbacks
extern std::function<void(std::uint16_t)> g_read_callback;
extern std::function<void(std::uint16_t)> g_write_callback;

} // namespace specbolt

// These functions are declared in global namespace intentionally
// to be callable from any module
extern "C" {
void notify_memory_read(std::uint16_t addr);
void notify_memory_write(std::uint16_t addr);
}
// These functions are declared in global namespace intentionally
// to be callable from any module
extern "C" {
void notify_memory_read(std::uint16_t addr);
void notify_memory_write(std::uint16_t addr);
}
