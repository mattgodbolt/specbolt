# C++26, as found

What the language actually did when v4 leaned on it, all verified on gcc 16.2 rather than
read in a paper. **New language facts go here**, whether or not they changed the design.

Part of [v4's notes](../NOTES.md).

---

## C++26 findings (all verified on gcc 16.2)

Hard-won and easy to forget. Each of these cost a debugging cycle.

### Reflection

- **`-freflection` is a language dialect switch, not a per-target option.** gcc cannot merge a module
  built without it into a TU built with it, importing one fails with conflicting declarations for
  types reachable both textually and through the module. It also requires `-std=c++26`. It therefore
  lives on `opt::c++26`, which every specbolt target links; applying it globally instead breaks
  third-party targets that build at the default standard.
- **`std::meta::info` is a consteval-only type.** It cannot be stored in anything that survives to
  runtime, a struct containing one becomes consteval-only, so *any* runtime use of that struct
  (including reading an unrelated `int` member) is ill-formed. Resolve reflections **inside** the
  splice: `[: find_operation(name, line) :]`, never `[: stored.fn :]`.
- **`identifier_of` throws on members without identifiers** (constructors, etc). Guard with
  `has_identifier` before comparing names, or the exception pre-empts your own diagnostic.
- **Reflection must live in template arguments and alias templates, never in a local.** A
  `constexpr auto parameters = define_static_array(parameters_of(Fn));` inside a function body is an
  immediate-escalating expression: it promotes the enclosing function to `consteval`, which then
  cannot be called with runtime CPU state. Use `template<info Fn> constexpr auto arity_of = …` and
  `template<info Fn, size_t I> using parameter_type = typename[:type_of(parameters_of(Fn)[I]):]`
  instead. This is the sharp edge of the consteval-only rule and it is easy to trip over twice.
- **`access_context::current()` at namespace scope excludes private members.** This is why `Ops` is a
  struct with a private section rather than a namespace: access control gates which names the table
  may use as verbs. Deliberate and worth keeping.
- **Released clang has no reflection at all**, 22.1 and trunk both lack `<meta>`. The wasm build is
  clang, so v4 is excluded in CMake via `if (SPECBOLT_HAS_REFLECTION)` rather than by `#ifdef`s in
  source. Bloomberg's P2996 fork is a different matter. See "The other implementation" below.
- Reflection works inside module interface units, including `template for` in a module purview and
  exported templates that reflect on their own parameters and are instantiated in importing TUs.

### Structural types and static promotion

The single most useful architectural fact:

- **`define_static_array` requires *structural* types.** `std::string_view` is not (private members),
  nor is `std::span`, nor anything holding a `std::vector`.
- **A pointer *into* another constexpr array is not an acceptable reflected constant.** Building
  `{const char*, size_t}` pointing into the `#embed`ed blob fails with `reflect_constant failed`.
  Each string needs its own storage via `define_static_string`.
- **But `constexpr std::array<T, N>` needs no structural type at all.** Structural is a
  `define_static_array` requirement, not a constexpr one, so an array sidesteps the whole problem and
  lets `Row`/`Vocabulary` keep plain `string_view`s into the blob.
- Transient allocation is fine: a `std::vector` may be created and destroyed inside one constant
  evaluation and passed between `consteval` functions freely. It just cannot escape into a
  namespace-scope `constexpr` variable.
- **So the parse works in `std::vector` throughout and an array is made of the answer at the end.**
  Getting the size means evaluating the whole parse twice, once for `.size()`, once for the contents
 , which is `to_array` in `ToArray.hpp`, and it is the only place in the pipeline that knows a count.
  The earlier arrangement counted matching lines in a cheap pre-pass and passed the count as a
  template argument to each parse function; that had to be right in two places, and it made every
  parse function a template with a capacity check nobody could reach. **What the second parse costs,
  measured** (alternating A/B, twice each side, gcc 16.2 `-O0`): on `Disassembler.cpp`, which is the
  parse plus every check and nothing else, this one change took 9.3s/387MB to 12.6s/570MB, about
  **+3.2s and +180MB**. On `Z80.cpp`, which is the same parse plus 1792 handler instantiations, 75.4s
  became 73.2s: the same work, lost in the noise of what dominates that TU. Three seconds for a
  pipeline in which one function knows a count. (For where those absolutes stand today, after the
  rest of the clarity work, see "Compile time, measured".)
- **Growing a `std::vector` during constant evaluation is much dearer than growing one at run time.**
  `instructions_of` builds a 1792-element vector for the checks to walk; adding a `reserve` for it
  took about a second off `Disassembler.cpp`. The evaluator has no `realloc`, every growth copies
  every element through the interpreter, so `reserve` is worth writing wherever the size is known,
  which in a parse it usually is.
- `define_static_string` still earns its place for *generated* text, where the bytes must outlive the
  evaluation.

Note the contrast with advice that a `string_view` into the `#embed`ed blob "is trivially structural
and survives promotion". It is neither, and both halves were verified false.

### Expansion statements

- **`template for` + `-Wshadow` is a gcc bug, half fixed** ([PR c++/124197][pr124197]). Each expanded
  copy is reported as shadowing the previous, though nothing is shadowed: every copy is its own scope.

  16.2 fixed it **in dependent contexts only**. An expansion statement inside a template is clean; the
  same statement in a non-dependent context still errors. Verified on the gcc 16.2 this builds
  against, one file, both forms, `-Wshadow -Werror`:

  ```cpp
  inline constexpr auto non_dependent = [] {                      // 4 errors
    std::array<int, 4> out{};
    template for (constexpr auto at: std::views::iota(0uz, 4uz)) out[at] = static_cast<int>(at);
    return out;
  }();

  template<int N> constexpr auto dependent() {                    // clean
    std::array<int, 4> out{};
    template for (constexpr auto at: std::views::iota(0uz, 4uz)) out[at] = static_cast<int>(at) + N;
    return out;
  }
  ```

  **An earlier version of this note said it was fixed outright**, on the evidence that removing all six
  `#pragma GCC diagnostic ignored "-Wshadow"` lines rebuilt clean. That evidence was real and the
  conclusion was wrong: all six were inside templates. `-Wshadow -Werror` is exactly this project's
  setting, which is why the pragmas existed at all, and it is still why `all_dispatches` is a pack
  rather than an expansion statement, that one initialiser is not dependent. `Execute.hpp` said so
  all along; this file contradicted it.

  [pr124197]: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=124197
- The range must be a constant expression, and for a range that means a constant *address*, not
  merely a constant value. A plain `constexpr auto row = …;` local does not qualify, gcc says so
  precisely: "address of non-static constexpr variable may differ on each invocation of the enclosing
  function; add `static`". `static constexpr` fixes it, and a namespace-scope `inline constexpr` or a
  template parameter object needs nothing. This is why `execute_one`'s `row` is `static`: expanding
  over `row.steps` directly is what lets the step be the loop variable rather than an index into it.
- **There is no `template switch`.** An expansion statement generates statements, and a `case`
  label is not one, so a 256-way dispatch cannot be expanded into a `switch`. The generated forms
  available are a table of function pointers (what `dispatch` does) or a chain of `if`s. A
  `switch` over a dense contiguous range is the one shape the compiler turns into a jump table on
  its own, so it is exactly the shape reflection cannot reach, worth knowing before assuming
  generated dispatch matches a hand-written interpreter's codegen.

### Toolchain

- gcc 16.2 from the compiler-explorer tarball. No distro packages gcc 16; CI pulls the same tarball.
- Binaries built with an out-of-prefix toolchain bind to the distro's `libstdc++` unless an rpath is
  embedded, resolve the standard library actually being linked and add its directory.
- **ccache's direct mode does not track `#embed` dependencies** and serves stale objects when only
  the embedded file changes. Worked around with `CCACHE_DEPEND=1`. Upstream fix is PR ccache#1765,
  merged 2026-07-19 but not in any release yet; delete the workaround when it ships.
- gcc enforces several module rules clang lets through: every interface partition must be re-exported
  from the primary module interface; textual `#include`s must precede all `import`s in a TU; and code
  `#include`d into a module interface partition must not put entities named by module-attached
  templates in an anonymous namespace.

---
