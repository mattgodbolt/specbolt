# v4

v4 is a Z80 core whose instruction set is not written in C++. It is a text file,
[`z80.cpu`](z80.cpu), which `Target.hpp` `#embed`s, and refract, a C++26 reflection library, parses and
checks it during constant evaluation and generates both an interpreter and a disassembler from it. A malformed
description is a compile error naming its line.

## What is where

- **`refract/`** is the library, the `refract` CMake target, with its headers in
  `refract/include/refract/` and its own tests in `refract/test/`. It knows no CPU: it reads a
  `.cpu` description, checks it, and generates code against whatever machine a target names. Its
  tests build with nothing of the Z80 on the include path, so the build checks that it needs none,
  and they include a second machine, a 6502 in `refract/test/m6502/`
  ([notes/6502.md](notes/6502.md)).
- **Everything else here is the Z80:**
  - `z80.cpu`, the description;
  - `Target.hpp`, which embeds it and names the machine and its palettes of verbs;
  - `Operations.hpp`, the palette of verbs that touch no machine state;
  - `include/z80/v4/Z80.hpp` and `Z80.cpp`, the machine: its state, the verbs it publishes, and what
    the library calls on it;
  - `Disassembler.cpp`, which tells the library's disassembler where the bytes come from;
  - `test/`, the unit tests of the Z80 as v4 builds it.

## Reading order

1. [CPU_FORMAT.md](CPU_FORMAT.md), the Overview: what a description says, and what it leaves to the
   CPU.
2. [`z80.cpu`](z80.cpu), the description itself.
3. [`Target.hpp`](Target.hpp), the whole of the Z80's side of the contract in one place.
4. [`refract/include/refract/Execute.hpp`](refract/include/refract/Execute.hpp): the "Reading order"
   comment near the top, then the file in that order.

Then [NOTES.md](NOTES.md) for why it is that shape, and what is still open.

## Building and testing

v4 needs C++26 reflection, so gcc 16.2 or Barry Revzin's clang fork; the top-level
[README.md](../../README.md) says how to get either. It is built only when the compiler has
reflection and modules are off, which is what the reflection presets set:

```sh
CC=~/opt/gcc-16.2.0/bin/gcc CXX=~/opt/gcc-16.2.0/bin/g++ cmake --preset debug-reflection
cmake --build --preset debug-reflection
ctest --preset debug-reflection -R "v4|refract"
```

The filter runs `refract Unit Tests` (the `refract_test` binary, the library on its own) and `Z80 v4
Unit Tests` (`z80_v4_test`, the Z80 built from it). Without the filter, `ctest` also runs
`Z80 Opcode and Boot Tests`, the shared suite in `z80/test`, which covers every core the build has,
v4 included. Use `release-reflection` for the zexdoc regression test too
(`Z80 Regression Test (v4 implementation)`), which is skipped in Debug as too slow. `Z80.cpp` is the slowest translation unit in the repository
to compile; [notes/MEASUREMENTS.md](notes/MEASUREMENTS.md) says why.

## The notes

| | |
|---|---|
| [NOTES.md](NOTES.md) | what v4 does today, where the library/CPU boundary sits, and what is open |
| [CPU_FORMAT.md](CPU_FORMAT.md) | the reference for the `.cpu` format |
| [notes/FINDINGS.md](notes/FINDINGS.md) | what C++26 actually did on a real compiler |
| [notes/MEASUREMENTS.md](notes/MEASUREMENTS.md) | speed, build cost and what accuracy buys, each dated with its method |
| [notes/PREFIXES.md](notes/PREFIXES.md) | a dated record of how the prefix design was reached |
| [notes/WASM.md](notes/WASM.md) | getting v4 into the browser |
| [notes/6502.md](notes/6502.md) | the 6502, refract's second machine, as a test of the format |
| [notes/JOURNAL.md](notes/JOURNAL.md) | what was decided and why, in order |
