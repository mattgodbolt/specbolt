## Build

Requires **clang-20+** (for C++26 modules + libc++), CMake 3.30+, Ninja, SDL2, readline.

Default (modules + libc++):

```sh
CC=clang-20 CXX=clang++-20 cmake -B build/Debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/Debug
```

Modules off (also works with gcc-15):

```sh
cmake -B build/DebugNoModules -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSPECBOLT_MODULES=OFF
cmake --build build/DebugNoModules
```

Run: `./build/Debug/sdl/specbolt_sdl`
Tests: `ctest --test-dir build/Debug`

The `zexdoc` regression tests are intentionally skipped in `Debug` (too slow); they run in `RelWithDebInfo`.

## Lint/Format

`pre-commit run --all-files` — clang-format does the heavy lifting.

## Style

See [STYLE_GUIDE.md](STYLE_GUIDE.md). Quick reminders:

- British English (`colour`, not `color`)
- `const` by default, if-init where it tightens scope
- PascalCase types, snake_case functions/variables, 120-col, 2-space indent
- Catch2 (`TEST_CASE` / `SECTION`) for tests
