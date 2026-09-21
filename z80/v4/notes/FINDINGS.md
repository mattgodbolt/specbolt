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
- **`^^std::uint8_t` is ill-formed.** A reflect-expression may not name a using-declarator, and
  libstdc++ brings the fixed-width integers into `std` with `using ::uint8_t;`. gcc 16.2 says
  "'^^' cannot be applied to a using-declaration". Reflect an alias of your own instead
  (`using Byte = std::uint8_t; ^^Byte`) and compare after `dealias` on both sides, since the alias
  reflects as itself and `type_of` a parameter may answer with `unsigned char`. Found 2026-09-21
  moving the bus-width checks into `destinations_fit`.

### Annotations on member functions

- **An annotation goes on a member function as readily as on an enumerator**, in its own attribute
  list after `[[nodiscard]]`, and `annotations_of` finds it. `[[=refract::operation]]` is how a
  machine publishes a member a description may name. `parameters_of` a non-static member does not
  count the implicit object, and `machine.[:Fn:](args...)` calls it; `is_class_member` with
  `is_static_member` says which of the two call forms a function wants. A namespace-scope constant
  named for the annotation must not share its name with any local, since `-Wshadow` sees through
  the attribute. `members_of` does not walk base classes, so a marked member of a base is not found.
- **`^^Alias` reflects the alias, not what it names.** `parent_of(^^Z80::delay) == ^^Machine` is
  false when `Machine` is `using Machine = Z80;`, and quietly so; `dealias(^^Machine)` is what to
  compare against. `members_of` dealiases for itself, which is why the same alias works there and
  hides the mistake next door.
- **`a == ^^T && b` does not mean what it says.** The line was

  ```cpp
  static constexpr bool machine_member = std::meta::parent_of(Fn) == ^^Machine && !std::meta::is_static_member(Fn);
  ```

  and gcc said `expected ';' before '!' token`. `^^` takes a type-id and the grammar reads
  `Machine &&` as one, an rvalue reference to `Machine`, so the `&&` that was meant to join two
  comparisons is eaten and the parse fails at the `!`. A reflection is not a `bool` and never
  converts to one, so nothing was being tested for truth; parenthesise the comparison.

### Constant evaluation can catch

- **`try`/`catch` works in constant evaluation on gcc 16.2** (P3068), including throwing a new
  exception from the handler. `Compiled` uses it to put the description's file name in front of a
  message the parser threw with only a line, so the library never has to know what file it is
  reading. The idiom that a mistake in the description is a thrown `consteval` exception survives:
  nothing catches the rethrow.

### Library, on libstdc++ 16

- `std::function_ref` and `std::copyable_function` are there; `disassemble` takes the former.
- `std::optional<T&>` is there, which an earlier note here had said it was not. `Description::row_for`
  and `rule_for` still return pointers and could return one.
- `std::format` is not usable in constant evaluation, so `decimal` stays on `std::to_chars`.
### Why refract has its own `Vector`, and what it would take to use `std::inplace_vector`

`Vector<T, N>` in Vector.hpp is a `std::array<T, N>` and a count, with `try_push_back`. It exists
because `std::inplace_vector`, which is exactly the right container for a parser that knows its
limits, fails two requirements this library has. One is permanent and one is temporary, and they
fall on different uses.

**Requirement 1, structural: permanent.** `Call` in Execute.hpp is a non-type template parameter:
every generated step is `apply<Fn, Call>`, and the `Call` holds two `Vector<Resolved, 4>`. A class
type used that way must be *structural* ([temp.param]/7): every base and every non-static data
member public, non-mutable, and itself structural, recursively. `std::inplace_vector` keeps its
storage and its size private, so it is not structural and no paper proposes that it should be. To
use it there the design would have to stop passing a `Call` by value: pass an index into a
`constexpr` table of calls instead and look the object up inside, which is the trick the journal
already records for `string_view`. That is a real change to the generator's shape, not a swap of
containers.

**Requirement 2, constant evaluation of a non-trivial element type: temporary.** Seven of the
twelve uses hold `Piece`, `Operand`, `Member`, `Rule` or `Step`, each of which carries a
`std::string_view` into the description or a `Name`, so none is trivial. They are built during the
parse and fixed into `Compiled`'s arrays, which the disassembler walks at run time. The history:

- C++26 as first adopted, P0843R14, [inplace.vector.overview]/4: "For any N > 0, if
  `is_trivial_v<T>` is false, then no `inplace_vector<T, N>` member functions are usable in constant
  expressions." So at that point the standard itself forbade this use.
- P3074R7, trivial unions, adopted February 2025, struck that sentence and added the feature-test
  macro `__cpp_lib_constexpr_inplace_vector` at `202502L`. Since then the standard permits it.
