# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands
- Configure: `CC=clang-20 CXX=clang++-20 cmake -B build/Debug -G Ninja -DCMAKE_BUILD_TYPE=Debug`
- Build: `cmake --build build/Debug`
- Run: `./build/Debug/sdl/specbolt-sdl`
- Tests: `ctest --test-dir build/Debug`
- Single test: `./build/Debug/<component>/test/<component>_test [<test-name>]`

## Lint/Format
- Pre-commit hooks check formatting: `pre-commit run --all-files`
- Formatting: Code follows `.clang-format` with LLVM-based style

## Code Style
- See detailed style guide in [STYLE_GUIDE.md](STYLE_GUIDE.md) for comprehensive guidelines
- C++26 with CMake and C++ modules when SPECBOLT_MODULES is on
- REQUIRES clang-20 or newer for C++ modules and modern features
- Column limit: 120 chars, tab width: 2 spaces
- Includes order: 1) System (<...>), 2) Local ("..."), 3) Others
- Types: Use strong typing with enum classes and standard types
- Imports: Use `import module:submodule` with modules, fallback to includes
- Error handling: Return error codes or throw exceptions where appropriate
- Naming: PascalCase for types/classes, snake_case for functions/variables
- British English spelling preferred (e.g., "colour" not "color")
- Make variables const by default, use if-init where appropriate
- Const value parameters in function definitions, not declarations
- Modules structure: `.cppm` for module interface files, `.cpp` for implementation
- Tests use Catch2 framework with standard TEST_CASE and SECTION macros
