## Build

Requires **clang-20+** (for C++26 modules + libc++), CMake 3.30+, Ninja, SDL2, readline.

Presets live in `CMakePresets.json`. The common ones:

```sh
cmake --preset debug            # Debug, no modules — works with clang or gcc
cmake --preset debug-modules    # Debug with C++ modules (needs clang + libc++)
cmake --preset release          # RelWithDebInfo, no modules (runs zexdoc tests)
cmake --preset debug-reflection # Debug with C++26 reflection (needs gcc 16+)
cmake --build --preset debug
ctest --preset debug
```

Pick the compiler with `CC=… CXX=…` or by setting `CMAKE_CXX_COMPILER` in a local `CMakeUserPresets.json` (gitignored) that `inherits` from one of the public presets.

The `zexdoc` regression tests are intentionally skipped in `Debug` (too slow); they run in `RelWithDebInfo`.

Run: `./build/debug/sdl/specbolt_sdl`

## C++26 reflection

Reflection (P2996) needs **gcc 16+**; no clang release implements it yet, and the WASI build is on clang, so anything reflective must be optional. `cmake/reflection.cmake` probes for it and exposes:

- `opt::reflection` — link against it to get `-freflection` and the `SPECBOLT_REFLECTION` define. Guard reflective code on that macro, or exclude the target in CMake with `if (SPECBOLT_HAS_REFLECTION)`.
- `SPECBOLT_REFLECTION` cache variable — `AUTO` (default, use if available), `ON` (require it; configure fails otherwise), `OFF`.

No distro packages gcc 16, so grab a compiler-explorer build — the same thing CI uses:

```sh
mkdir -p ~/opt && curl -fsSL https://s3.amazonaws.com/compiler-explorer/opt/gcc-16.2.0.tar.xz | tar Jxf - -C ~/opt
CC=~/opt/gcc-16.2.0/bin/gcc CXX=~/opt/gcc-16.2.0/bin/g++ cmake --preset debug-reflection
```

Reflection presets set `SPECBOLT_MODULES=OFF`; combining reflection with modules is untested.

## Lint/Format

`pre-commit run --all-files` — clang-format does the heavy lifting.

## Style

See [STYLE_GUIDE.md](STYLE_GUIDE.md). Quick reminders:

- British English (`colour`, not `color`)
- `const` by default, if-init where it tightens scope
- PascalCase types, snake_case functions/variables, 120-col, 2-space indent
- Catch2 (`TEST_CASE` / `SECTION`) for tests

Modules-specific rules, both enforced by gcc and silently accepted by clang:

- In any TU, put every `#include` **before** the first `import` (including imports a project header performs for you). gcc merges the module's global module fragment on import, and a standard header pulled in afterwards redefines what the module already supplied.
- Code `#include`d into a module interface partition must not put entities named by templates in an anonymous namespace — a template attached to a module can't name a TU-local entity.