- libstdc++ 16.2 defines `__cpp_lib_inplace_vector` at `202603L` and does not define
  `__cpp_lib_constexpr_inplace_vector`. Its `inplace_vector` header carries the reason in its own
  words: `// TODO: use new(_M_elems) _Tp[_Nm]() once PR121068 is fixed`, above
  `__builtin_unreachable(); // only trivial types are supported at compile time`. GCC PR 121068 is
  constexpr placement-new of an array. A `constexpr` `inplace_vector<Piece, N>` reaches that line
  and the build fails with "`__builtin_unreachable()` is not a constant expression", which does not
  say why.

So this half is a conformance gap with a bug number, not a bug and not a prohibition. The test
that it has closed is `#ifdef __cpp_lib_constexpr_inplace_vector`, and on the day it does, the
seven uses above could become `std::inplace_vector` with `try_push_back` unchanged, since that
name was chosen to match.

**What is left over.** Two uses are trivial already, `Pattern::slices` and a local in
`slices_read_by`, and could be `inplace_vector` today; a second fixed-capacity vector for two
sites is not worth having. And `Call`'s two would stay on `Vector` regardless, for requirement 1.
That is the order the header comment gives the reasons in, structural first, and it is why
"eventually `inplace_vector`" is true of most of the parser and false of the generator.

### Compile time, and where it went when it moved

Every figure here is one compile of `z80/v4/Z80.cpp` at `RelWithDebInfo` with gcc 16.2, taken
with `/usr/bin/time` on the same laptop within one afternoon. The baseline, the commit before any
of this, is **68.5s and 1.83 GB**, twice, with the load average below two. Each lesson was learned
by adding something and watching the number, and the comparisons that matter were taken again on
the quiet machine, alternating the two builds.

- **One constant evaluation cannot be collected; many can.** A check that compared the resolved
  operands of every opcode sharing a generated body was first written inside `decoding_for`, the
  one `consteval` call that lays out a table: **7.8 GB**, and over 180s on a loaded machine. Moved
  into a `static_assert` per body inside the expansion that builds the dispatch table, so that each
  body's share ran as its own instantiation: **1.9 GB, and 85s to 87s quiet.** The same work, four
  times the memory, because gcc reclaims between template instantiations and never inside one
  evaluation. This is the mechanism behind the `Compiled` figures in MEASUREMENTS.md too, seen
  from the other side.
- **A 256-entry scan of a constant array per body is not free.** The per-body check with its
  comparison switched off, leaving only a loop reading `fill_of<Table>[opcode]` 256 times for each
  of 757 bodies, cost about 6s, back to back with and without: some 194,000 reads of a
  namespace-scope `constexpr` array at 30 microseconds a read. The evaluator does not index a
  constant the way a running program does.
- **Resolving a step is a few milliseconds.** The comparison itself, `call_for` for every opcode
  that shares a body, roughly 2,500 evaluations, was the other 8s to 10s. Hoisting the key's own
  call out of the loop changed nothing, which says the cost is per call, not per pair.
- **A check that costs a sixth of the build had better be worth it.** It guards a library
  invariant against a future edit, not a description against its author. It is now a unit test
  over every opcode of every table, at no compile cost, and `body_key` and `call_for` became
  `constexpr` rather than `consteval` so that a test can call them. The file with everything else
  in this pass and without the check: 71s to 76s.
- **Reading every enumerator's annotations on every lookup costs about 6s.** Honouring `Spelling`
  in the unscoped location lookup, by comparing `spelling_of` for each of the machine's fifty-odd
  enumerators on each of several thousand lookups, was a 6s difference back to back. Comparing
  identifiers first and consulting spellings only when nothing matched costs nothing measurable.
- **Copying a `Row` into a `static constexpr` per body costs nothing.** Binding a
  `static constexpr const auto &` into the compiled array instead, to save the copy, measured the
  same to the second and the megabyte. gcc shares the constant either way.
- **Check the load average before believing a number.** The same file compiled in 55s and in 71s
  an hour apart with no change to it; the difference was three browser processes at 85% each,
  load average 15. Peak memory is unaffected by load and time is not, so a time from a loaded
  machine is a bound at best. Every time above was taken with the load below two, alternating
  the two builds, or says so.

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

### `consteval {}` blocks (P3289)

- **gcc 16.2 implements them, at namespace, class and block scope.** A block runs its statements during constant
  evaluation; a throw that escapes it is reported as "uncaught exception ... what(): file.cpu:7: the message" with
  nothing else in front, where `static_assert(f())` on a throwing `f` first says "non-constant condition for static
  assertion" and then the same. Every check in refract that used to be `static_assert(check_x())` with `check_x`
  returning `true` is now a `void` function called from a block.
- **A block at class scope in a class template runs when the class is instantiated**, as a class-scope
  `static_assert` does. `Compiled<Source>` runs the whole-description checks that way, so a description is checked
  wherever its `Compiled` is first named and there is no `check()` for a consumer to forget. Member functions
  declared earlier in the class can be called from the block; the `steps::` variable templates can too.
- **`[[nodiscard]]` still applies inside a block.** `naming(file, check)` returned the check's `true` and every call
  in the block tripped `-Werror=unused-result`; the checks return `void` now, which is what they meant.

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
