## Build

Needs CMake 3.30+, Ninja, SDL2 and readline, and a compiler that suits the preset:

- the plain presets: clang 20+ or gcc 16+;
- the modules presets: clang 20+ with libc++, or gcc 16+ with libstdc++ (CI builds `debug-modules` with both, since
  gcc enforces module rules clang lets through);
- the reflection presets: gcc 16+, or a P2996 clang fork (see below);
- the `wasm` preset: clang with a WASI sysroot; see [README.md](README.md).

Presets live in `CMakePresets.json`. The common ones:

```sh
cmake --preset debug            # Debug, no modules; works with clang or gcc
cmake --preset debug-modules    # Debug with C++ modules (needs clang + libc++)
cmake --preset release          # RelWithDebInfo, no modules (runs zexdoc tests)
cmake --preset debug-reflection # Debug with C++26 reflection (needs gcc 16+ or a P2996 clang)
cmake --preset release-reflection # RelWithDebInfo with reflection; runs zexdoc for v4 too
cmake --build --preset debug
ctest --preset debug
```

Pick the compiler with `CC=… CXX=…` or by setting `CMAKE_CXX_COMPILER` in a local `CMakeUserPresets.json` (gitignored)
that `inherits` from one of the public presets.

The `zexdoc` regression tests are intentionally skipped in `Debug` (too slow); they run in `RelWithDebInfo`.

Run: `./build/debug/sdl/specbolt_sdl`

## C++26 reflection

Reflection (P2996) needs **gcc 16+** or one of the P2996 clang forks; no clang *release* implements it, and the WASI
build is on stock clang, so anything reflective must be optional. `cmake/reflection.cmake` probes for it, including the
extra flags clang wants, and exposes:

- `SPECBOLT_HAS_REFLECTION`: true when the compiler can do it. Exclude reflective targets with
  `if (SPECBOLT_HAS_REFLECTION)`, and guard reflective code on the `SPECBOLT_REFLECTION` macro.
- `SPECBOLT_REFLECTION` cache variable: `AUTO` (default, use if available), `ON` (require it; configure fails
  otherwise), `OFF`.

`-freflection` rides on `opt::c++26` so it reaches every specbolt target uniformly. It's a dialect switch: gcc can't
merge a module built without it into a TU built with it (importing one fails with conflicting declarations for types
reachable both textually and through the module), and it needs `-std=c++26`, so applying it globally breaks third-party
targets built at the default standard.

See [README.md](README.md) for getting a gcc 16 toolchain, or Barry Revzin's clang fork, which also builds everything.

Reflection works inside module interface units on gcc 16 — including `template for` in a module purview, and exported
templates that reflect on their own parameters and get instantiated in importing TUs. Even so, v4 has no modules build
and is not built when modules are on, which is why the reflection presets set `SPECBOLT_MODULES=OFF`: its compiled
description, `z80/v4/Target.hpp`, is a header a modules build would include into more than one partition, duplicating
its definitions. Everything else builds under both.

## v4 and refract

v4 is a Z80 generated at compile time from a text description, `z80/v4/z80.cpu`, by `refract`, a CPU-agnostic library
in `z80/v4/refract/`. Start at [z80/v4/README.md](z80/v4/README.md); [NOTES.md](z80/v4/NOTES.md) says why it is that
shape, `notes/JOURNAL.md` records decisions in order, and `notes/FINDINGS.md` holds dated facts about the compilers.

- `ctest --preset release-reflection -R "v4|refract"` runs its tests: `refract_test` (the library alone) and
  `z80_v4_test`. zexdoc for every core runs in the RelWithDebInfo presets.
- refract is its own CMake target, and `refract_test` builds it with nothing of the Z80 on the include path. Its
  second machine is a documented NMOS 6502 in `z80/v4/refract/test/m6502/`, so a change to refract must keep both
  machines working.
- refract knows no Z80 facts. Naming the Z80 as a marked example is fine; stating a rule in its vocabulary, or a
  number counted from its description, is not. Run the `refract-generality-auditor` agent (`.claude/agents/`) after
  editing anything under `refract/`.
- refract's parser and generator run during constant evaluation, where a throw is the compile error. Don't add
  `try_` APIs, error returns or guards against what constant evaluation already diagnoses; context comes from the
  `naming` and `at_line` wrappers. Diagnostic tests use `throws_with` and are skipped where
  `__cpp_constexpr_exceptions` is missing (the clang fork, for now).
- What a compiler does or fails to do goes in `notes/FINDINGS.md`, dated, never in a code comment. Code that bends
  around clang sits behind `REFRACT_CLANG_WORKAROUNDS` (`refract/Workarounds.hpp`) with an entry in `notes/WASM.md`.
- Measure speed in time (ns per instruction), not instruction counts; `notes/MEASUREMENTS.md` has the method. Don't
  time builds unless asked.

## Lint/Format

`pre-commit run --all-files`: clang-format does the heavy lifting, and `tools/format_cpu_table.py` aligns the columns
of `.cpu` tables. It checks only tracked files, so run `pre-commit run --files <path>...` on new files before staging
them.

## Style

See [STYLE_GUIDE.md](STYLE_GUIDE.md). Quick reminders:

- British English (`colour`, not `color`)
- `const` by default, if-init where it tightens scope
- PascalCase types, snake_case functions/variables, 120-col, 2-space indent
- Comments wrap at 120 columns too, like the code. A comment on a function, type or template says what it does first;
  preconditions, history and justification come after
- No hard-coded counts in comments or notes ("the 26 rows"): name the constant, or say it without the number
- Catch2 (`TEST_CASE` / `SECTION`) for tests

Modules-specific rules, both enforced by gcc and silently accepted by clang:

- In any TU, put every `#include` **before** the first `import` (including imports a project header performs for you).
  gcc merges the module's global module fragment on import, and a standard header pulled in afterwards redefines what
  the module already supplied.
- Code `#include`d into a module interface partition must not put entities named by templates in an anonymous namespace:
  a template attached to a module can't name a TU-local entity.
