# SpecBolt Common Code Patterns

This document provides examples of common design patterns and code idioms used in the SpecBolt project. Use these as reference when implementing new features.

## Table of Contents

1. [Observer Pattern Implementation](#observer-pattern-implementation)
2. [Memory Access Tracking](#memory-access-tracking)
3. [Module Support](#module-support)
4. [Visualization Components](#visualization-components)
5. [If-Init Pattern](#if-init-pattern)
6. [Const Correctness](#const-correctness)
7. [SDL Integration](#sdl-integration)
8. [Configuration Constants](#configuration-constants)

## Observer Pattern Implementation

The observer pattern is used extensively in SpecBolt to allow components to react to events from other components.

### Base Pattern

```cpp
// In Component.hpp
class Component {
public:
  class Listener {
  public:
    virtual ~Listener() = default;
    virtual void on_event(/* event parameters */) = 0;
  };

  void set_listener(Listener *listener) { listener_ = listener; }
  [[nodiscard]] bool has_listener() const { return listener_ != nullptr; }

private:
  Listener *listener_ = nullptr;

  // Helper to notify the listener
  void notify_event(/* event parameters */) {
    if (listener_) {
      listener_->on_event(/* event parameters */);
    }
  }
};
```

### Concrete Example: Memory Listener

```cpp
// Memory.hpp
class Memory {
public:
  class Listener {
  public:
    virtual ~Listener() = default;
    virtual void on_memory_read(std::uint16_t address) = 0;
    virtual void on_memory_write(std::uint16_t address) = 0;
  };

  void set_listener(Listener *listener) { listener_ = listener; }
  [[nodiscard]] bool has_listener() const { return listener_ != nullptr; }

  // Memory access methods that notify the listener
  std::uint8_t read(std::uint16_t address) const {
    if (listener_) {
      listener_->on_memory_read(address);
    }
    return memory_[address];
  }

private:
  Listener *listener_ = nullptr;
  std::array<std::uint8_t, 65536> memory_{};
};
```

## Memory Access Tracking

The SpecBolt memory heatmap demonstrates a pattern for tracking memory access:

```cpp
// HeatmapMemoryListener.hpp
class HeatmapMemoryListener final : public Memory::Listener {
public:
  explicit HeatmapMemoryListener(MemoryHeatmap &heatmap) : heatmap_(heatmap) {}

  void on_memory_read(std::uint16_t address) override {
    heatmap_.record_read(address);
  }

  void on_memory_write(std::uint16_t address) override {
    heatmap_.record_write(address);
  }

private:
  MemoryHeatmap &heatmap_;
};

// Usage
MemoryHeatmap heatmap;
HeatmapMemoryListener listener(heatmap);
memory.set_listener(&listener);
```

## Module Support

SpecBolt supports both traditional includes and C++ modules:

```cpp
// In .hpp files
#ifndef SPECBOLT_MODULES
#include "peripherals/Memory.hpp"
#include <vector>
#else
import peripherals;
import <vector>;
#endif

// In .cppm files
export module module_name;

#ifndef SPECBOLT_MODULES
#include "header_file.hpp"
#else
import other_module;
#endif

export namespace specbolt {
  // Exported components
}
```

## Visualization Components

Visualization components follow a consistent pattern:

```cpp
// Data collection component
class SomeDataListener final : public SomeComponent::Listener {
public:
  explicit SomeDataListener(DataStore &data_store) : data_store_(data_store) {}

  void on_event(/* parameters */) override {
    data_store_.record_event(/* parameters */);
  }

private:
  DataStore &data_store_;
};

// Visualization component
class SomeVisualizer {
public:
  explicit SomeVisualizer(SomeComponent &component)
    : data_store_(), listener_(data_store_) {
    component.set_listener(&listener_);
  }

  void render(SDL_Renderer *renderer, const SDL_Rect &dest_rect) {
    // Visualization logic
  }

  // Configuration methods
  void set_some_option(SomeOption option) { option_ = option; }

private:
  DataStore data_store_;
  SomeDataListener listener_;
  SomeOption option_ = SomeOption::Default;
};
```

## If-Init Pattern

Use if-with-initializer to limit variable scope:

```cpp
// Good: Limits the scope of now and elapsed to the if statement
void update() {
  // Apply decay at regular intervals
  if (const auto now = std::chrono::high_resolution_clock::now();
      now - last_decay_time_ > DECAY_INTERVAL) {
    apply_decay();
    last_decay_time_ = now;
  }
}

// Good: Conditional mutation with clear scope
if (float opacity = current_opacity() + 0.1f; opacity > 1.0f) {
  set_opacity(1.0f);
} else {
  set_opacity(opacity);
}
```

## Const Correctness

Function declarations vs. definitions:

```cpp
// In header (.hpp) - Declaration without const for value parameters
class Example {
public:
  void process(std::uint16_t value);
  void update(ProcessMode mode);
};

// In implementation (.cpp) - Definition with const for value parameters
void Example::process(const std::uint16_t value) {
  // Implementation that doesn't modify value
}

void Example::update(const ProcessMode mode) {
  // Implementation that doesn't modify mode
}

// For inline functions in headers
void inline_function(const std::uint16_t value) {
  // Implementation
}
```

## SDL Integration

Pattern for SDL rendering components:

```cpp
class SdlComponent {
public:
  SdlComponent(SDL_Renderer *renderer)
    : texture_(nullptr) {
    // Initialize resources
    create_texture(renderer);
  }

  ~SdlComponent() {
    if (texture_) {
      SDL_DestroyTexture(texture_);
    }
  }

  void render(SDL_Renderer *renderer, const SDL_Rect &dest_rect) {
    update_texture(renderer);
    SDL_RenderCopy(renderer, texture_, nullptr, &dest_rect);
  }

private:
  SDL_Texture *texture_;

  void create_texture(SDL_Renderer *renderer) {
    // Create texture resources
  }

  void update_texture(SDL_Renderer *renderer) {
    // Update texture data
  }
};
```

## Configuration Constants

Pattern for configuration constants:

```cpp
// In implementation file (.cpp)
namespace {
// Configuration constants
constexpr auto DECAY_INTERVAL = 100ms;
constexpr float DEFAULT_OPACITY = 0.7f;
constexpr int TEXTURE_SIZE = 256;
} // anonymous namespace

// Usage within the implementation
void Component::update() {
  if (elapsed_time > DECAY_INTERVAL) {
    // ...
  }
}
```

---

These examples should help developers understand and follow the coding patterns established in the SpecBolt project. When in doubt, refer to existing code and follow similar patterns for consistency.
