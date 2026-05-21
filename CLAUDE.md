## Build

Requires **clang-20+** (for C++26 modules + libc++), CMake 3.30+, Ninja, SDL2, readline.

Presets live in `CMakePresets.json`. The common ones:

```sh
cmake --preset debug           # Debug, no modules — works with clang or gcc
cmake --preset debug-modules   # Debug with C++ modules (needs clang + libc++)
cmake --preset release         # RelWithDebInfo, no modules (runs zexdoc tests)
cmake --build --preset debug
ctest --preset debug
```

Pick the compiler with `CC=… CXX=…` or by setting `CMAKE_CXX_COMPILER` in a local `CMakeUserPresets.json` (gitignored) that `inherits` from one of the public presets.

The `zexdoc` regression tests are intentionally skipped in `Debug` (too slow); they run in `RelWithDebInfo`.

Run: `./build/debug/sdl/specbolt_sdl`

## Lint/Format

`pre-commit run --all-files` — clang-format does the heavy lifting.

## Style

See [STYLE_GUIDE.md](STYLE_GUIDE.md). Quick reminders:

- British English (`colour`, not `color`)
- `const` by default, if-init where it tightens scope
- PascalCase types, snake_case functions/variables, 120-col, 2-space indent
- Catch2 (`TEST_CASE` / `SECTION`) for tests
