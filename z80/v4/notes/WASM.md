# v4 in the browser

The web build is clang targeting `wasm32-wasip1`, and gcc has no WebAssembly backend, so v4 reaches the browser only
through a clang that implements reflection. Released clang does not, so this is Barry Revzin's fork
([brevzin/llvm-project](https://github.com/brevzin/llvm-project)), which is LLVM and so has the wasm backend already.
This file is the log of getting there, in the order things were found, and the recipe that came out of it. Started
2026-09-22.

Part of [v4's notes](../NOTES.md).

---

## Where it stands

**It works, with one patch to the fork.** On 2026-09-22, v4 built for `wasm32-wasip1`:

- passes every assertion of `z80_v4_test` under Node 24's WASI, through `ctest`;
- boots the 48K and 128K ROMs in the web front end's own `spectrum.wasm`, driven headlessly by `web/tools/boot.mjs`,
  at about 0.24 ms of wasm time per emulated 20 ms frame;
- runs zexdoc at 25.5 ns per instruction under Node, against 9.6 ns for the same source natively under gcc 16.2
  (`z80_bench_v4 -r 5`, one run each, adjacent, same machine; the native run's spread was 24%, so read its figure
  loosely).

Not yet seen in a real browser: the Chrome available to this session could not reach the dev server. The browser adds
only `@bjorn3/browser_wasi_shim` in place of Node's WASI and its own engine's tail calls and exceptions, both of which
current engines have. zexdoc's *correctness* under wasm is also unchecked, because `zexdoc_test` links v3, which does
not build for wasm; the benchmark runs the same program without checking its answers.

What it needs from the fork is below under "For Barry": a crash with a one-line fix, which blocks wasm outright, and
bugs we work around on our side.

## The recipe

1. **A reflection clang that handles 32-bit targets.** Barry's fork at `compiler-explorer/barry` with the one-line
   `SemaExpand.cpp` change in item 7 below, built with `clang` and `lld` for the X86 and WebAssembly targets:

   ```sh
   git clone --depth 1 --branch compiler-explorer/barry https://github.com/brevzin/llvm-project.git
   # apply the SemaExpand.cpp change
   cmake -S llvm -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DLLVM_ENABLE_ASSERTIONS=ON \
       -DLLVM_ENABLE_PROJECTS="clang;lld" -DLLVM_TARGETS_TO_BUILD="X86;WebAssembly" \
       -DCMAKE_INSTALL_PREFIX=$HOME/opt/barry-patched -DLLVM_INCLUDE_TESTS=OFF
   ninja -C build install-clang install-clang-resource-headers install-lld
   ```

   Fourteen minutes for clang on 36 cores, forty seconds more for lld. `lld` has to come from the same build: the
   project links with thin LTO, and wasi-sdk's `wasm-ld` (LLVM 23.1) refuses the fork's bitcode ("Unknown module asm
   property (Producer: 'LLVM23.0.0git' Reader: 'LLVM 23.1.0-wasi-sdk')"). Then `ln -s lld wasm-ld` and symlinks to
   wasi-sdk's `llvm-ar`, `llvm-ranlib` and `llvm-nm` in the install's `bin`: the WebAssembly driver ignores
   `--ld-path` and looks for `wasm-ld` beside itself, and CMake's IPO support looks for the archiver there too. Copy
   wasi-sdk's `libclang_rt-34.0/wasm32-unknown-wasip1/libclang_rt.builtins.a` into
   `$(clang++ -print-resource-dir)/lib/wasm32-unknown-wasip1/`.
2. **wasi-sdk 34** (`wasi-sysroot-34.0.tar.gz`, `libclang_rt-34.0.tar.gz`, and the x86_64 Linux SDK for its binutils,
   from the GitHub release). It is built from LLVM 23.1 and ships libc++ with exceptions under
   `lib/wasm32-wasip1/eh`.
3. **libc++ headers with `<meta>`.** wasi-sdk's have none, because upstream has no reflection. Copy the fork's
   `include/c++/v1`, drop wasi-sdk's `include/wasm32-wasip1/eh/c++/v1/__config_site` over its own, and in
   `__locale_dir/locale_base_api.h` wrap the `#include <__locale_dir/locale_base_api/ibm.h>` in the fallback branch in
   `#if defined(__MVS__)`. Without that, WASI takes the fallback, and `ibm.h` redeclares `strtod_l` and friends with an
   `abi_tag` ("cannot add 'abi_tag' attribute in a redeclaration"); upstream 23.1 has the guard and the fork's headers
   predate it. The two `__config_site`s differ in ABI version (wasi-sdk's is `__2`), threads, time zone database and
   default hardening, none of which `<meta>` depends on.
4. **Configure** with `cmake/wasm-reflection.cmake`, which says why each of its flags is there:

   ```sh
   cmake -B build/wasm-v4 -G Ninja --toolchain cmake/wasm-reflection.cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo \
       -DSPECBOLT_WASM=ON -DSPECBOLT_MODULES=OFF -DSPECBOLT_TESTS=ON -DSPECBOLT_REFLECTION=ON \
       -DSPECBOLT_REFLECTION_CLANG=$HOME/opt/barry-patched -DSPECBOLT_WASI_SYSROOT=$HOME/opt/wasi-sysroot-34.0 \
       -DSPECBOLT_WASM_LIBCXX_HEADERS=$HOME/opt/wasm-reflect/include/c++/v1
   cmake --build build/wasm-v4 --target z80_v4_test spectrum.wasm z80_bench_v4
   ctest --test-dir build/wasm-v4 -R v4
   node web/tools/boot.mjs build/wasm-v4/web/spectrum.wasm --model 128
   ```

   About three minutes for those targets. For the front end, `VITE_WASM_BUILD_DIR=build/wasm-v4 npm start` in
   `web/`.

## What the project needed

- **v4 is built under `SPECBOLT_WASM` whenever the compiler has reflection** (`z80/CMakeLists.txt`); v3 still is not.
  The stock wasm build has no reflection, so it is unaffected.
- **The web front end uses v4 when the build has it**, and v2 otherwise (`web/CMakeLists.txt`, `web/main.cpp`). Its
  stand-ins for `__cxa_allocate_exception` and `__cxa_throw` now apply only without `__cpp_exceptions`: with
  exceptions the real ones are linked, and the stand-ins would take their place.
- **The top-level `-target wasm32-wasi --sysroot` applies only without a toolchain file.** It is added before
  `project()`, so it overrode the toolchain's `wasm32-wasip1`, and wasi-sdk 34 keeps its libraries under
  `wasm32-wasip1`.
- **Tests are no longer forced off for wasm**; the `wasm` preset turns them off instead, so a build can ask for them.
  With Node on the path, `CMAKE_CROSSCOMPILING_EMULATOR` runs them through `web/tools/run-wasi.mjs`.
- **Flags, all in the toolchain file:** `-fwasm-exceptions`, because refract throws during constant evaluation and a
  `throw` does not compile without exceptions, even one only ever reached at compile time; `-mtail-call`, because
  without it clang does not recognise `[[clang::musttail]]` on wasm at all, and the run loop is nothing but mandatory
  tail calls; `-Wl,-mllvm,-wasm-enable-eh`, because under LTO the link does the code generation and does not otherwise
  know to lower exceptions for wasm, so every `throw` became uncatchable (the unit tests that expect a throw died with
  an uncaught `WebAssembly.Exception`, and everything else passed); `-Wl,-u,__main_argc_argv`, because WASI's start
  code refers to `main` only weakly, which does not pull Catch2's `main` out of its archive (`RuntimeError:
  unreachable` in `undefined_weak:main`); and `CATCH_CONFIG_NO_POSIX_SIGNALS`, since WASI has no signals.

## The fork on the host, first

The notes record a clean native build with the fork in May. A lot has changed since, so the first job was to repeat
it natively with `cmake --preset release-reflection -B build/barry23` and the fork as `CC`/`CXX`. Each of these would
have been found identically under wasm.

The compiler was Compiler Explorer's nightly, `https://s3.amazonaws.com/compiler-explorer/opt/clang-barry-clang-trunk-YYYYMMDD.tar.xz`
(20260922, clang 23.0.0git, 3d0f86e4). The copy under `/opt/compiler-explorer` on this machine is from May and is
clang 21. The fork is based on upstream LLVM, not Bloomberg's clang-p2996: its branch shares history with upstream
`main` up to 2026-06-01, is 505 commits ahead of that point, and is some 15,800 behind `main` today, so an upstream fix
made since June is not in it until Barry merges.

1. **The fork crashes on `is_structural_type` of a nested class of a class template** while the template is being
   instantiated: the nested class's definition has not been instantiated yet, and the fork asks the incomplete class
   for its properties instead of completing it (`Assertion 'DD && "queried property of class with no definition"'`
   in `Type::isStructuralType`). gcc completes it and answers. `Interpreter` asserts this of its nested `Call`.
   Workaround: `static_assert(sizeof(Call) > 0)` first. [Reduced](https://compiler-explorer.com/z/M9GohWEEa).
2. **`[[gnu::musttail]]` is unknown to clang.** clang's spelling is `[[clang::musttail]]`, which gcc accepts as well,
   so the tail calls now use that. Not a workaround: it is the spelling both compilers take.
3. **`std::function_ref` is not in libc++.** `disassemble` takes a constrained `auto` parameter instead when
   `__cpp_lib_function_ref` is not defined.
4. **Two warnings, which `-Werror` makes errors.** Lambdas in `execute_one` captured `machine` by name where only one
   `if constexpr` branch uses it, which clang reports as an unused capture: the thing the May build found, come back.
   `[&]` is the answer for both compilers. Separately, the `Byte`/`Word` aliases in `check_destinations_fit` are
   reported `-Wunused-local-typedef`, because the fork does not count a reflect-expression as a use of an alias; they
   are `[[maybe_unused]]` under clang.
5. **v3's generator cannot run** (`libc++.so.1: cannot open shared object file`). The top-level CMake bakes in an
   rpath for the standard library it assumes, which is libstdc++ unless modules are on, and the fork defaults to
   libc++. Host-only; `LD_LIBRARY_PATH=<fork>/lib/x86_64-unknown-linux-gnu` gets past it, and it does not touch v4.
6. **`set`, `res` and `bit` under `cb` used bit 0 whatever the opcode said.** Every other assertion passed. The
   resolved operand was right at compile time (from the opcode, with the slice's shift and mask) and the opcode was
   right at run time, but `Op.slice.extract(decoded.opcode)` returned 0. Reduced to a short C++20 program with no
   reflection in it: calling a member function on a subobject of a class-type template argument that
   has a base class reads the wrong bytes. `Resolved` gained its `Access` base on 2026-09-21, which is why this did not
   show in May. clang 19 to 22.1 get it wrong, 23.1 and trunk get it right, and gcc always did; the fork's merge base
   predates the fix. Workaround: copy the slice into a local `constexpr` before calling `extract`.
   [Reduced](https://compiler-explorer.com/z/YzPo69Tzr).

With those, `z80_v4_test` passes every assertion under the fork natively, and under gcc 16.2 unchanged.

## Then wasm

7. **Every expansion statement crashes the fork on a 32-bit target**, wasm32 and `-m32` alike: `Assertion
   'V.getBitWidth() == C.getIntWidth(type)'` in `IntegerLiteral`. Iterating, enumerating (`{1, 2, 3}`) and
   destructuring forms all do it, inside a template. `SemaExpand.cpp` builds the expansion index as
   `llvm::APSInt::get(Instantiations.size())`, which is a 64-bit value, and gives it type `size_t`, which is 32 bits
   there. `Context.MakeIntValue(Instantiations.size(), Context.getSizeType())` sizes it for the target, and with that
   change the fork compiles all of v4 for wasm. There is no reasonable workaround on our side: expansion statements
   are the interpreter's core. [Reduced](https://compiler-explorer.com/z/T7eExY4oh).
   Upstream clang has expansion statements only in part, and refuses the iterating form ("iterating expansion
   statements are not yet supported"), so the fork is the only way there for now.
8. Everything after that was toolchain plumbing, recorded under "The recipe" and "What the project needed": the
   libc++ header guard, the linker and archiver from the same build, and the link and Catch2 settings.

## The workarounds, and how to find them

Every change made for clang's sake that is not simply better code is behind `REFRACT_CLANG_WORKAROUNDS`, defined in
`refract/Workarounds.hpp` as whether the compiler is clang, with the straightforward code in the other branch. The
one that is a library gap rather than a compiler bug tests the standard feature macro and names
`REFRACT_CLANG_WORKAROUNDS` in a comment, so a single search finds all of them. Build with
`-DREFRACT_CLANG_WORKAROUNDS=0` to try the straightforward code on a newer fork.

| where | what it avoids | gone when |
|---|---|---|
| `Execute.hpp`, before the `is_structural_type(^^Call)` assert | item 1, the crash | the reduced case compiles |
| `Execute.hpp`, `direct_value_of` | item 6, the wrong bit | the fork merges upstream past the fix |
| `Execute.hpp`, `check_destinations_fit` | item 4, the alias warning | the fork counts `^^` as a use |
| `Disassemble.hpp`, `disassemble` | item 3, no `function_ref` | libc++ defines `__cpp_lib_function_ref` |

## For Barry

The reports, each reduced and each checked against gcc 16.2:

1. Every expansion statement crashes with a 32-bit `size_t`, with the one-line fix (item 7). This is the one that
   blocks wasm.
2. `is_structural_type` of a not-yet-instantiated nested class crashes (item 1).
3. The fork lacks an upstream fix for member calls on template-argument subobjects with a base (item 6); a merge of
   upstream `main` would bring it in, along with the `__MVS__` guard in `locale_base_api.h`.
4. Found 2026-09-25 while trying alternatives to passing `Call` by value (FINDINGS.md): a `static constexpr` local
   declared inside `template for` and used as a reference template argument gives every expansion the first
   expansion's instantiation. Silent wrong code; gcc 16.2 gets it right. [Reduced](https://compiler-explorer.com/z/EqEc1sfjh).
   The same exploration found the fork crashing on a lambda inside `template for` used as a closure-type carrier;
   that one was not reduced.
