# SpecBolt Integration Patterns

This document outlines patterns for extending and integrating with SpecBolt components. Following these patterns ensures consistent architecture and maintainable code.

## Table of Contents
1. [Observer Pattern](#observer-pattern)
2. [Component Extension](#component-extension)
3. [Frontend Integration](#frontend-integration)
4. [New Peripherals](#new-peripherals)
5. [Visualization Tools](#visualization-tools)
6. [File Format Support](#file-format-support)

## Observer Pattern

The primary integration pattern in SpecBolt is the observer pattern, allowing components to subscribe to events from other components.

### Memory Access Listener

The `Memory::Listener` interface enables components to observe memory access:

```cpp
// In Memory.hpp
class Memory {
public:
  class Listener {
  public:
    virtual ~Listener() = default;
    virtual void on_memory_read(std::uint16_t address) = 0;
    virtual void on_memory_write(std::uint16_t address) = 0;
  };

  void set_listener(Listener *listener);
  bool has_listener() const;

private:
  Listener *listener_ = nullptr;
};

// Implementation example
class MyMemoryMonitor final : public Memory::Listener {
public:
  void on_memory_read(std::uint16_t address) override {
    // React to memory read
  }

  void on_memory_write(std::uint16_t address) override {
    // React to memory write
  }
};

// Usage
Memory memory;
MyMemoryMonitor monitor;
memory.set_listener(&monitor);
```

### Guidelines for New Listeners

1. Make listener classes `final` unless they're designed to be subclassed
2. Consider narrow, focused listeners over broad ones
3. Use raw pointers for non-owning references to listeners
4. Always check if a listener is set before notifying

## Component Extension

When extending existing components:

### Z80 CPU Implementations

The project has multiple implementations (v1, v2, v3) of the Z80 CPU, demonstrating different architectural approaches:

```cpp
// Required interface for Z80 implementations
class Z80Base {
public:
  virtual ~Z80Base() = default;
  virtual void reset() = 0;
  virtual int step() = 0;
  virtual RegisterFile &registers() = 0;
  // ...
};

// New implementation (e.g., v4)
class Z80v4 : public Z80Base {
public:
  // Implement required interface
};
```

### Guidelines for New CPU Versions

1. Consistently implement the Z80Base interface
2. Consider performance vs. accuracy tradeoffs
3. Document architectural decisions and strategies
4. Provide comprehensive test coverage

## Frontend Integration

SpecBolt supports multiple frontends (SDL, Console, Web). To add a new frontend:

### Frontend Structure

```
newfrontend/
  ├── CMakeLists.txt       # Build configuration
  ├── main.cpp             # Entry point
  └── [frontend-specific]  # Custom components
```

### Integration Points

1. Include `Spectrum` for emulator functionality
2. Include `Memory`, `Z80`, and peripherals as needed
3. Provide input handling and display mechanisms
4. Wire up listeners and event handling

### Example Frontend Skeleton

```cpp
#include "spectrum/Spectrum.hpp"

// Other required components
#include <your_frontend_library>

int main(int argc, char *argv[]) {
  // Set up emulator
  Spectrum spectrum;

  // Set up frontend resources

  // Main loop
  while (running) {
    // Handle input

    // Run emulation step
    spectrum.step();

    // Update display

    // Manage timing
  }

  return 0;
}
```

## New Peripherals

To add new peripherals or hardware components:

### Peripheral Structure

```cpp
// In peripherals/NewDevice.hpp
class NewDevice {
public:
  NewDevice();
  ~NewDevice();

  // Device-specific interface

  // Optional integration with existing peripherals
  void connect_to_memory(Memory &memory);

private:
  // Implementation details
};
```

### Peripheral Integration

1. Add to `CMakeLists.txt` in peripherals directory
2. Update module exports if using C++ modules
3. Wire up in the `Spectrum` class if it's a core peripheral

## Visualization Tools

For visualization components like the memory heatmap:

### Visualization Pattern

```cpp
// Listener component
class VisualizationListener : public SomeComponent::Listener {
  // Implement event handlers
};

// Renderer component
class VisualizationRenderer {
public:
  // Setup and rendering methods
  void render(/* rendering context */);

  // Configuration methods
  void set_some_option(/* option values */);

private:
  VisualizationListener listener_;
  // Rendering resources
};
```

### Guidelines for Visualizations

1. Separate data collection from visualization
2. Provide interactive controls for configuration
3. Use consistent initialization patterns
4. Support enable/disable functionality
5. Follow SpecBolt naming conventions

## File Format Support

To add support for new file formats:

### File Format Handler

```cpp
// In spectrum/Formats/NewFormat.hpp
class NewFormatHandler {
public:
  static bool load(Spectrum &spectrum, const std::string &filename);
  static bool save(const Spectrum &spectrum, const std::string &filename);

  // Format detection
  static bool is_valid_format(const std::string &filename);
};
```

### Integration with Asset Loading

1. Add format detection in `Assets` class
2. Update CMakeLists.txt to include new files
3. Document the format support in README.md

## Common Tips

1. Use British English spelling throughout the codebase
2. Apply const correctness following the style guide
3. Prefer single-phase initialization when possible
4. Use the proper scope for variables with if-init where appropriate
5. Make variables const by default
6. Use std::ranges algorithms where applicable
7. Support both module and non-module builds
8. Provide adequate unit tests for new components

This document will evolve as new integration patterns emerge in the project.
