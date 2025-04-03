# SpecBolt ZX Spectrum Emulator [![specbolt CI](https://github.com/mattgodbolt/specbolt/actions/workflows/ci.yml/badge.svg)](https://github.com/mattgodbolt/specbolt/actions/workflows/ci.yml)

A modern C++23 ZX Spectrum emulator with a focus on clean architecture and educational value. SpecBolt demonstrates the power of modern C++ features including modules and std::ranges while emulating the iconic 8-bit computer.

## Project Overview

SpecBolt is structured into several key components:

- **Z80 CPU Emulation** - Multiple implementations showcasing different architectural approaches
- **Memory and Peripherals** - Clean abstractions for ZX Spectrum hardware components
- **Visualization Tools** - Including memory heatmap visualization for educational purposes
- **Multiple Frontends** - SDL, Console, and Web interfaces

## Development Setup

### Prerequisites

- **Compiler:** Clang 20+ (required for C++23 modules support)
  - On Ubuntu: `wget https://apt.llvm.org/llvm.sh; sudo bash llvm.sh 20 all`
- **Build System:** CMake 3.26+ and Ninja
- **Libraries:**
  - SDL2: `sudo apt-get install libsdl2-dev` (Ubuntu)
  - Readline: `sudo apt-get install libreadline-dev` (for console app)

### Building

```bash
# Configure with modules support (default)
cmake -B build/Debug -G Ninja -DCMAKE_BUILD_TYPE=Debug

# Configure without modules
cmake -B build/DebugNoModules -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSPECBOLT_USE_MODULES=OFF

# Build
cmake --build build/Debug

# Run
./build/Debug/sdl/specbolt_sdl
```

### Web/WASM Build

```bash
# Install WASM dependencies
sudo apt install libc++-20-dev-wasm32 libclang-rt-20-dev-wasm32

# Configure with WASI support
cmake -B build/Wasm -G Ninja -DSPECBOLT_WASI_SDK=ON -DSPECBOLT_WASI_SYSROOT=/path/to/wasi

# Setup web environment
cd web
npm install
echo "VITE_SPECBOLT_WASI_SYSROOT=/home/user/path/to/build/root" > .env.local

# Run development server
npm start
```

## Project Documentation

- [Style Guide](STYLE_GUIDE.md) - Comprehensive coding standards for the project
- [Integration Patterns](INTEGRATION.md) - How to extend and integrate with existing components
- [Example Patterns](EXAMPLE_PATTERNS.md) - Common code patterns with concrete examples
- [Project Glossary](GLOSSARY.md) - Definitions of ZX Spectrum and emulator terminology
- [CLAUDE.md](CLAUDE.md) - Instructions for Claude AI when working with the codebase

## Features

- Full Z80 CPU emulation
- Accurate audio and video emulation
- Memory access visualization via heatmap overlay
- Support for keyboard input
- Loading from tape, snapshot files, and Internet Archive
- Multiple frontends: SDL graphical interface, console mode, and web version

## Controls

| Key        | Function                                |
|------------|----------------------------------------|
| F1         | Help screen                            |
| F2         | Toggle heatmap (when enabled)          |
| F3         | Toggle heatmap mode (Read/Write/Both)  |
| F4         | Toggle heatmap colour scheme           |
| F5/F6      | Adjust heatmap opacity                 |
| F7         | Reset heatmap data                     |
| Esc        | Exit                                   |

## Acknowledgements

- [Hana Dusíková](https://github.com/hanickadot) for all the clever C++ stuff, and help with modules
- [blargg (aka Shay Green)](http://www.slack.net/~ant/) for the band-limiting code used in the audio
- [World of Spectrum](https://worldofspectrum.org/) for documentation and resources
- [ClaudeAI](https://claude.ai/code) for pair programming and documentation assistance
