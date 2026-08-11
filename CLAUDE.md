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

- `SPECBOLT_HAS_REFLECTION` — true when the compiler can do it. Exclude reflective targets with `if (SPECBOLT_HAS_REFLECTION)`, and guard reflective code on the `SPECBOLT_REFLECTION` macro.
- `SPECBOLT_REFLECTION` cache variable — `AUTO` (default, use if available), `ON` (require it; configure fails otherwise), `OFF`.

`-freflection` rides on `opt::c++26` so it reaches every specbolt target uniformly. It's a dialect switch: gcc can't merge a module built without it into a TU built with it (importing one fails with conflicting declarations for types reachable both textually and through the module), and it needs `-std=c++26`, so applying it globally breaks third-party targets built at the default standard.

See [README.md](README.md) for getting a gcc 16 toolchain.

Reflection works inside module interface units on gcc 16 — including `template for` in a module purview, and exported templates that reflect on their own parameters and get instantiated in importing TUs. The reflection presets set `SPECBOLT_MODULES=OFF`, and v4 is not built when modules are on: its table is a header included into more than one module partition, so its definitions duplicate. Everything else builds under both.

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
