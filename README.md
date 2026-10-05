# specbolt ZX Spectrum Emulator [![specbolt CI](https://github.com/mattgodbolt/specbolt/actions/workflows/ci.yml/badge.svg)](https://github.com/mattgodbolt/specbolt/actions/workflows/ci.yml)

A modern C++26 ZX Spectrum emulator with a focus on clean architecture and educational value. specbolt demonstrates the
power of modern C++ features including modules and std::ranges while emulating the iconic 8-bit computer.

## Project Overview

specbolt is structured into several key components:

- **Z80 CPU Emulation** - Multiple implementations showcasing different architectural approaches, including
  [v4](z80/v4/README.md), which C++26 reflection generates at compile time from a text description of the instruction set
- **Memory and Peripherals** - Clean abstractions for ZX Spectrum hardware components
- **Visualization Tools** - Including memory heatmap visualization for educational purposes
- **Multiple Frontends** - SDL, Console, and Web interfaces

## Development Setup

### Prerequisites

- **Compiler:** Clang 20+ or gcc 16+; either builds the modules presets (clang with libc++). Reflection needs gcc 16+
  or a clang fork: see [C++26 reflection](#c26-reflection)
  - On Ubuntu: `wget https://apt.llvm.org/llvm.sh; sudo bash llvm.sh 20 all`
- **Build System:** CMake 3.30+ and Ninja
- **Libraries:**
  - SDL2: `sudo apt-get install libsdl2-dev` (Ubuntu)
  - Readline: `sudo apt-get install libreadline-dev` (for console app)

### Building

The simplest path is via CMake presets (see `CMakePresets.json` for the list):

```bash
# Configure, build, test
cmake --preset debug              # Debug, no modules; works with clang or gcc
cmake --build --preset debug
ctest --preset debug

# Run
./build/debug/sdl/specbolt_sdl
```

Other useful presets: `debug-modules` (clang 20+ with libc++, or gcc 16+), `release` (RelWithDebInfo, runs the zexdoc
regression tests), and `release-modules`.

To pin a specific compiler, set `CC`/`CXX` or create a local `CMakeUserPresets.json` (gitignored) that inherits a public
preset and overrides `CMAKE_CXX_COMPILER`.

### C++26 reflection

Reflection (P2996) needs **gcc 16+**, or one of the clang forks that implement it: no clang *release* does. Detection is
automatic, including whichever extra flags the compiler wants, so any other compiler simply builds without the
reflective code. The `debug-reflection` and `release-reflection` presets require it and fail to configure otherwise.

If your distro has no gcc 16 package, grab a [Compiler Explorer](https://compiler-explorer.com/) build, the same one
CI uses:

```bash
mkdir -p ~/opt
curl -fsSL https://s3.amazonaws.com/compiler-explorer/opt/gcc-16.2.0.tar.xz | tar Jxf - -C ~/opt
CC=~/opt/gcc-16.2.0/bin/gcc CXX=~/opt/gcc-16.2.0/bin/g++ cmake --preset debug-reflection
```

Barry Revzin's clang fork ([brevzin/llvm-project](https://github.com/brevzin/llvm-project)) builds everything too. Of
the clangs, it is the one that needs no reflection switch beyond `-freflection`; the configure step adds that and the
other flags clang wants (a larger constexpr step budget, and `-Wno-c23-extensions` for `#embed`). Point it at a
libstdc++ new enough for the C++23 library pieces:

```bash
CC=<clang>/bin/clang CXX=<clang>/bin/clang++ cmake --preset debug-reflection \
    -DCMAKE_CXX_FLAGS=--gcc-toolchain=$HOME/opt/gcc-16.2.0
```

It compiles v4 more slowly than gcc does; [z80/v4/notes/MEASUREMENTS.md](z80/v4/notes/MEASUREMENTS.md) has the
numbers and what each compiler needed.

### Web/WASM Build

```bash
# Install WASM dependencies
sudo apt install libc++-20-dev-wasm32 libclang-rt-20-dev-wasm32

# Configure and build with WASI support, into build/wasm
CC=clang-20 CXX=clang++-20 cmake --preset wasm -DSPECBOLT_WASI_SYSROOT=/path/to/wasi-sysroot
cmake --build --preset wasm

# Set up the web environment, pointing it at that build
cd web
npm install
echo "VITE_WASM_BUILD_DIR=$PWD/../build/wasm" > .env.local

# Run development server
npm start
```

The web build uses v2, because stock clang has no reflection. v4 builds for the browser too, with a reflection clang and
`cmake/wasm-reflection.cmake`; [z80/v4/notes/WASM.md](z80/v4/notes/WASM.md) has the recipe.

The `wasm` preset turns the tests off. Configure with `-DSPECBOLT_TESTS=ON` as well and, with `node` on the path,
`ctest` runs them under Node. `node web/tools/boot.mjs build/<dir>/web/spectrum.wasm` boots a build without a browser
and writes the screen out.

## Project Documentation

- [v4](z80/v4/README.md) - The Z80 core generated at compile time from a text description, and where to start reading
- [The `.cpu` format](z80/v4/CPU_FORMAT.md) - Reference for the instruction-set description v4 compiles
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
