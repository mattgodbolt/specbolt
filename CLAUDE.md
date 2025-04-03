# CLAUDE.md

This file provides comprehensive guidance to Claude Code (claude.ai/code) when working with code in this repository. It includes project-specific instructions, common workflows, and style guidelines to ensure consistent, high-quality contributions.

## Key Project Resources

- [README.md](README.md): Overview, build instructions, and feature summary
- [STYLE_GUIDE.md](STYLE_GUIDE.md): Detailed coding standards and best practices
- [INTEGRATION.md](INTEGRATION.md): Patterns for extending and integrating components
- [GLOSSARY.md](GLOSSARY.md): Definitions of project-specific terminology

## Build Commands

### Standard Builds

```bash
# Configure (with modules - requires clang-20+)
CC=clang-20 CXX=clang++-20 cmake -B build/Debug -G Ninja -DCMAKE_BUILD_TYPE=Debug

# Configure (without modules)
CC=clang-20 CXX=clang++-20 cmake -B build/DebugNoModules -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSPECBOLT_USE_MODULES=OFF

# Build
cmake --build build/Debug  # or build/DebugNoModules

# Run SDL frontend
./build/Debug/sdl/specbolt_sdl

# Run console frontend
./build/Debug/console/specbolt_console
```

### Testing

```bash
# Run all tests
ctest --test-dir build/Debug

# Run specific component tests
./build/Debug/<component>/test/<component>_test

# Run a specific test case
./build/Debug/<component>/test/<component>_test "[<test-name>]"
```

## Lint/Format

- Pre-commit hooks check formatting: `pre-commit run --all-files`
- Formatting: Code follows `.clang-format` with LLVM-based style
- Run: `clang-format -i path/to/file.cpp` to format a specific file

## Code Style

See [STYLE_GUIDE.md](STYLE_GUIDE.md) for comprehensive guidelines.

### Core Principles

- C++23 features with CMake and C++ modules when SPECBOLT_MODULES is on
- Use modern C++ features (std::ranges, constexpr, concepts, etc.)
- Make code clear and maintainable over clever optimizations
- Follow single responsibility principle for classes and functions

### Key Style Points

- **Naming**:
  - PascalCase for types/classes/enums
  - snake_case for functions/variables
  - Trailing underscore for class members (`opacity_`)
  - UPPERCASE for standalone constants

- **Formatting**:
  - Column limit: 120 chars
  - Tab width: 2 spaces
  - Consistent bracing style (LLVM)

- **C++ Best Practices**:
  - British English spelling (e.g., "colour" not "color")
  - Variables const by default
  - Use if-init where appropriate (`if (const auto val = ...; condition)`)
  - Const value parameters in function definitions, not declarations
  - Use `[[nodiscard]]` for functions with important return values
  - Implement const-correctness throughout

- **Modules**:
  - `.cppm` for module interface files
  - `.cpp` for implementation
  - Support both module and non-module builds with preprocessor guards

### Testing

- Tests use Catch2 framework
- Use standard TEST_CASE and SECTION macros
- Test each component in isolation
- Provide comprehensive coverage

## Common Workflows

### Adding a New Feature

1. **Plan Architecture**: Consider how your feature integrates with existing components
2. **Check Integration Patterns**: Reference [INTEGRATION.md](INTEGRATION.md) for established patterns
3. **Implement Core Logic**: Follow style guidelines while implementing the feature
4. **Add Tests**: Write comprehensive tests for your new code
5. **Document**: Update relevant documentation files
6. **Build and Test**: Verify in both module and non-module modes

### Fixing a Bug

1. **Reproduce**: Create a minimal test case to reproduce the issue
2. **Diagnose**: Identify the root cause using appropriate debugging techniques
3. **Fix**: Make the minimal necessary changes to fix the bug
4. **Test**: Verify the fix works in all build configurations
5. **Document**: Add regression test to prevent future occurrences

### Improving Performance

1. **Measure**: Use profiling tools to identify actual bottlenecks
2. **Develop Strategy**: Plan optimizations with minimal impact on readability
3. **Implement**: Make changes to improve performance
4. **Verify**: Confirm performance improvements with benchmarks
5. **Document**: Note optimization techniques used for future reference

## Project Structure

```
specbolt/
  ├── z80/                 # Z80 CPU emulation implementations
  │   ├── common/          # Shared CPU components
  │   ├── v1/              # First implementation approach
  │   ├── v2/              # Second implementation approach
  │   └── v3/              # Third implementation approach
  ├── peripherals/         # Hardware peripherals
  │   ├── Memory.*         # Memory subsystem
  │   ├── Video.*          # Video display
  │   ├── Audio.*          # Sound generation
  │   ├── Keyboard.*       # Input handling
  │   └── Tape.*           # Tape interface
  ├── spectrum/            # Core ZX Spectrum integration
  ├── sdl/                 # SDL graphical frontend
  │   └── heatmap/         # Memory access visualization
  ├── console/             # Text-based console frontend
  ├── web/                 # Web frontend using WASM
  └── cmake/               # CMake helper modules
```

## Performance Expectations

- CPU emulation should be fast enough for real-time operation
- Memory access visualization should have minimal impact on performance
- Web version may be slower but still usable for demonstration

## Troubleshooting

### Module Build Issues

- Check clang version is 20+ with `clang++ --version`
- Verify module visibility with `export SPECBOLT_USE_MODULES=ON`
- Module-specific code should be wrapped in `#ifdef SPECBOLT_MODULES` guards

### Common Error Messages

- "No such file or directory": Check include paths and module imports
- "No member named...": Check for typos or missing imports
- "Const-qualified parameter": Ensure declarations don't have const value parameters

## Recommended Tools

- VS Code with C++ extensions
- clangd for code navigation
- cmake-tools for build integration
- clang-tidy for static analysis
