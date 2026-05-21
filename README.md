# specbolt ZX Spectrum Emulator [![specbolt CI](https://github.com/mattgodbolt/specbolt/actions/workflows/ci.yml/badge.svg)](https://github.com/mattgodbolt/specbolt/actions/workflows/ci.yml)

A modern C++26 ZX Spectrum emulator with a focus on clean architecture and educational value. specbolt demonstrates the power of modern C++ features including modules and std::ranges while emulating the iconic 8-bit computer.

## Project Overview

specbolt is structured into several key components:

- **Z80 CPU Emulation** - Multiple implementations showcasing different architectural approaches
- **Memory and Peripherals** - Clean abstractions for ZX Spectrum hardware components
- **Visualization Tools** - Including memory heatmap visualization for educational purposes
- **Multiple Frontends** - SDL, Console, and Web interfaces

## Development Setup

### Prerequisites

- **Compiler:** Clang 20+ (required for C++26 modules support)
  - On Ubuntu: `wget https://apt.llvm.org/llvm.sh; sudo bash llvm.sh 20 all`
- **Build System:** CMake 3.30+ and Ninja
- **Libraries:**
  - SDL2: `sudo apt-get install libsdl2-dev` (Ubuntu)
  - Readline: `sudo apt-get install libreadline-dev` (for console app)

### Building

The simplest path is via CMake presets (see `CMakePresets.json` for the list):

```bash
# Configure, build, test
cmake --preset debug              # Debug, no modules — works with clang or gcc
cmake --build --preset debug
ctest --preset debug

# Run
./build/debug/sdl/specbolt_sdl
```

Other useful presets: `debug-modules` (needs clang + libc++), `release` (RelWithDebInfo, runs the zexdoc regression tests), and `release-modules`.

To pin a specific compiler, set `CC`/`CXX` or create a local `CMakeUserPresets.json` (gitignored) that inherits a public preset and overrides `CMAKE_CXX_COMPILER`.

### Web/WASM Build

```bash
# Install WASM dependencies
sudo apt install libc++-20-dev-wasm32 libclang-rt-20-dev-wasm32

# Configure with WASI support
cmake -B build/Wasm -G Ninja -DSPECBOLT_WASM=ON -DSPECBOLT_WASI_SYSROOT=/path/to/wasi

# Setup web environment
cd web
npm install
echo "VITE_SPECBOLT_WASI_SYSROOT=/home/user/path/to/build/root" > .env.local

# Run development server
npm start
```

## Project Documentation

- [Style Guide](STYLE_GUIDE.md) - Comprehensive coding standards for the project
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
