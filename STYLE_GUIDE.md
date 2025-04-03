# SpecBolt C++ Style Guide

This document outlines the C++ coding standards and best practices for the SpecBolt project. Following these guidelines ensures code consistency, readability, and maintainability across the codebase.

## Table of Contents

1. [Variable Declarations](#variable-declarations)
2. [Const Correctness](#const-correctness)
3. [If Statements](#if-statements)
4. [Includes and Imports](#includes-and-imports)
5. [Naming Conventions](#naming-conventions)
6. [Comments](#comments)
7. [Enumerations](#enumerations)
8. [Language Features](#language-features)

## Variable Declarations

### General Rules

- Declare variables as `const` by default
- Use the appropriate scope for variables (declare as close to usage as possible)
- Use `auto` when the type is obvious from the initializer or when the specific type isn't important
- Specify the type when it improves readability or when the exact type matters

### Examples

```cpp
// Good: const by default when the variable won't be modified
const auto now = std::chrono::high_resolution_clock::now();

// Good: explicit type when it matters
const std::uint32_t r = value < 0.5f
                      ? static_cast<std::uint32_t>(value * 2 * 255)
                      : 255;

// Good: constexpr for compile-time constants
constexpr float epsilon = 0.1f;
```

## Const Correctness

### Function Parameters

- In function **declarations** (header files, no body), do NOT use `const` for value parameters:
  ```cpp
  // In header (.hpp) file - declarations
  void record_read(std::uint16_t address);
  void set_mode(Mode mode);
  ```

- In function **definitions** (implementation with body), use `const` for value parameters that aren't modified:
  ```cpp
  // In implementation (.cpp) file - definitions
  void MemoryHeatmap::record_read(const std::uint16_t address) {
    // Implementation
  }

  // For inline functions in headers (both declaration and definition)
  void set_mode(const Mode mode) { mode_ = mode; }
  ```

### Member Functions

- Mark member functions as `const` when they don't modify the object's state
- Use `[[nodiscard]]` for functions that return values which should not be ignored

```cpp
[[nodiscard]] Mode mode() const { return mode_; }
[[nodiscard]] std::uint32_t get_colour_for_value(float value) const;
```

## If Statements

### If with Initializer

- Use if-with-initializer (C++17) to limit variable scope when possible
- Particularly useful for variables used only within the if statement

```cpp
// Good: limits the scope of 'elapsed' to the if condition and body
if (const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_decay_time_).count();
    elapsed > 100) {
  heatmap_.decay();
  last_decay_time_ = now;
}

// Good: limits the scope of 'opacity' to the if condition and body
if (float opacity = heatmap_.opacity() + 0.1f; opacity > 1.0f) {
  heatmap_.set_opacity(1.0f);
} else {
  heatmap_.set_opacity(opacity);
}
```

## Includes and Imports

### Include Only What You Need

- Only include headers that are directly used in the file
- Remove unnecessary includes to reduce compilation time
- Order includes alphabetically within each group

### Include Order

1. Associated header (matching .hpp for .cpp files)
2. C++ standard library headers
3. Third-party library headers
4. Project headers

```cpp
// Example for memory_heatmap.cpp
#include "memory_heatmap.hpp"

#include <algorithm>
#include <cmath>
#include <print>
#include <ranges>

#include <SDL.h>

#include "project_header.hpp"
```

### Module Support

- Use preprocessor guards to support both module and non-module builds:

```cpp
#ifndef SPECBOLT_MODULES
#include "peripherals/Memory.hpp"
#else
import peripherals;
#endif
```

## Naming Conventions

### General

- Use meaningful, descriptive names
- Use British English spelling for identifiers (e.g., `colour` not `color`)

### Types and Classes

- Use PascalCase for classes, structs, enums, and type aliases:
  ```cpp
  class MemoryHeatmap { ... };
  enum class ColourScheme { ... };
  ```

### Functions and Variables

- Use snake_case for function and variable names:
  ```cpp
  void toggle_colour_scheme();
  float decay_rate_ = 0.95f;
  ```

### Constants and Enumerations

- Use all UPPERCASE for standalone constants:
  ```cpp
  constexpr std::size_t MEMORY_SIZE = 64 * 1024;
  ```

- Use PascalCase for enum members:
  ```cpp
  enum class Mode {
    ReadOnly,   // Not ReadOnly or READ_ONLY
    WriteOnly,
    ReadWrite,
    Disabled
  };
  ```

### Class Members

- Use trailing underscores for private/protected class member variables:
  ```cpp
  private:
    Mode mode_ = Mode::ReadWrite;
    float opacity_ = 0.7f;
  ```

## Comments

### General Rules

- Write clear, concise comments explaining "why" rather than "what"
- Keep comments up-to-date with code changes
- Use complete sentences with proper punctuation

### Documentation Comments

- Place comments before the code they describe
- For class and function descriptions, explain purpose and usage

```cpp
// Helper method for visualizing memory access patterns as a heatmap overlay
void render(SDL_Renderer *renderer, const SDL_Rect &dest_rect);
```

## Enumerations

- Use `enum class` instead of plain `enum` to avoid name clashes
- Place each enum value on a separate line with a comment if the meaning isn't obvious

```cpp
enum class ColourScheme {
  Heat,      // Blue (cold) to Red (hot)
  Spectrum,  // Use ZX Spectrum colours
  Grayscale  // Black to White
};
```

## Language Features

### Modern C++ Features

- Use `constexpr` for values known at compile-time
- Use range-based for loops and algorithms from `<algorithm>` and `<ranges>`
- Prefer `std::ranges::` versions of algorithms when available
- Use `std::optional` for values that may or may not exist
- Use `[[nodiscard]]` for functions whose return values should not be ignored

### Examples

```cpp
// Use std::ranges algorithms
std::ranges::for_each(read_counts_, [this](auto &count) { count *= decay_rate_; });

// Use constexpr for compile-time constants
constexpr int HEATMAP_WIDTH = 256;

// Use nodiscard for functions that return values that shouldn't be ignored
[[nodiscard]] ColourScheme colour_scheme() const { return colour_scheme_; }
```

---

These style guidelines will evolve as the project grows. When in doubt, follow the style of surrounding code for consistency.
