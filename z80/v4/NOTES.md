# v4: notes from the spike

Working notes for the table-driven, compile-time v4: what it does today, what was learned about C++26
along the way, and what remains. For the `.cpu` format itself — grammar, semantics and worked
examples — see [CPU_FORMAT.md](CPU_FORMAT.md). This file is about *why* it is that shape.

Known bugs in v1/v2/v3 discovered while researching this are filed as issues rather than recorded
here.

---

## Where the spike is

`z80.cpu` is `#embed`ed, parsed at compile time, and drives two artefacts. 127 rows in seven decoding
tables decode **every one of 1792 entries**: `base`, `cb`, the `ix`/`iy` views, `ddcb`/`fdcb` and `ed`
are all complete, and the instruction set is finished.

- **Disassembly.** Walks the row's lowered pieces, following a `goto` through a prefix table.
- **Execution.** A 256-entry dispatch table per decoding table, built with a `template for`
  expansion statement, one `execute_one<Table, Opcode, Index>` instantiation per entry, each
  resolving its verb by reflection over the CPU's `operation_scopes()`.

The pipeline is `#embed` → `consteval` parse → lower to validated pieces → `template for` → splice.

A row has three columns: an encoding token sequence, a mnemonic, and an ordered list of steps. Each
column is checked against the others — length and what is fetched come from the encoding, never from
the display text.

Mnemonics are **lowered at parse time** into a fixed `Piece` array (literal chunk, field reference
with field and slice indices already resolved, immediate slot), so the disassembler parses nothing
at runtime.

Malformed tables are compile errors carrying the source line, e.g.

```
z80.cpu:9: reference names a vocabulary that does not exist
z80.cpu:9: vocabulary has the wrong number of values for its opcode bits
z80.cpu:9: this CPU has nothing named 'ld17'
z80.cpu:25: this row overlaps a later one without being contained by it
```

### The argument this exists to make

Everything inside `consteval` is memory-safe by construction — constant evaluation refuses to index
out of bounds or to read a dangling pointer, so a parser bug is a compile error rather than a corrupt
table. Every correctness hole found in the first cut of the spike was in the half left at runtime.
Lowering the table to a validated fixed shape at parse time removes that half entirely.

---

## C++26 findings (all verified on gcc 16.2)

Hard-won and easy to forget. Each of these cost a debugging cycle.

### Reflection

- **`-freflection` is a language dialect switch, not a per-target option.** gcc cannot merge a module
  built without it into a TU built with it — importing one fails with conflicting declarations for
  types reachable both textually and through the module. It also requires `-std=c++26`. It therefore
  lives on `opt::c++26`, which every specbolt target links; applying it globally instead breaks
  third-party targets that build at the default standard.
- **`std::meta::info` is a consteval-only type.** It cannot be stored in anything that survives to
  runtime — a struct containing one becomes consteval-only, so *any* runtime use of that struct
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
- **Released clang has no reflection at all** — 22.1 and trunk both lack `<meta>`. The wasm build is
  clang, so v4 is excluded in CMake via `if (SPECBOLT_HAS_REFLECTION)` rather than by `#ifdef`s in
  source. Bloomberg's P2996 fork is a different matter — see "The other implementation" below.
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
  Getting the size means evaluating the whole parse twice — once for `.size()`, once for the contents
  — which is `to_array` in `ToArray.hpp`, and it is the only place in the pipeline that knows a count.
  The earlier arrangement counted matching lines in a cheap pre-pass and passed the count as a
  template argument to each parse function; that had to be right in two places, and it made every
  parse function a template with a capacity check nobody could reach. **What the second parse costs,
  measured** (alternating A/B, twice each side, gcc 16.2 `-O0`): on `Disassembler.cpp`, which is the
  parse plus every check and nothing else, this one change took 9.3s/387MB to 12.6s/570MB — about
  **+3.2s and +180MB**. On `Z80.cpp`, which is the same parse plus 1792 handler instantiations, 75.4s
  became 73.2s: the same work, lost in the noise of what dominates that TU. Three seconds for a
  pipeline in which one function knows a count. (For where those absolutes stand today, after the
  rest of the clarity work, see "Compile time, measured".)
- **Growing a `std::vector` during constant evaluation is much dearer than growing one at run time.**
  `instructions_of` builds a 1792-element vector for the checks to walk; adding a `reserve` for it
  took about a second off `Disassembler.cpp`. The evaluator has no `realloc` — every growth copies
  every element through the interpreter — so `reserve` is worth writing wherever the size is known,
  which in a parse it usually is.
- `define_static_string` still earns its place for *generated* text, where the bytes must outlive the
  evaluation.

Note the contrast with advice that a `string_view` into the `#embed`ed blob "is trivially structural
and survives promotion". It is neither, and both halves were verified false.

### Expansion statements

- **`template for` + `-Wshadow` was a gcc bug** — [PR c++/124197][pr124197], **fixed** in 16.2, which is
  what this builds against. Verified by removing all six pragma lines and rebuilding with
  `-Wshadow -Werror`: clean. Kept here because the shape of the bug is worth knowing.
  Each expanded copy is reported as shadowing the previous, though nothing is shadowed: every copy is
  its own scope. Minimal repro:

  ```cpp
  constexpr std::array<int, 3> values{1, 2, 3};
  int sum = 0;
  template for (constexpr auto value : values) { sum += value; }   // 3 spurious warnings
  ```

  `-Wshadow -Werror` is exactly this project's setting, so until it landed every expansion statement
  needed a local `#pragma GCC diagnostic ignored "-Wshadow"`. They are all gone now.

  [pr124197]: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=124197
- The range must be a constant expression, and for a range that means a constant *address*, not
  merely a constant value. A plain `constexpr auto row = …;` local does not qualify — gcc says so
  precisely: "address of non-static constexpr variable may differ on each invocation of the enclosing
  function; add `static`". `static constexpr` fixes it, and a namespace-scope `inline constexpr` or a
  template parameter object needs nothing. This is why `execute_one`'s `row` is `static`: expanding
  over `row.steps` directly is what lets the step be the loop variable rather than an index into it.
- **There is no `template switch`.** An expansion statement generates statements, and a `case`
  label is not one, so a 256-way dispatch cannot be expanded into a `switch`. The generated forms
  available are a table of function pointers (what `dispatch` does) or a chain of `if`s. A
  `switch` over a dense contiguous range is the one shape the compiler turns into a jump table on
  its own, so it is exactly the shape reflection cannot reach — worth knowing before assuming
  generated dispatch matches a hand-written interpreter's codegen.

### Toolchain

- gcc 16.2 from the compiler-explorer tarball. No distro packages gcc 16; CI pulls the same tarball.
- Binaries built with an out-of-prefix toolchain bind to the distro's `libstdc++` unless an rpath is
  embedded — resolve the standard library actually being linked and add its directory.
- **ccache's direct mode does not track `#embed` dependencies** and serves stale objects when only
  the embedded file changes. Worked around with `CCACHE_DEPEND=1`. Upstream fix is PR ccache#1765,
  merged 2026-07-19 but not in any release yet; delete the workaround when it ships.
- gcc enforces several module rules clang lets through: every interface partition must be re-exported
  from the primary module interface; textual `#include`s must precede all `import`s in a TU; and code
  `#include`d into a module interface partition must not put entities named by module-attached
  templates in an anonymous namespace.

---

## Design decisions still to make

These dictate the `Row`/`Field`/`Matched` data shapes and are expensive to retrofit. Everything else
is additive.

### 1. Encoding is a byte sequence, not one 8-bit pattern — partly done

The encoding column is now a token sequence rather than a single pattern:

```
00pp0001 n n | ld {p}, $nnnn | ld16 {p} <- n
110qq110 n   | {q} a, $nn    | {q} a, flags <- a n
```

`length` is derived from it, where it used to be inferred by counting `$n`s in the *display* text —
a human-facing string deciding how many bytes the CPU fetches. The fetch itself is now driven by
`row.immediate_bytes` too, so the encoding is the single source of truth for what is read.

That collapses a redundancy: an immediate operand has one spelling, `n`, and the encoding says how
wide it is. `nn` in an action is now an error suggesting the fix. A vocabulary member may not append
an immediate at all, since only the encoding fetches.

The three columns now cross-check, each failure naming its line:

| broken | reported |
|---|---|
| encoding fetches fewer bytes than the mnemonic renders | `the mnemonic renders a different number of immediate bytes than the encoding fetches` |
| encoding fetches an immediate the action never uses | `the action and the encoding disagree about whether there is an immediate` |
| `nn` written in an action | `write 'n'; the encoding column says how many bytes it occupies` |
| unknown encoding token | `'d' is not an encoding byte; expected 'n'` |

Still to do, and it *is* prefixes rather than a separate step: more than one bit-pattern token per
row, so `11001011 <pattern>` and the DDCB fetch reordering become expressible. The token sequence is
the structure that will carry them; today every row has exactly one pattern token, at the front.

#### Where this is going

Real encodings are up to four bytes, and `DD CB d op` puts the displacement **between** the prefix
and the opcode. The token list generalises to that without changing shape:

```
DD CB d 01bbbzzz | bit {b}, (ix{d}) | ...
```

Fetch order is textual order, which is what timing needs, and it settles the DDCB inversion for
free: the row states left to right that the displacement precedes the opcode, so no per-table "the
operands come in this order" declaration is needed.

One known limitation to lift on the way: the execution path fetches a single immediate before any
step runs, which cannot express `ld (ix+d), n` — two immediates read at different points. Fetching
per-token, at the point the token appears, is the fix, and it is the same change that makes multiple
pattern tokens work.

### 2. Vocabulary members must be structured — the timing half is done

A member was just a name. But in `r = b c d e h l (hl) a`, member 6 changes storage class, timing
(`+1` T-state), and the flag inputs to `Alu::bit` (bus noise comes from `wz` when indirect).

The member now carries its own access sequence:

```
field r = b c d e h l (hl)/delay=1 a
```

`delay=1` says the machine idles for one cycle between reading through this addressing mode and
writing back through it. The framework applies it only to a destination that was also a source, so
`ld (hl), b` stays 7 and `inc (hl)` becomes 11 with the idle cycle in the right place, between the
read and the write, rather than appended to make the total come out.

#### Latches: built, then deleted

The first attempt made the sequence explicit in the row, with a latch to hold the value:

```
00110100 | inc (hl) | ld8 t <- (hl) ; inc8 t, flags <- t flags ; delay 1 ; ld8 (hl) <- t
```

That works, and the mechanism cost nothing — **a latch is just a location**, so the CPU declares it
and the framework needs no support at all. But it produced four pairs of near-duplicate rows
(`inc (hl)` beside `inc {r:y}`, and the same for `dec`, `res`, `set`), each differing only by a
mechanical `ld8 t <- X ; … ; ld8 X <- t` bracket. That bracket is not information; it is a
consequence of X being memory, and the framework already knew that.

Moving it to the member deleted all four extra rows and left the general rows exactly as they were:

```
00yyy100 | inc {r:y}      | inc8 {r:y}, flags <- {r:y} flags
10bbbzzz | res {b}, {r:z} | res8 {r:z} <- {r:z} {b}
```

The latch was removed with them. It returns for DDCB and `ld (ix+d), n`, which genuinely need a
value to survive between steps, but nothing needs it today and speculative surface in a table meant
to be read is worse than re-adding six lines later.

`bit {b}, (hl)` keeps its own row, and that is not boilerplate: its idle cycle follows a read with
no write-back at all, and its bus noise differs from the register form. Genuinely different
behaviour, genuinely a different row.

Vocabularies still need empty members (the ED block group concatenates three vocabularies, one
containing `""`). Holes exist and are checked.

**This is also where 6502 support lives or dies.** On the Z80 exactly one member is special. On the
6502 *every* member of `bbb` is an addressing mode that changes length, cycles and whether the row
exists at all. Design the member record for the 6502 case and the Z80 becomes the easy instance.

### 3. Rows need several slots, and slot ≠ letter — done

`{vocab:slice}` decouples the vocabulary name from the slice letter, so `01yyyzzz | ld {r:y}, {r:z}`
binds one vocabulary to two slices. Arity and parameter types are checked by reflecting
`parameters_of` on the resolved primitive, which is also what makes a width mismatch a
`-Wconversion` error rather than a truncation.

`Matched::max_slices` is 4 and `Field::max_values` is 8; no Z80 row needs more, but `r` and `cc` sit
exactly at 8 with no headroom.

### 3a. Vocabulary members must bind to C++ entities, not carry loose text

Today a field member is a bare `string_view`. The disassembler uses it to emit `"bc"`; execution
ignores it entirely and re-derives the mapping in `Ops::pair_for`:

```cpp
constexpr std::array pairs{RegisterFile::R16::BC, RegisterFile::R16::DE,
    RegisterFile::R16::HL, RegisterFile::R16::SP};
```

That is the same list as `field p = bc de hl sp`, restated in C++ and coupled to the `.cpu` only by
ordinal position. Reorder one and you get a silently wrong register with no diagnostic. The `unsigned
which` threaded through every `Ops` member is the symptom: the field value crosses the boundary as a
bare ordinal, so nothing can check it.

**Fix: resolve members to real C++ entities by reflection**, the same trick `find_operation` already uses
for the action column. Verified working on gcc 16.2:

```cpp
consteval std::meta::info find_register(std::string_view name) {
  for (const auto e : std::meta::enumerators_of(^^RegisterFile::R16))
    if (equal_ignoring_case(std::meta::identifier_of(e), name)) return e;
  throw table_error(line, "no such register");
}

template<RegisterFile::R16 Reg> constexpr void ld16(RegisterFile &, std::uint16_t);
// ...
constexpr auto reg = [:find_register(member_name):];
ld16<reg>(regs, operand);
```

What this buys:

- `Ops` members take `RegisterFile::R16`, never `unsigned`.
- `pair_for` and its duplicated array both disappear; the register list is stated once, in the `.cpu`.
- A typo in the `.cpu` is a compile error with a line number.
- Display text can come off the enumerator via `identifier_of`, so even the name is stated once
  (case-folded for display, or kept as an explicit display column where the mnemonic differs from the
  C++ spelling — `(hl)` has no enumerator).

**There is more than one kind of vocabulary**, and a member should declare what it binds to:

| vocabulary | binds to | example |
|---|---|---|
| `p`, `rp`, `r` | an enumerator | `bc` → `RegisterFile::R16::BC` |
| `alu` | a primitive function | `add8` → `Alu::add8` |
| `bit`, `rst` | a plain number | `0..7`, `0x00..0x38 step 8` |
| `cc` | a condition primitive plus display text | `nz` |
| memory members | nothing nameable | `(hl)`, `(ix+d)` — display text plus a `mem:` kind |

This is the same axis as the structured-members decision in §2 and subsumes it: "is this member
special?" and "what does this member bind to?" are one question. Design the member record to answer
it once.

### 4. Row is parse-time only — partly done

`decoded[table][256]` exists: it killed the runtime linear scan and is what precedence checking and
coverage are computed from. Text is projected into `pieces` at parse time. Cycles and
flags-affected are not projected — cost is resolved during execution from the addressing mode and
the step list, and nothing consumes a flags-affected table yet. The `line` field propagates
everywhere, so every diagnostic reports `z80.cpu:N`.

### 5. First-match-wins needs compile-time checking — done

`halt` (`01110110`) collides with `ld r,r'` (`01yyyzzz`), and the winner was decided by line order,
silently. All three prior implementations treat this as a hazard needing an explicit statement.

Implemented as `check_row_precedence()`, a `static_assert` over the winner array. Two rules:

- a row must win at least one opcode
- where two rows overlap, the earlier must be **wholly contained** in the later — that is an
  override, and it is how `halt`, `cp` and `inc (hl)` all work. A partial overlap is an accident and
  is rejected.

No "intentional override" marker turned out to be needed: containment already distinguishes the
legitimate case from the accident, so the legal shape is checked rather than merely asserted by the
author. Verified by breaking the table three ways and reading the diagnostic:

| what was broken | reported |
|---|---|
| override placed *after* the general row | `z80.cpu:25: this row overlaps a later one without being contained by it` |
| a row partly overlapping a later one | `z80.cpu:13: …` |
| a row all of whose vocabulary members are holes | `z80.cpu:30: this row matches no opcode at all` |

Coverage is `decoded_count`, a compile-time constant, ratcheted by a test. Because precedence is
checked, coverage cannot be gained by silently shadowing another row.

### 6. Timing attaches to the micro-op sequence — done, and it moved

Landed, but not where this section predicted. Cost turned out to belong to the **addressing mode**
for anything to do with an operand, with an explicit `delay` step only for idle cycles belonging to
the operation itself. See "Where cost actually lives" and "Time passes in exactly one place" below.
The original argument, which still stands:

- v3 has **no cycle numbers anywhere**. Timing emerges from the primitives (`read`/`write` = 3,
  opcode fetch = 4) plus `pass_time(n)` interleaved at the right points.
- v1 *did* attach times to operands and it collapsed — `access_time(Operand)` carries the comments
  "Doesn't make sense but I am just trying to fix up the bad timings" and "Heinous hack to make this
  flawed approach agree with reality".
- Cost is conditional (`djnz` 8/13), depends on a vocabulary member (`y == 6` adds 1), and depends on
  a per-row quirk (the DD/FD displacement prologue costs 5 normally but 2 for load-immediate rows —
  v3 carries a whole `is_load_immediate` field just for this).

So: primitives declare their own cost, the semantic column is an **ordered list of steps** with
explicit idle delays, and a row-level `t=` is an *assertion* checked at compile time rather than a
definition. Conditional rows want `t=min/max`, which is also what a timing view wants.

This is floooh/chips' micro-op model, and converging on it is fine — chips is cycle-exact and was
retargeted to the 6502, so it is evidence the model generalises. The difference is the mechanism:
chips generates ~1600 numbered step cases from a Python script emitting C, with nothing type-checking
the output until the next compiler run. Here the steps are a compile-time description, resolved
against real C++ signatures by reflection, with errors pointing at the `.cpu` line.

**Critically, the steps are unrolled away — nothing resembling a state machine survives into the
binary.** Verified rather than assumed: `DD CB d 00` built as an 8-step sequence dispatched through
`template for`, each step spliced by reflection, produces assembly **byte-identical** to the
hand-written equivalent at `-O2` (17 instructions), and the five separate `pass_time` contributions
(3+5+3+1+3) fold into a single `addq $15`. The latch never reaches memory. Variable-length sequences
fold too.

Code size measured at 58.5 bytes/opcode unrolled, against **94 bytes/opcode that v3 already ships**
(165 KB for its seven dispatch functions). Unrolling is smaller than the status quo, not larger.

What unrolling forfeits is mid-instruction suspension — the ability to drive the CPU cycle by cycle,
which is exactly why chips keeps its runtime state machine. specbolt has never had that: v1, v2 and
v3 are all instruction-stepped with `pass_time` accumulating T-states, and there is no memory
contention model anywhere in the repo. Contention stays reachable later, since each step carries its
cycle offset. What is genuinely given up is floating-bus reads and interrupt acceptance at a
sub-instruction boundary.

**The step model is for the semantic column only.** The encoding column stays a bit-pattern token
sequence, because instruction length, `winner[256]` and coverage checking all need patterns — you
cannot derive them from a step list without symbolically executing it. chips does not have this
constraint because chips does not disassemble; v4 produces two artefacts from one table, which is the
whole point.

It also retires v3's `is_load_immediate` boolean. The 5T-vs-2T displacement prologue exists only
because the fetches of `d` and `n` interleave with the internal delay; as an ordered step list that
is simply *stated* rather than special-cased.

### 7. Flags come from the primitive signature — mostly

The table never mentions F. The CPU holds the flags; `Alu` returns `ResultT<T>{result, flags}` and
the framework routes the flags half into the register file. **Spiked and working** against the real
`Alu` shapes — all four calling conventions fall out of reflection alone:

| primitive | derived wiring |
|---|---|
| `R8 add8(u8, u8, bool)` | carry spliced in; result → destination, flags → F |
| `R8 and8(u8, u8)` | no flag input at all |
| `R16 add16(u16, u16, Flags)` | whole flag word spliced in |
| `Flags bit(u8, u8, Flags, u8)` | flags only — destination untouched |

Parameter types decide what is read; the return type decides whether a value is written. Verified by
`static_assert` that `bit` leaves A alone while `add8` writes it, purely from the signatures.

**But the signature is not sufficient on its own.** `add8` is shared by ADD and ADC: v3 calls
`add8(a, rhs, false)` for one and `add8(a, rhs, flags().carry())` for the other. Same for
`sub8`/SUB/SBC. So "a `bool` parameter means splice the carry" is wrong half the time, and nothing in
the signature distinguishes them.

The policy belongs on the **vocabulary member**, which is where the encoding already puts it —
`10ooozzz`'s `ooo` field *is* the add/adc/sub/sbc/and/xor/or/cp vocabulary. So a member binds to a
primitive **and** a calling policy for whatever the signature leaves ambiguous. Verified: `add` and
`adc` route through the same `add8` and differ only in the member's carry source.

Refined claim: **the signature determines the shape; the vocabulary member determines the policy for
what the shape leaves ambiguous.** Weaker than "the signature is the whole declaration", still beyond
anything a text-emitting generator can check.

Residues that need saying explicitly regardless: partial preservation (`in r,(c)`), extra inputs
(`bit`'s bus noise), and the block ops, which will never be table-expressible and get a bespoke
primitive.

### 8. WZ/MEMPTR is per-instruction data

Cannot be inferred, and all three existing versions are incomplete — nothing sets WZ for `ld a,(nn)`,
`ex (sp),hl`, `jp nn`, `call` or `out (n),a`, all of which the real chip updates and all observable
through `bit n,(hl)`. An optional per-row attribute, defaulting to "a memory operand sets WZ to the
effective address" (which is what all three versions already do).

v4 does not model it either. `bit {b}, (hl)` names `h` as its bus-noise source, which is HL's high
byte and therefore right for that one instruction — the same approximation v3 computes. The reason
nobody notices is structural: the only regression test in the repo is **zexdoc**, and "doc" means
documented flags. There is no zexall, so undocumented-flag behaviour is untested repo-wide apart
from a handful of hand-written checks in `OpcodeTests.cpp` — which is exactly where v4's two
failures showed up, and both are now fixed.

---

## Prefixes

### Status: table switches, views, indexed addressing and DDCB all work

`table <name>` declares a decoding table, and `goto <table>` is a step. A prefix is an ordinary row:

```
11001011     | (cb)            | goto cb

table cb

01bbb110     | bit {b}, (hl)   | bit8 flags <- (hl) {b} flags ; delay 1
01bbbzzz     | bit {b}, {r:z}  | bit8 flags <- {r:z} {b} flags
```

Decoding starts in the first table declared, so no name is special to the framework. Dispatch,
precedence checking and coverage are all per-table; the `(hl)` override rows inside `cb` are checked
for containment exactly as the ones in `base` are.

`goto` is deliberately *not* a CPU primitive. Fetching the next byte is (`fetch_opcode`), but
choosing a table is the framework's own job — it is the one verb the framework understands.

Scored on `z80/test/OpcodeTests.cpp`:

| suite | passing | undecoded | wrong |
|---|---|---|---|
| unprefixed | 139 / 192 | 53 | 0 |
| cb | 23 / 39 | 16 | 0 |

DD/FD are covered by `ExecuteTest.cpp` instead, against the same expectations.

Every failure in both is an opcode the table does not describe. All three CB timings are right: 8
for register forms, 12 for `bit n,(hl)`, 15 for `res`/`set` on memory.

### What is papered over, deliberately

Audited against every suite in `OpcodeTests.cpp`; no suite reports a genuine mismatch. These are
the things the suites do *not* catch, or catch only because we match an approximation v3 also makes:

- **WZ is not modelled.** `bit {b}, (hl)` names `h` as its bus-noise source, because for that one
  instruction WZ's high byte is HL's. It is right for the tested cases and states the approximation
  where a reader can see it, but it is not WZ. §8 stands. The register form is *not* an
  approximation: flags 3 and 5 genuinely come from the operand, which the row now says.
- **Interrupts are not handled at all.** v3 checks `irq_pending_` at the top of `execute_one`; v4
  does not. Not a papered-over difference so much as a missing feature, but it is missing.
  *(Since fixed: `Z80::handle_interrupt` does all three modes, `ei` defers acceptance by one
  instruction, and `ExecuteTest` covers it. What remains open is that nothing ever releases /INT --
  see the section on it below.)*
- **The immediate is fetched once, before any step**, rather than at the token that names it. Fine
  for every row that exists; wrong for `ld (ix+d), n`.
- **A push writes its two bytes in the wrong order.** `write_memory16` goes low byte first, which is
  what `ld (nn), hl` does; hardware pushes high to sp-1 and then low to sp-2. The bytes end up in the
  same places, so nothing can see it until `Z80::bus` starts contending or something watches writes.
  One function cannot serve both orders -- the fix is either a second one, or letting the description
  spell the two writes out, which `push`'s two `dec16 sp` steps already half do.

Found by this audit and fixed rather than recorded: `scf` and `ccf` had `a` as a destination, but
`Alu::scf`/`ccf` return the accumulator unchanged, so the rows claimed a write that never happened.
They now discard, as `cp` does. Harmless in behaviour, wrong as documentation -- and the table is
documentation.

### What DD/FD needs, in order

What exists is a table switch, which is what CB and ED need and is the easy case. DD/FD are *views*,
and the groundwork is this, roughly in the order it has to happen:

1. ~~**The execution model, before any syntax.**~~ **Done.** `goto base with view=ix` re-enters the
   table it came from, and the old `check_no_goto_cycles` rejected that. The reason was never that
   template instantiation would fail to terminate — `enter<Table>` was forward-declared and mutual
   instantiation is fine — but that each goto was a real opcode fetch made by a *nested call*, so a
   cycle was unbounded C++ recursion, and `DD DD DD…` is legal Z80.

   A handler now **returns** `Next` — the table to decode the next byte in, or nothing — and
   `execute_instruction` loops on it. Every turn of that loop fetches a byte, so it always advances
   both PC and the clock: a cycle is progress, not recursion, and the check is gone along with its
   diagnostic. The handlers no longer instantiate each other at all, which is why the forward
   declaration went too.

   It costs one branch per instruction and nothing per prefix byte: `cb` compiles to
   `mov $0x101,%eax; ret`, and the two-dimensional dispatch folds into a single scaled load indexed
   by `table << 8 | opcode`.
2. ~~**`goto` learns `with view=`.**~~ **Not needed — the item dissolved.** The sketch below spelled
   the same idea twice: `goto base with view=ix` *and* `table ix = base with hl->ix, …`. Only the
   second is necessary. If a view is a **derived table**, then `goto` never changes: it already takes
   a table name, and `ix` is one. `Next` stays a table index rather than becoming a (table, view)
   pair, and the state count stops being a product.

   It also gets `ed` right for free. `ED` discards a pending `DD`, which the first spelling needed an
   explicit `with view=hl` to say. Under derived tables the inherited row is `goto ed`, a goto names
   a table and a substitution rewrites *members*, not table names — so decoding lands in plain `ed`
   with no rule to write.

   What exists now:
   - `table ix = base with hl->ix, h->ixh, l->ixl` — either spacing round the arrow. The right-hand
     side is parsed as a full vocabulary member, so a substitute brings its own primitive and its own
     `/delay=`.
   - `decode_tables` gives a derived table its parent's rows for every opcode it does not claim
     itself, so an override row is just a row and first-match-wins does the rest. A parent must be
     declared above its children, which makes the derivation a forest and lets declaration order
     resolve a chain.
   - `member_of` applies the renaming, and it is still the only place a reference is followed. That
     is the whole of the mechanism: **a view is a function from member to member, applied at the one
     point a `{field}` is resolved.** Literal text is untouched by construction, which is the rule
     stated below, now enforced by there being nowhere else for a rule to act.
   - A derived table with no rows of its own is legal — it *is* its parent, renamed — so
     `check_tables_used` no longer demands rows of one.

   Not yet wired into `z80.cpu`: `(hl)` must become `(ix+d)`, which fetches a displacement byte, and
   that is item 4. Adding `dd` before then would decode `inc (hl)` as `inc (hl)` under DD — a
   knowingly wrong emulator — so the mechanism is tested on its own description in `DiagnosticsTest`
   instead, including that `dd dd` re-enters.
3. ~~**References before views.**~~ **Done.** `Operand` and `Piece` each spelled a reference as two
   loose indices, and four places spelled out the lookup that follows one. Both now hold a
   `Reference`, and `member_of` is the only place one is followed — which is the place a view will
   have to intercept.
4. ~~**Per-token fetching, and the latch it needs.**~~ **Done** — and it needed no multi-token
   encodings after all. See below.

### The displacement is derived, not declared

`(hl)` becomes `(ix+d)` under a view, and that costs a byte the row never mentioned. Who says so?

**floooh's chips says so by hand.** Its description carries `flags: { indirect: true }` on every row
that touches `(HL)`, and the generator turns those into a 256-entry `_z80_indirect_table[]` that the
shared DD/FD fetch consults:

```c
case Z80_DDFD_M1_T4:
    cpu->addr = cpu->hlx[cpu->hlx_idx].hl;
    _goto(_z80_indirect_table[cpu->opcode] ? Z80_DDFD_D_T1 : cpu->opcode);
```

That flag exists because his description does not resolve operands ahead of time — the generator
cannot tell that `INC (HL)` touches memory except by being told, on about forty rows, correctly,
for ever. **We already resolve every operand at compile time, so the same fact is derivable**:
`displaced_through` asks what the operands resolve to under this table's rules and returns the one
they are displaced through, or nothing. No annotation, no table, nothing to forget. The flag floooh
must write is a `consteval` question for us.

(His view is the runtime twin of ours: `hlx[3]` indexed by `hlx_idx`, so every generated case reads
`cpu->hlx[cpu->hlx_idx].h`. Same idea, opposite trade — he pays an indexed load on every H access
for ever, we pay compile time and table count.)

Three things the exploration got wrong first, each caught by a test rather than by reasoning:

- **The address must be formed once per instruction, not once per operand.** `inc (ix+d)` reads and
  writes through the same address; forming it per operand paid for the window twice. So `execute_one`
  forms it up front and hands it to every operand that shares it — which is the "latch", arriving for
  a reason that has nothing to do with DDCB. A row may only be displaced through one base, and that
  is checked.
- **The window absorbs the immediate.** `ld (ix+d), n` is 19 T-states, not 22: the `n` is read
  *inside* the five-T-state window that forms the address, not before it. So the machine is told how
  many bytes were already read. floooh handles this with `if (cpu->opcode == 0x36)`; deriving it
  needs no special case.
- **A rule names the vocabulary it rewrites.** It did not always: rules once matched on a member's
  text alone, so `h -> ixh` reached the `{real:y}` in `ld {real:y}, (ix+d)` and wrote IXH instead of
  H — exactly the bug that row exists to avoid. A `Rule` now carries a vocabulary index and
  `member_of` compares it, so `reg.h -> ixh` leaves `real.h` alone. Rules still apply to every row
  the table decodes, its own as well as inherited ones.

**The customisation point is one function.** Not a DSL attribute, and not framework arithmetic:

```cpp
[[nodiscard]] std::uint16_t displaced_address(Cpu &, std::uint16_t base, std::uint8_t offset,
    std::uint8_t immediate_bytes);
```

It owns both how a base and an offset combine *and* what forming the address costs, because both are
facts about the machine — a 6502 wraps within page zero for one mode and charges for a page crossing
in another. Taking `Cpu &` is what lets the cost live there. Everything else the table already said.

Verified in `ExecuteTest.cpp` against the counts `OpcodeTests.cpp` asserts of v1/v2/v3: 19 for
`ld r,(ix+d)`, `ld (ix+d),r`, `ld (ix+d),n` and `add a,(ix+d)`; 23 for `inc (ix+d)`; 8 for a DD that
renames nothing; and `dd dd dd 23` at 4 T-states a prefix byte. Generated code forms the address once
with `add`, reuses it for the read and the write, and folds both idles into constant clones.

**The disassembler followed.** A member's text is now lowered into `Piece`s at parse time exactly as
a row's mnemonic is — `lower_text` does both — with `Piece::Kind::Displacement` for the hole `+d`
leaves. A row renders its pieces, and a member renders its own, so `inc (ix-0x01)` falls out without
the disassembler parsing anything at runtime. The displacement is taken before the pieces are walked,
because it precedes any immediate, which also makes the reported length right.
5. ~~**Capacity.**~~ **Checked; nothing to change.** `Field::max_values` is 8 and the `ix` view's
   register vocabulary is exactly 8 (`b c d e ixh ixl (ix+d) a`) — it fits, with no headroom.
   `Rules` holds 6 and `ix` needs 4. `Row::max_steps` is 6, which DDCB might exceed, but bumping a
   limit before something reaches it is guessing.

   What was actually missing was any evidence about the edge, so every fixed capacity now has a case
   in `DiagnosticsTest`: steps, operands, destinations, mnemonic pieces, substitutions. All five
   report the table's limit rather than corrupting quietly, so raising one when DDCB needs it is a
   one-line change made in response to a message rather than to a guess.

Nothing in 1–4 is a syntax question. The table language for prefixes is already written down below;
what is missing is the machinery underneath it.

Still untested for real: that a long prefix chain does not grow the stack. `DiagnosticsTest` asserts
only that a self-goto *parses*, because no row uses one yet. The test to write alongside DD is
`dd dd dd … 00`: constant stack, 4T a byte, `r` incremented once a byte.

### What DD actually does, measured

Diffing `execute_one_dd` against `execute_one_base` in v3's generated code, case by case:

| | count |
|---|---|
| byte-identical — DD is a pure no-op | **169 / 256 (66%)** |
| differ | **87 / 256 (34%)** |

Of the 87, **86 are register renames and exactly one is a table switch** (`CB`). So "DD makes every
opcode different" is emphatically false — two thirds are untouched.

The shape of the affected set decides the design. They are vocabulary slices — `r ∈ {4,5,6}`,
`rp[2]`, `rp2[2]`, and the standalone index register — and **`{4,5,6}` is not maskable**. It is not a
bit-pattern family. `01yyyzzz` covers 64 opcodes of which 38 are affected and 26 are not, with no bit
pattern separating them.

### The model: `goto <table>`, where a view is a derived table

No new primitive. Prefix rows are ordinary rows, and a view is a table declaration.

```
table base
  11001011 | (cb) | goto cb
  11101101 | (ed) | goto ed     # lands in plain ed: a rule renames members, not tables
  11011101 | (dd) | goto ix     # re-enter the same map, renamed
  11111101 | (fd) | goto iy

table ix = base with hl->ix, h->ixh, l->ixl, (hl)->(ix+d)
  11001011 | (dd cb)          | goto ddcb          # override: a jump, not a rename
  01yyy110 | ld {r8:y}, (i+d) | ld_from_indexed    # override: half-registers stay real
  01110zzz | ld (i+d), {r8:z} | ld_to_indexed
```

An earlier sketch also gave `goto` a `with view=` clause. That was the same idea written twice; the
declaration form is enough, and keeping only it means the decode state stays a single table index.

A derived table may carry **override rows** that shadow the derived ones by first-match-wins — which
§5 already requires anyway, so overrides cost no new mechanism. That is what removes the two
special-purpose mechanisms an earlier sketch needed: no `view=` guard for routing DD CB, and no
`view-rule` for half-register suppression. Both become rows a reader can see.

Stating DD as "re-enter the table you were already in" is also more honest than "set a mode": it
makes clear a full opcode fetch follows, with its 4 T-states and R increment.

State is one table index — seven of them, exactly v2's seven tables. Prefix chains (`DD DD FD`) fall
out: `ix` inherits base's `goto iy` row, so each byte just re-enters, last wins.

**One rule to keep: only `{field}` references are rewritten; literal text never is.** `ex de, hl`
written literally is therefore immune by construction. Substitution-by-default would reproduce the
exact bug v2 and v3 both have. This is now structural rather than a rule to remember: `member_of` is
the only place a rule is consulted, and it is only reachable through a `{field}`.

Naming a different vocabulary *is* enough, now that a rule carries the vocabulary it rewrites; it
was not when rules matched on member text alone, and `ExecuteTest` caught the difference. What stays
exempt is only the diagnostic: `check_inherited_literals` does not ask an override row to spell out
what its parent spelled literally. The rows above name `{real:y}` rather than `{reg:y}` for a second
reason as well: `real` has a hole where `reg` has `(hl)`, so `01yyy110` does not claim `0x76` and
`dd 76` stays `halt`.

### How DDCB was actually done

`d` in the encoding column, which was reserved for this from the start — the old diagnostic already
said `'d' is not an encoding byte`. A row that reads one does not use it; it hands it to the table it
goes to:

```
table ix = base with hl -> ix, …
  11001011 d | (dd cb) | goto ddcb
```

Everything else is derived from that one token:

- **A table is *latched* if the gotos reaching it read a displacement.** Its rows use the incoming
  value instead of reading their own, and it is an error for a table to be reached both ways. Nothing
  is declared on the table itself.
- **A latched table's opcode arrives as an operand read**, not an instruction fetch — three cycles
  and no refresh, which is exactly what the real chip does for that byte and why `R` does not
  increment for it. The loop picks `fetch_immediate` over `fetch_opcode` on that one bit.
- **The address window absorbs it.** The five-T-state window that forms `ix+d` contains the opcode
  read here, just as it contains the immediate in `ld (ix+d), n`: `5 - 3×1 = 2` idle cycles.
  `displaced_address` needed no change at all, only a truthful count of what was read inside it.

So the latch is `Next` carrying a byte, and it costs nothing: the displacement arrives in a register
parameter. `set 4, (ix+d), b` compiles to sign-extend, `idle`, `add`, `read`, `or $0x10`, `idle`,
`write`, `mov` — the primitive inlined and the undocumented copy a single store.

Two small capabilities came with it, both general rather than DDCB-shaped: an operand written out in
a row may carry `/delay=1` exactly as a vocabulary member can, and one result may name more than one
destination. The second *is* the undocumented copy.

Timings verified against `OpcodeTests.cpp`: 20 for `bit n,(ix+d)`, 23 for `set`/`res`. Flags 3 and 5
come from `wzh`, the high byte of the address the machine last formed, which is a named location now
rather than a papered-over `h`.

Still missing from these tables: the rotate family, because `cb` does not have it either.

### DDCB is different in kind, and substitution provably cannot express it

CB and ED are pure table switches. DD and FD are re-readings of the same map. **DDCB is both, plus a
fetch reordering** — the only encoding in the instruction set where the opcode byte is not the last
byte read. It gets its own four-row table, and that is not a taste judgement:

**v2 and v3 both build DDCB by substitution, and are wrong for 224 of 256 entries.** The generated
code fetches the displacement, then operates on the named register and never touches memory:

```
// Z80Generated.cpp:7454   case 0x00: // rlc b
      const auto lhs = get(R8::B);   // reads the real B; (ix+d) ignored
```

Real hardware: `DD CB d 00` is `rlc (ix+d)` with the result *also* copied into B. Correct only for
the 32 entries where `z == 6`. v1 gets the addressing mode right (`Decoder.cpp:158`) and omits only
the undocumented register copy.

So the repo contains a working implementation of "DD is a view over the CB table", and it is wrong
224 times. Write down *why* `ddcb` is separate, or someone will fold it back into `cb`.

The undocumented register copy is a second destination, and `z == 6` is a separate row reached by
first-match-wins rather than a "no destination" member — `s` already has a hole at 6, so `10bbbzzz`
does not claim it and `10bbb110` does. No new vocabulary syntax was needed.

`ddcb` and `fdcb` are written out twice rather than one derived from the other, because they differ
only in a *literal* operand and a rule rewrites only `{field}` references. That is the same rule that
keeps `ex de, hl` safe, so the duplication is the price of it; worth revisiting together when
`ex de,hl`, `jp (hl)` and `ld sp,hl` land, since those are the rows that decide whether literals
should ever be rewritten.

### Pure goto is refuted by v1

Giving DD its own full table is the obvious alternative and the repo already tried it.
`v1/Decoder.cpp:191` is a whitelist override table covering **165 of 256**; the other **91 opcodes
return `??`**, including `DD 00`. The failure mode is invisible — nothing in the file says a row is
missing. Rows would go from ~87 to ~177, with 94 duplicated, and every semantic fix would need making
in three places.

### Row counts for a complete Z80

| | rows | duplicated |
|---|---|---|
| **derived tables (this design)** | **~87** | ~8 |
| DD/FD as full tables | ~177 | 94 |
| 256 longhand per state | 1792 | ~1450 |

Base map is 47 rows by bit-pattern family, `cb` 4, `ed` ~24, `ddcb` 4, plus vocabularies and headers.
Cross-checks against v3's own family count (49 `return` sites in `match_op`). Call it ~90 rows,
110–120 if you decline the tighter vocabularies. It is not copy-paste.

The real duplication, being specific: `rot`/`bit`/`res`/`set` written twice (once for `cb`, once for
`ddcb`, because the semantics genuinely differ); `ld (nn),hl` and `ld hl,(nn)` existing in both base
and ED because the chip really does encode them twice; `inc`/`dec` pairs; `view ix`/`view iy`.

### Loop, not recursion

v2 and v3 recurse on prefixes. A loop with an explicit state, where a handler returns the next state,
means `DD DD DD…` cannot blow the C++ stack, and "are we mid-prefix?" becomes an inspectable value.
That matters because **none of v1/v2/v3 model interrupt acceptance around prefixes** — all three
sample `irq_pending_` once at the top and swallow the whole chain. With recursion it is not even
expressible.

v4 does this now: `execute_instruction` is the loop and `Next` is the state. The interrupt half is
still not done, but it has somewhere to go — the top of the loop is exactly the point the Z80 will
not accept an interrupt at, because a prefix and its opcode are one instruction. Knowing that
requires the state to be a value, which it now is.

### Costs, measured

7 states × 256 = 1792 instantiations, of which **338 are byte-identical duplicates** (169 per index
register). Aliasing those, plus folding `fdcb` into `ddcb` with the index register as a runtime
value, leaves ~1200.

Dispatch machinery floor on gcc 16.2 (`-O1 -freflection`, trivial bodies): 1×256 = 1.59 s / 114 MB;
3×256 = 3.10 s / 154 MB; 7×256 = 4.80 s / 233 MB — roughly **0.53 s and 20 MB per additional
256-entry table**, linear in states. With real step bodies, ~+1 s and +22 MB per 256; 7×256
extrapolates to about **12 s / 390 MB**.

---

### The parser reads whatever it is handed

The parse functions used to read the `#embed`ed description directly, which meant the only way to
see what a malformed table said was to damage the real one — done by hand four times before it was
obvious that it was a smell rather than a technique.

They now take the description, the vocabularies and the table names as parameters, and are
`constexpr` rather than `consteval`, so they run at runtime too. The three names that reach for the
embedded file are declared *below* every function that parses, so a parser cannot reach the
production instance even by accident; `-Wshadow` enforces it, which is how the last two were found.

`DiagnosticsTest.cpp` drives the whole pipeline with its own raw-string tables and asserts the exact
message and line for every diagnostic a malformed table can produce. Writing it immediately found a
dead one: `is_table` matched `"table "` with a space, so a bare `table` line was silently ignored
rather than reporting that it has no name.

The same change is what a second CPU needs, since nothing about the parser now says `z80.cpu` except
the one line that embeds it.

## Speed, measured

zexdoc, sequential, same machine, same `release-reflection` build, one run each. All four execute the
identical program, so the ratio is relative interpreter throughput.

| | | vs v4 |
|---|---:|---:|
| v1 | 262.2s | 2.45× slower |
| v2 | **88.8s** | 1.20× faster |
| v3 | 108.4s | 1.4% slower |
| **v4** | **106.9s** | — |

The number that matters: **v4 is level with v3**, the code-generated one. Generating an interpreter
from a table costs nothing against generating one from a C++ generator. And both are 2.4× faster than
v1's decode-then-execute.

**v2 is 20% faster than both, and it is not dispatch.** v2 does
`impl::table<impl::build_execute_hl>[opcode](*this)` — a 256-entry function-pointer table, exactly
v4's shape — and both keep `read`/`write` out of line. So the `template switch` theory (that
reflection cannot generate the jump table a hand-written `switch` gets) does not explain this: v2
does not have one either.

Where the difference actually is has not been established, and guessing is not worth much. The
hypothesis worth testing first is that v4 routes *every* idle cycle through `Z80::bus`, an
out-of-line call that switches on the access kind and stores the bus address, where v2 charges its
internal cycles directly. That would be the price of *Time passes in exactly one place* — a design
choice made deliberately so contention has somewhere to live, and one worth knowing the cost of
before the Spectrum needs it. **Profile before believing any of this.**

Caveats: one run each, no repeats, on a laptop; and zexdoc's instruction mix is ALU-heavy, so this
under-reports dispatch cost relative to a program doing more loads and jumps.

## Compile time, measured

The other half of the trade, and the one that is easy to forget because `ccache` hides it. Same
compiler for all four (gcc 16.2, `-O0 -g`, `-freflection`), `ccache` bypassed, each translation unit
compiled on its own. The sweep was run in both orders and the **minimum** of each TU taken, because
this laptop moves a compile by 30% depending on what ran before it — v4's disassembler TU measured
15.5s running last and 23.1s running first, on identical input.

| | how the decoder is written | compile | peak RSS | `.a` |
|---|---|---:|---:|---:|
| v1 | decode to an `Instruction`, then execute | 10.6s | 0.27 GB | 3.1 MB |
| v2 | templates, one instantiation per opcode | 22.8s | 0.46 GB | 8.9 MB |
| v3 | a C++ generator emitting 12,501 lines, compiled | 13.2s | 0.31 GB | 2.5 MB |
| **v4** | **a table read and expanded by the compiler** | **99.9s** | **1.35 GB** | **35.7 MB** |

v3's figure includes building and running its generator (3.3s to compile `MakeZ80.cpp`, and the
generated file is then 4.6s of the total). That is the honest comparison: v3 and v4 do the same job,
one with a program that writes C++ and one with the compiler itself, and **v4 costs about 7.5× as
much wall clock and 4.3× the memory**.

Almost all of it is one translation unit: `Z80.cpp` is 84s of the 100s and the whole 1.35 GB,
because that is where the 1792 `execute_one` instantiations land. `Disassembler.cpp` — the same
parse and every `static_assert`, but no handlers — is the other 15s.

Two things follow. Reflection is not free at this scale, and a talk that shows the technique without
the number is selling it. And the cost is *concentrated*: it is per-instantiation, not per-line, so
it scales with the instruction set times the table count rather than with the size of the
description. That is the number to watch when the 6502 arrives.

Caveats, and they are large: `-O0`; one machine, and a thermally limited laptop at that; and the
sweep is minimum-of-two rather than a distribution. Treat the ratios as real and the absolutes as
indicative.

### What tooling there is, which is not much

**gcc has no `-ftime-trace`.** clang's flag emits a Chrome trace with a span per template
instantiation and per function, which is exactly the tool this question wants; there is no gcc
equivalent, and the option is not recognised. What gcc offers instead:

- `-ftime-report` and `-ftime-report-details` — a table of *passes*, not of symbols. Useful, and
  used below, but it cannot tell you which instantiation or which `consteval` call was expensive.
- `-fmem-report`, `-fpre-ipa-mem-report`, `-fpost-ipa-mem-report` — allocation by pass.
- `-Q` — prints each function as it is compiled. Crude attribution, and it says nothing about the
  front end, which is where this workload lives.

So the only way to see inside is to **profile `cc1plus` itself**. That works: the
compiler-explorer build carries no debug info, but it keeps 48,954 dynamic symbols, which is enough
for a flat profile. `perf record -F 199 -- g++ …` follows the driver's children automatically.

### Where the 90 seconds actually goes

gcc's own accounting for `Z80.cpp`:

| phase | wall | share |
|---|---:|---:|
| parsing | 12.0s | 15% |
| **lang. deferred** (template instantiation and constant evaluation) | **41.3s** | **51%** |
| **opt and generate** (the back end) | **26.1s** | **32%** |
| last asm | 1.0s | 1% |
| — *of which* overload resolution | 11.0s | 14% |
| — *of which* garbage collection | 6.7s | 8% |

5,400 MB allocated through the collector to compile one file.

And the profile of the compiler, sampled at 199Hz over the same build:

| symbol | share |
|---|---:|
| `cxx_eval_constant_expression` | 7.4% |
| `consteval_only_p_walker::walk` | 4.2% |
| garbage collector (`ggc_set_mark`, alloc, marking) | 8.4% |
| `walk_tree_1` | 1.7% |
| **everything named `reflect`/`splice`/`metafn` put together** | **0.37%** |

5,118 distinct symbols were sampled and the top twenty account for 28.6% of them. It is *flat*.

**The headline, and it is not what one expects: reflection is not what costs.** The machinery that
implements `^^`, `[: :]` and `std::meta` is a third of one percent. A third of the build is the back
end compiling the 1792 functions we asked for, which would cost the same if a Python script had
written them. Another seventh is overload resolution — every `cpu.read([:location:])` is an overload
set to resolve, and there are thousands. Actual constant evaluation is under a tenth.

One entry is worth calling out: `consteval_only_p_walker::walk` at 4.2%, roughly three and a half
seconds, is gcc deciding *whether an expression contains an immediate call* — the analysis P2564's
escalation rule requires. It is a tax levied in proportion to how much `consteval` you write, on
code the standard is otherwise encouraging you to write.

### How it scales, measured

By temporarily capping how many tables `all_dispatches` builds:

| decoding tables | compile | peak RSS |
|---|---:|---:|
| 1 | 23.7s | 0.61 GB |
| 2 | 34.2s | 0.76 GB |
| 4 | 58.9s | 0.96 GB |
| 7 | 90.1s | 1.35 GB |

A straight line: **12.6s fixed, then 11.2s and about 0.12 GB per 256-entry decoding table.** So
reading, checking and lowering the description is 14% of the build, and expanding it into handlers
is 86%. (The 1-table build additionally trips `-Werror=pointer-arith` in `execute_instruction`,
which is an artefact of the cap; its time agrees with the line fitted through the other three.)

That is the number to quote when someone asks what a second CPU costs. Not the size of the
description — the size of the instruction set times the number of decoding tables.

### So what could reasonably change

Everything below reduces to one sentence: **the cost is the number of functions the compiler is
asked to write, so the only changes that matter are ones that ask for fewer.**

1. **Fewer tables.** At 11.2s each this is the largest lever, and two are available without
   inventing anything. `fdcb` is `ddcb` with a different index register, and `iy` is `ix` with a
   different index register; both are written out separately today because a rule rewrites
   vocabulary references and not literal text. Making the index register a runtime value in those
   two would take 7 tables to 5 — about **−22s and −0.25 GB, a 25% cut** — at the price of one
   runtime indirection on the rarest instructions in the set. The 338 byte-identical duplicate
   handlers recorded above are the same observation from the other end, and aliasing them would be
   the same win by another route.
2. **Split the translation unit — for wall clock only.** Seven TUs would each pay the 12.6s fixed
   cost, so total CPU goes *up*, to about 167s; but wall clock on four cores falls to roughly 45s
   and on sixteen to about 25s. Worth doing for a developer's edit-build loop, not for CI throughput.
   It needs a change first: `inline constexpr auto dispatches` is a namespace-scope variable, so
   **merely including `Execute.hpp` instantiates all 1792 handlers**, used or not. Found the hard
   way, trying to measure one table by including the header and touching nothing.

### And two things that look like levers and are not

Worth stating because both are where one instinctively reaches first, and the measurements say
neither would repay the effort.

- **Optimising the parse.** The whole of reading, checking and lowering the description is inside
  the 12.6s fixed cost — 14% of the build — of which the `to_array` double evaluation is 3.2s.
  Deleting the parser outright, checks and all, would leave 86% of the build standing. Everything in
  it should be optimised for being read, because that is the only thing it is expensive in.
- **Blaming the back end.** It is 32%, the single largest phase, and it is not reflection's doing:
  it is 29 MB of object code at `-O0`, and a Python script emitting the same 1792 functions would
  pay it identically. The only thing that moves it is emitting fewer or smaller handlers, which is
  item 1 above rather than a separate idea.

### The other implementation: Bloomberg's clang-p2996

There is a second implementation of all this, and "gcc 16 only" is a heavy dependency for a talk to
ask of anyone, so it is worth knowing exactly what it does and does not do. There are in fact **two**
clang forks, and the difference between them matters:

| on Compiler Explorer | fork | clang | flags needed |
|---|---|---|---|
| `clang_bb_p2996` | Bloomberg/clang-p2996 | 21.0.0git | `-freflection -fexpansion-statements -fparameter-reflection` |
| `clang_barry` | brevzin/llvm-project | **23.0.0git** | **`-freflection`** |

Bloomberg's is the reference implementation and is two major versions behind; it also splits the
feature across three switches, so the first two errors one hits are just missing flags. `template
for` is P1306 rather than P2996 and has its own; parameter reflection — the whole mechanism by which
an operation's signature decides what a row may say — has a third. **Barry Revzin's fork wants only
`-freflection`, exactly as gcc does**, with expansion statements and parameter reflection on by
default. That is the one to reach for.

**And the whole thing builds with Barry's, and passes.** Not a probe: the real project, configured
with `CXX=<clang>/bin/clang++ … -DCMAKE_CXX_FLAGS=--gcc-toolchain=…/gcc-16.2.0`, all 1792 handlers,
`ctest` 7/7 green including the 396 disassembly expectations and every diagnostic message. Three
things had to give first:

- **`-fconstexpr-steps=100000000`.** clang's constexpr budget defaults to about a million steps
  against gcc's 33.5 million, and *parsing the description* exceeds it long before any handler is
  instantiated. The diagnostic is "constexpr evaluation hit maximum step limit; possible infinite
  loop?", which is not what has happened and sends you looking in the wrong place. It is the first
  thing anyone doing serious compile-time work on clang will hit.
- **`-Wno-c23-extensions`**, because `#embed` is C23 and this project builds with `-Werror`.
- **Two source fixes, both real.** `apply` captured `[&cpu]` explicitly where only one branch of an
  `if constexpr` names it, so clang rightly flagged an unused capture; a default capture is the
  answer. And the disassembler's relative-jump rendering added a `std::int8_t` displacement to a
  `std::size_t` offset, taking a backwards jump through 64 bits of wraparound to reach the right
  answer by luck. gcc's `-Wconversion` never mentioned it; clang's `-Wsign-conversion` did. **A
  second implementation earned its keep on the first build.**

`cmake/reflection.cmake` now probes for both flags rather than keying off the compiler id, so this
is `cmake --preset debug-reflection` with a different `CXX` and nothing else.

**What it costs.** Same machine, same source, same `-O0`, run adjacently:

| | compile | peak RSS | object |
|---|---:|---:|---:|
| gcc 16.2, libstdc++ | **85.6s** | 1.35 GB | 28.1 MB |
| clang 23, libstdc++ 16.2 | 119.9s | 1.43 GB | — |
| clang 23, libc++ | 144.7s | 1.43 GB | 36.9 MB |

So **clang is about 1.4× slower than gcc on identical source and an identical standard library**,
and the choice of standard library is worth another 20% on top — libc++ is dearer here than
libstdc++, and produces a 31% larger object. A full project build with clang, everything, is 251s.

Two implementations agreeing on the output while differing 1.4× on the cost of producing it is
about the most useful thing this section can say: the expense is inherent to the workload rather
than a quirk of one compiler.

**Both are capable.** Every idiom this spike depends on was tried against both: enumerator splices
resolving an overload set, `members_of` with `access_context::current()` hiding private helpers,
`define_static_array` promoting `nonstatic_data_members_of`, `std::meta::info` as a non-type template
parameter, `parameters_of` in a variable template, `typename[: :]`, member splices, `[:Fn:](…)` in
callee position, `#embed`, and `template for`. The library side is fine too, on libc++ at least:
constexpr `from_chars`/`to_chars`, `ranges::to`, `ranges::contains`, and the `to_array` idiom all
behave, and the whole probe is clean under `-Wall -Wextra`.

One wart to expect: `#embed` warns under `-Wc23-extensions` on both, which this project's `-Werror`
turns into an error, so a clang build wants `-Wno-c23-extensions`.

**But the diagnostics do not survive the move, and that is the finding.** The entire error strategy
here is a `consteval` function that throws, so that a mistake in the description becomes a compile
error carrying the message and the `.cpu` line. Identical source, identical mistake:

```
gcc 16.2:   error: uncaught exception of type 'std::runtime_error'; 'what()':
            'z80.cpu:9: reference names a vocabulary that does not exist'

clang bb:   error: constexpr variable 'bad' must be initialized by a constant expression
            note: subexpression not valid in a constant expression
                      throw table_error(9, "reference names a vocabulary that does not exist");
```

clang points at the `throw` and never prints what it said. The text is right there in the source it
quotes, so a human can read it — but nothing carries it to the top of the error, nothing puts it in
a build log, and an editor jumping to the diagnostic shows "subexpression not valid in a constant
expression" rather than the sentence written for the reader.

**Both forks give that same answer**, so this is not the older one lagging: clang 21 and clang 23
are identical here. **The technique this project uses to make bad tables legible is, today, a gcc
feature.** Worth saying out loud in a talk that recommends it, and worth a bug against clang,
because nothing in the standard prevents printing `what()` — gcc simply chose to.

### And with clang building, `-ftime-trace` finally answers the question

The whole reason the gcc section above is assembled from a pass table and a symbol profile is that
gcc has no `-ftime-trace`. clang does, and it attributes time to *individual template
instantiations*. Same TU, clang 23 with libstdc++, 126s traced:

| phase | seconds | count |
|---|---:|---:|
| Frontend | **119.2** | |
| — `PerformPendingInstantiations` | 85.2 | |
| — `EvaluateAsConstantExpr` | 60.2 | **451,161** |
| — `EvaluateAsInitializer` | 24.4 | 44,675 |
| — `Source` (headers) | 27.9 | |
| Backend | **6.3** | |
| — `CodeGen Function` | 5.9 | 13,982 |
| `CheckConstraintSatisfaction` | 1.7 | 720,105 |

Note the split: **95% front end, 5% back end** — where gcc spent 32% in "opt and generate". The two
are not measuring quite the same boundary, but the direction is stark, and 451,161 constant-
expression evaluations to compile one file is a number that needs no interpretation.

Then the part gcc cannot do at all, time by template (inclusive, so `execute_one` contains the rest):

| template | seconds | instantiations |
|---|---:|---:|
| `refract::execute_one` | **85.1** | 1792 |
| `refract::apply` | 22.7 | 1191 |
| `refract::to_array` | **6.1** | **6** |
| `refract::operands_of` | 4.7 | 1230 |
| `refract::store` | 4.0 | 321 |

**Six instantiations of `to_array` are the entire compile-time pipeline** — the `#embed`, the parse,
the checks, the coverage, the decode tables — and they cost 6.1 of 126 seconds. Individually:

```
3.1s  to_array<Table.hpp:43>   row_opcodes -- opcodes_of_each, the cartesian product walk
1.4s  to_array<Table.hpp:42>   rows        -- parse_rows
1.0s  to_array<Table.hpp:44>   decoded     -- decode_tables
1.0s  all_dispatches<0..6>
0.4s  to_array<Table.hpp:40>   vocabularies
```

That is the "don't bother optimising the parse" claim, confirmed to the individual expression by a
different compiler: **reading the description is 5% of the build.** And 1792 `execute_one`
instantiations at 85.1s is 47ms each, so a 256-entry decoding table costs about 12s — against the
11.2s per table gcc's scaling curve gave. **Two compilers, two entirely different measurement
techniques, agreeing to within 10% on what a decoding table costs.**

The most expensive single handlers are the multi-step rows — `call nz,nn`, `call nn`, `rst` — at
about 0.3s each, six times the average. Nothing surprising, but it is the first time the question
"which instruction is expensive to compile?" has had an answer at all.

To regenerate: add `-ftime-trace -ftime-trace-granularity=200` to the clang build and read the
`.json` beside the object file; it is about 16 MB for this TU.

### What compilers could do, since we are going to keep asking for this

- **Give gcc a `-ftime-trace`.** The gcc half of this section is guesswork assembled from a
  pass-level table and a symbol profile of a stripped binary; neither can answer "which
  instantiation cost me a second", which is the only question an author actually has. Getting clang
  building was worth it for this alone — it answered in one run what three gcc experiments had only
  bounded, and it agreed with them. Nobody should have to port to a second compiler to find out
  where their build went.
- **Make the escalation analysis cheaper.** 4.2% spent asking "is this consteval?" scales with
  exactly the feature it is checking for.
- **The tree representation is not built for this.** 5.4 GB allocated and 8% of the build in the
  collector, to evaluate a 147-line text file and stamp out functions from it.
- **Print `what()`.** gcc does; clang-p2996 does not. A `consteval` function that throws is the
  idiom the whole ecosystem is converging on for compile-time diagnostics, and half the
  implementations currently discard the message.
- **Peak memory is the real ceiling.** 1.35 GB for one TU, growing 0.12 GB per table, is what stops
  this scaling — a CI box running several of these in parallel runs out of memory long before it
  runs out of patience.

## Open: /INT is a level, and v4 has no way to release it

v4 now holds an interrupt request raised while `iff1` is clear, instead of discarding it — which is
right for the case that motivated it, a request arriving inside a one-instruction `di`/`ei` window.
It is wrong for the case it created.

`Z80Base` offers `interrupt()` and nothing else: there is no deassert. `Spectrum::video_line()`
raises the request once a frame, and on real hardware /INT is held for about 32 T-states and then
released. So a routine running under `di` across a frame boundary — loaders, multicolour, border
effects all do this — leaves a request latched, and v4 takes it on the eventual `ei` where hardware,
and v1/v2/v3, take nothing.

Two ways out, both out of scope for the change that found it:

1. **Give the request a release.** `Z80Base` grows a deassert, and `Spectrum` drops the line after
   the documented window. Correct, and it fixes all four cores at once — but it changes shared
   framework and every front end that raises an interrupt.
2. **Give the request a lifetime inside v4.** Record the cycle it was raised at, and expire it after
   ~32 T-states. Keeps the fix's benefit, needs no shared change, and puts a machine-specific number
   inside the CPU where the machine cannot see it — which is the wrong place for it, but a small
   wrong place.

Until one of them lands, v4 differs from the other three in a way real software could notice, and
the difference is *more* wrong than what it replaced for long `di` regions, and *less* wrong for
short ones.

## Done: the line between the library and the Z80

The CPU-agnostic half is now `refract/`, in `namespace specbolt::refract`: `Model.hpp`,
`Lower.hpp`, `Parse.hpp`, `ToArray.hpp`, `Coverage.hpp`, `Pattern.hpp`, `Parser.hpp`, `Vector.hpp`,
`TableError.hpp`, `Machine.hpp`, `Execute.hpp` and `Disassemble.hpp`. The Z80 half is `z80.cpu`,
`Operations.hpp`, `Locations.hpp`, `Table.hpp`, `Disassembler.cpp` and `Z80.hpp`/`Z80.cpp`.

**Both artefacts are now the library's.** `Disassemble.hpp` renders any description, so
`Disassembler.cpp` is four lines: it says where the bytes come from and nothing else. Following
prefixes, filling the DDCB latch, applying a view's renaming and walking the lowered pieces were all
facts about the description rather than about the Z80, and having them in the CPU half meant the two
artefacts followed prefixes by two separate pieces of code. The one thing genuinely left behind is
the assembler syntax `0x` and `+0x`/`-0x` are written in; if a second CPU wants `$1234` that becomes
a parameter, and not before.

What made it possible is `Description` — the five spans a consumer of a parsed table needs
(vocabularies, rows, tables, the decode tables, and where decoding starts) as one value. Passing
"the table" rather than five of its parts is what keeps the signature honest.

The two things that stood in the way both went:

1. **`Execute.hpp` no longer includes any Z80 header by name.** It includes `refract_binding.hpp`,
   which is the whole of the coupling in that direction: it points a namespace alias `target` at
   `specbolt::v4` and includes what lives there. What the framework needs is written down as the
   `Machine` concept, so a machine missing a piece is told which piece rather than finding out
   inside a generated instruction. The one thing the concept cannot state is `read`/`write` for
   locations — the shape of that overload set depends on the machine's own `location_scopes()` —
   so a bad location name is diagnosed at the splice in `find_location` instead.
2. **`SPECBOLT_CPU_TABLE` is a compile definition**, set by `z80/v4/CMakeLists.txt`, with a neutral
   default in `TableError.hpp`. The framework no longer names the description file.

What did *not* go: one binary still cannot hold two descriptions, because `Cpu` and the table
constants are definitions rather than parameters. A second `.cpu` file means a second binary. That
is the limit, and it is what would have to change to test a second CPU alongside the first.

## Idea: could the CPU class *be* the CPU description?

Not done, and half of the reason for it has since evaporated. A name in the table travels through
three places:

1. `z80.cpu` names something — `delay`, `ld8`, `ex_sp_hl`.
2. `find_operation` looks it up in `^^Operations` or `^^Alu`.
3. Some of those static functions turn straight round and call a member of `v4::Z80`.

The *worst* of the shims are gone: what the framework itself needs — `fetch_opcode`, `read_memory`,
`delay`, `displaced_address` — is now stated as the `Machine` concept and called directly on the
chip, and location access is `Z80::read`/`Z80::write` rather than free functions. What is left in
`Operations.hpp` is mostly real semantics, plus a handful that are still pure forwarding
(`Operations::delay`, `ex_sp_ix`, `ex_sp_iy`).

The idea that remains: teach the framework to reflect over **member** functions, so `Z80` itself is
the description and the forwarding goes too. `find_operation` already rejects non-static members
because a splice of one cannot be called without an object; the framework always *has* the object.

Things to work out before committing to it:

- Splicing a member function needs the object: `(cpu.*[:Fn:])(args...)`, or `[:Fn:](cpu, args...)` if
  a reflection of a member function can be called with an explicit object argument. Check what P2996
  and gcc actually allow.
- `takes_cpu` disappears, or inverts: a member has the machine implicitly, so the distinction between
  "wants the CPU" and "does not" stops being visible in the signature.
- Access control does useful work today — `access_context::current()` at namespace scope hides the
  private helpers in `Ops`, so a table cannot name them. The same trick should still work on a class,
  but it decides what a table may call, so check it.
- The naming problem does not go away, it moves: `read`/`write` are overloaded on `Z80` for
  registers *and* memory, and the table needs to tell them apart. Some renaming on `Z80` is probably
  the price, and might be an improvement in its own right.
- `Alu` is a second scope of free functions and would stay as it is, so the mechanism has to keep
  supporting both.

Worth a spike rather than a rewrite: pick `idle`, `read` and `write`, see whether a member splice
works at all, and judge from there.

## What a second CPU would need

The format has described exactly one processor, and an outside reader given only
[CPU_FORMAT.md](CPU_FORMAT.md) — told to know 6502 and Z80 but not to look at the code — went
looking for the seams and found them. Recorded here because "not Z80-specific" is currently a design
intent that has been half-tested, and it should either become true or stop being claimed.

Each of these is a concrete 6502 instruction that cannot be written today.

### 1. An addressing mode must be able to fetch its own operand

The 6502's `aaabbbcc` puts the addressing mode in `bbb` — which is exactly what a vocabulary is
for — but its members are different *lengths*: `#` and `zp` fetch one byte, `abs` two, accumulator
mode none. The encoding column is per-row and fixed, and `parse_member` explicitly refuses:

```
a vocabulary member cannot append an immediate; only the encoding fetches those
```

So the one thing that makes the 6502 compressible is unusable, and every operation group becomes
eight rows instead of one. This is the finding that matters most: it is the difference between the
format's headline claim ("one row, sixty-four instructions") transferring or not.

**The machinery half exists.** `(ix+d)` already causes a fetch that no encoding column declares, and
`displaced_through` already derives per (row, table, opcode) whether that fetch happens. What is
missing is generalising it from *one signed displacement byte* to *an arbitrary operand of a width
the member states*, and making `immediate_bytes` a property of the decoded state rather than of the
row. `check_immediates` would move with it.

That diagnostic was written when a member could not have an access sequence at all. It is now the
main thing standing in the way, and it should probably go.

### 2. Indirection cannot nest

`indirect = "(" , ( "n" | number | name ) , [ "+d" ] , ")"` is one level deep, so `LDA ($20),Y`
(`B1`) — read a pointer from a fetched zero-page address, then index it — has nowhere to go. The
escape is a CPU-supplied `lda_indirect_y` primitive, at which point the table has stopped naming
general operations and the whole argument collapses for that CPU.

### 3. Indexing is hard-wired to a displacement byte

`(ix+d)` means "base plus one signed byte read from the instruction". `LDA $1234,X` (`BD`) is
"16-bit immediate base plus the contents of a register", and there is no syntax for it. The two are
the same idea — a base and an offset — with the offset coming from different places.

### 4. Cost that depends on the data

The 6502 charges one extra cycle on `abs,X`, `abs,Y` and `(zp),Y` **when the index crosses a page
boundary, and only on reads**: `LDA $1234,X` is 4 or 5 cycles, `STA $1234,X` is always 5.

`displaced_address` takes the machine by reference, so it *can* charge conditionally — but it is not
told whether the access it is forming will be a read or a write, so it cannot tell those two apart.
That is a small signature change. The harder half is that we form the address **once per
instruction** (§ *indexed addressing*), which is right for the Z80 and wrong for a machine where the
cost belongs to each access.

### 5. Bus timing is ordered at step granularity

Not a 6502 issue, but the same review raised it and it belongs here. Cost is a total per instruction,
ordered by step; a multi-byte access is indivisible and nothing below a step can be scheduled. Good
enough for totals and for contention at instruction granularity, not enough for a *schedule* of bus
cycles at known offsets. The Spectrum's contention will decide whether this matters — see
*Time passes in exactly one place*.

### What is already fine

Worth saying, so the list above is not read as worse than it is: eight-bit opcodes, no prefixes
needed, conditions, relative jumps, the stack, and page-zero addressing all work today, and the
6502's flag model is no harder than the Z80's. The gaps are addressing modes and their cost, not the
shape of the thing.

## Review findings still open

A review of the whole spike found eighteen things. The correctness ones are fixed and each has a
case in `DiagnosticsTest.cpp` where it can be tested at all; these are the rest, kept here so they
are deferred rather than forgotten.

- ~~**Argument evaluation order.**~~ **Fixed, and it was not theoretical.** `bit n, (ix+d)` has two
  bus-touching operands: it reads memory and then asks for the address that read left on the bus.
  gcc evaluates right to left, so the undocumented flags came from the opcode fetch. Operands are
  materialised into a braced `std::tuple` first, whose initialisation is sequenced.
- ~~**A mistyped line vanishes.**~~ **Fixed.** After blanks and `#`, every line must be a
  declaration or a row; `check_every_line_means_something` says so. Still true and unfixed:
  `next_word` splits on spaces only, so a tab-indented `field` is not recognised at all — `trim`
  handles tabs, which shows they were meant to be whitespace.
- ~~**`SPECBOLT_CPU_TABLE` lives in `TableError.hpp`**, so a framework header names the CPU
  description, and `Execute.hpp` includes the Z80's headers by name for the same reason.~~ **Both
  fixed** — see "the line between the library and the Z80" above. What is left is that one binary
  still cannot hold two descriptions.
- ~~**`Operand` carries jobs that already have types.**~~ **Done.** `Reference` is a type, and
  `write_back_delay` now lives only on `Operand` — `parse_member` writes it there directly, so
  `resolve` is a one-liner and there is nothing to keep in step.
- **The write-back-delay rule compares only the name**, not that both ends are indirect, and two
  nameless indirect operands compare equal. Nothing exercises it today; the rule meant is "the
  destination is the same addressing mode as one of the operands".
- **`Matched::matches` is test-only and misleading** — it is the obvious way to decode and the
  design deliberately does not use it. Either exercise `opcodes_of` in its place or say why it stays.
- **Naming.** "field" means the `.cpu` keyword, the C++ `Field`, a `BitSlice` (in one error message),
  and `Piece::Kind::Field`. One word per concept. `Matched` is a participle for "a parsed opcode
  pattern". `Member::display` is not only for display.
- **The v4 `.cppm` files cannot compile.** v4 is excluded whenever modules are on, so every
  `SPECBOLT_MODULES` branch in v4 is unbuildable by construction, and the partitions do not include
  the headers they would need. They look maintained and are not.
- **`DisassemblerTest` understates coverage.** Around twenty commented-out `CHECK`s are instructions
  the table now covers, including the whole CB `bit`/`res`/`set` block.

## Follow-up work, in order

1. ~~**Multi-slot rows.**~~ Done.
2. ~~**Compile-time overlap and coverage checking.**~~ Done — see §5.
3. **Encoding as a byte sequence.** Immediates and length derived: done. Multiple pattern tokens
   per row, which is what expresses the DDCB fetch order: folded into 4, because it is the same work.
4. **Prefixes.** Table switch and override rows: done, CB works; the goto cycle a view needs is now
   legal, decoding being a loop. Still to do, and the hard half: DD/FD as *views* and DDCB — see the
   Prefixes section.
5. ~~**Timing**~~ Done, though not as this list expected: cost belongs to the addressing mode, with
   an explicit `delay` step only for idle cycles belonging to the operation. `t=` assertions still
   want static per-step costs, which do not exist: cost is resolved inside `Z80::bus`.
6. **Interrupts.** Absent entirely. jsbeeb samples the interrupt at a named position in the cycle
   schedule rather than at the top of the instruction, which is the difference between exact and
   approximate — worth deciding before writing the easy version.
7. **WZ**, flags cross-checks, `undoc` marking. Nothing will catch a regression here until there is
   a zexall run; zexdoc tests documented flags only.
8. **Conditional cycle schedules.** `djnz` 8/13 has no expression today. jsbeeb's `split(condition)`
   forks the remaining schedule, which states both rather than asserting a range.
9. **Project `Row` into artefact tables** rather than scanning at runtime.

Parallel, not blocking: make `RegisterFile` header-only and `constexpr` so execution tests can be
`STATIC_CHECK`. `static_assert(run(0x21, 0x4000).get(R16::HL) == 0x4000)` is the slide the talk
wants, and it is currently impossible only because the accessors are defined in a `.cpp`.

Also parallel: ~~**write a small compile-time vector**~~ — done, `Vector.hpp`, and it is used by
`Field::values`, `Matched::slices`, `Row::pieces` and `Row::steps`. It carries its own count, throws
its caller's message on overflow, and is structural so it can be a template argument.

Worth considering a structural fixed-capacity **string** at the same time. `string_view` being
non-structural has now blocked three separate things — `Row` as an NTTP, `Piece` in an
`inplace_vector`, and a vocabulary member as a template argument — and each time the workaround is to
pass an *index* and look the object up inside. That works, but a structural string type would remove
the class of problem rather than the instances.

The obvious answer for the vector is `std::inplace_vector`, and it is the wrong one *for now*: gcc 16.2 ships
`<inplace_vector>` but its constexpr path supports **trivial types only** —
`__builtin_unreachable(); // only trivial types are supported at compile time`. `Piece` holds a
`std::string_view`, which is trivially copyable but not trivially default constructible, so it does
not qualify. Verified both ways. This is a libstdc++ limitation rather than a language one, so
revisit and delete our version when it lifts.

### Does the table need to survive constant evaluation at all?

Currently yes, and that is worth being deliberate about. `rows` is read **at runtime** — the
disassembler does `rows[*index]` per instruction — so the table is static data, which is exactly why
it cannot hold a `std::vector` and needs the fixed-capacity idiom above.

That is only true because disassembly is *interpreted* while execution is *generated*. Generate
disassembly per-opcode too, the way `execute_one` already works, and nothing needs to survive: `Row`
could use `std::vector` freely inside the parse and the fixed-capacity types would disappear
entirely. Worth deciding on purpose rather than by default, because it also decides whether a
debugger view can ask questions of the table at runtime.

---

## What the real Z80 buys, measured

The description targets `v4::Z80 : Z80Base` rather than a stand-in struct, so v4 can be dropped
straight into `z80/test/OpcodeTests.cpp` — that suite is already a template over the
implementation, which makes it the scoreboard. Two measurements, before and after memory operands:

| | rows | opcodes decoded | unprefixed suite | wrong answers |
|---|---|---|---|---|
| registers only | 18 | 145 / 256 | 73 / 146 | **0** |
| with memory operands | 23 | 181 / 256 | 135 / 192 | **4** |
| with timing steps | 25 | 181 / 256 | 139 / 192 | **0** |
| with CB, addressing modes carrying their own timing | 28 | 182 base + 192 cb | 139 / 192 and 23 / 39 | **0** |

The "wrong answers" column is the one that matters: everything the table describes, it gets right,
including the `pc()` and `cycle_count()` checks that suite makes on every section. The gap is
"not written yet", not "written wrong".

All four wrong answers are the same bug, and it is the one predicted below:

| | want | got |
|---|---|---|
| `inc hl`, `dec hl` | 6 | 4 |
| `inc (hl)`, `dec (hl)` | 11 | 10 |

**Timing is otherwise free.** It falls out of the fetch cycle rather than being data the table
carries: `nop` 4, `add a, b` 4, `add a, n` 7, `ld bc, nn` 10, `ld c, (hl)` 7, `ld (hl), n` 10 — all
correct without the table saying anything about cycles. What is missing is only the *internal*
cycles, which belong to no bus operation and therefore to no step the fetch cycle knows about. That
is exactly the micro-op sequence argument, arriving from the direction of timing rather than of
prefixes, and it now has four witnesses instead of one.

**Immediates are fetched by the framework**, in table order, before the call. Argument evaluation
order is unspecified in C++, so a row with two immediates would otherwise fetch them in whichever
order the compiler picked. Getting this right for free is also why `ld (hl), n` has the correct
fetch-then-write order.

Getting from a fake CPU to a real one and then to memory cost three functions on the customisation
surface: `fetch_immediate`, `read_memory`, `write_memory`.

### Timing: steps, verified

Implemented as design decision 6 says — cost lives in an ordered step list, not in an annotation on
the row. The mechanism is one separator:

```
00pp0011 | inc {p}  | inc16 {p} <- {p} ; delay 2
00110100 | inc (hl) | inc8 (hl), flags <- (hl) flags ; delay 1
```

`delay` is not a framework concept: it is an ordinary primitive in the CPU's `Ops`, and the only
new framework rule is that **a primitive may take `Cpu &` as its first parameter**, which the
framework supplies. That is not the `is_supplied_by_framework` mistake returning — that one
special-cased `Flags`, a *domain* type. `Cpu` is the single type the framework is parameterised on,
so it is the one thing it can always hand over, and it is what `jp`, `call`, `push` and `in`/`out`
will all need.

The conditional part — `inc (hl)` costs one more than `inc r`, and `{4,5,6}` is not maskable — needs
no mechanism either. A specific row placed before the general one wins by first-match-wins, which
§5 already requires. This is the same override mechanism the prefix design depends on, so prefixes
now have a working precedent rather than a promise.

Result: the four timing failures are gone. On the unprefixed suite the failure count is now exactly
equal to the undecoded-opcode count — **zero wrong answers of any kind**.

### Time passes in exactly one place

`Z80::bus(Bus kind, uint16_t address)` is the only function in the CPU that advances the clock.
Every access routes through it: `read_opcode`, `read_immediate`, `read`, `write`, and `idle`. It
takes the address and runs *before* the transfer, so anything scheduled sees the machine as it was
at the moment of the access.

```cpp
enum class Bus : std::uint8_t { opcode, operand, read, write, io_read, io_write, internal };
```

The enum is **per-CPU**, declared in `Z80.hpp` rather than the framework. A 6502 declares its own,
and would add the one kind the Z80 has no use for: a dummy cycle the bus sees but whose value is
discarded — a *write* on the NMOS 6502 and a *read* on the 65C12. I/O is in, because the Z80
genuinely has a separate address space with its own wait state, and separate address spaces are not
unusual.

Contention and cycle stretching are one commented line inside `bus`. Everything they need is already
there: the kind, the address, and `cycle_count()`, from which frame position is `% 70000`. Nothing in
the repo models either today — the Spectrum contends `0x4000-0x7fff` while the display is drawn, and
none of v1, v2 or v3 attempt it.

### What jsbeeb does, and what is worth taking

jsbeeb is cycle-accurate with BBC-specific 1MHz stretching, so it is the right thing to check this
design against rather than guessing.

What it validates:

- **Advance time immediately before the access that ends it.** jsbeeb batches contiguous cycles and
  flushes them with `polltimeAddr(cycles, addr, isWrite)` just before the memory operation, so
  peripherals are caught up to that instant. `bus()` has the same shape.
- **The machine owns the stretch policy, not the CPU.** `polltimeAddr` consults `is1MHzAccess(addr)`;
  `readmem`/`writemem` do no timing at all. Ours matches: `Z80::bus` charges, `Memory::read`
  transfers.
- **The minimum information really is (cycles, address, is-write).** jsbeeb needs no enum of kinds.
  Ours is a didactic layer over the same three facts — worth knowing it is a convenience, not a
  requirement.
- **Cycles that cannot stretch pass no address.** jsbeeb uses plain `polltime` for zero-page and
  stack, which are always fast. The Spectrum differs: an internal cycle still contends on whatever
  the address bus holds, which is why `idle` presents `bus_address_` rather than nothing.

Two ideas worth stealing that we have no answer for yet:

- **`split(condition)`** forks the remaining cycle schedule on a runtime condition — page crossing on
  the 6502, and exactly the shape of `djnz` 8/13. Better than the `t=min/max` sketch in §6, because
  the two schedules are both stated rather than a range being asserted.
- **The interrupt is sampled at a named position in the schedule** — jsbeeb injects `checkInt()`
  before the penultimate cycle. Since v4 does not handle interrupts at all yet, that is the detail
  that makes them exact rather than approximate, and it argues for adding them as a step position
  rather than a check at the top of `execute_one`.

#### Where cost actually lives

Three places, and none of them is a number written on a row:

- **the fetch cycle**, which the CPU's `read_opcode`/`read_immediate` already pay for
- **the addressing mode**, via `read`/`write` costing 3 and `/delay=1` for the idle cycle in a
  read-modify-write
- **an explicit `delay` step**, for an idle cycle that belongs to the operation rather than to an
  operand — `inc {p} ; delay 2`, and `bit {b}, (hl)`

A row states a number only in the third case, which is the case where the number is genuinely a
property of that instruction. Everything else falls out.

#### The steps really do vanish

Verified on this code rather than on a spike, `-O2`, gcc 16.2, against hand-written equivalents:

| | generated | by hand | identical | loops | indirect calls |
|---|---|---|---|---|---|
| `ld b, c` | 9 | 9 | **yes** | 0 | 0 |
| `inc bc` (2 steps) | 58 | 58 | **yes** | 0 | 0 |
| `inc (hl)` (2 steps) | 53 + 30 | 72 | no | 0 | 0 |

No loop over steps, no function pointer, no `Step` data anywhere in the output. `inc bc` and
`ld b, c` are instruction-for-instruction what a human would write, and the `delay 2` folds into a
`lea [rax+2]` on the cycle counter.

The one qualification, which the earlier spike measurement missed because everything in it was
trivial: when a step's primitive is itself out-of-line — `Alu::inc8`, `Z80::read`, `Z80::write` —
gcc declines to inline the `apply<…>` specialisation into the handler, so `inc (hl)` pays one extra
call boundary. The body is still fully specialised straight-line code with no branches; it is just
not merged. Whether to force it with `always_inline` is a tuning question, not a design one, and
the 94 bytes/opcode v3 already ships is the number to beat.

### Addresses are a modifier, not a kind

`(hl)` is not a fifth kind of operand alongside constant, immediate, name and field reference. It is
any of those with `indirect` set — "work out the operand, then use it as an address". That
composes for free: `(bc)`, `(de)` and eventually `(nn)` need no new mechanism, and a vocabulary
member may be written `(hl)` because a member's text is parsed by the same function that parses an
operand in a row. Filling the `-` hole in `field r` with `(hl)` was therefore a one-word change to
the table, and it unlocked 24 opcodes across `ld r,r'`, the ALU group and `inc`/`dec r`.

---

## Where the framework/CPU boundary sits

`Operations.hpp`, `Locations.hpp` and `Z80.hpp` are the whole customisation surface. Retargeting means
writing these and nothing else:

- `Cpu` and `Operations` — the machine state and its non-ALU primitives
- `operation_scopes()` — where the table may name operations from
- `location_scopes()` — where it may name storage
- `read`/`write` overloads — how to touch that storage
- `read_memory`/`write_memory` — how to touch memory through an address
- `fetch_opcode`/`fetch_immediate` — how to read the instruction stream
- `delay` — how to spend an idle cycle

A primitive may take `Cpu &` as its first parameter, which the framework supplies. That is the one
type it is parameterised on, so it is the one thing it can always hand over — and it is what `jp`,
`call`, `push` and `in`/`out` will need.

The framework names no CPU type at all: not `RegisterFile`, not `Alu`, not `Flags`. It knows only
that a row has a verb, some operands and some destinations, and that the CPU can resolve a name.

### The collapse that got us here

Three Z80-isms used to live in the framework, and all three were the same mistake — inferring
meaning from a C++ type rather than reading it off the row:

- `Operand::Kind::Accumulator` presumed a CPU has one.
- `CarrySource` filled a `bool` parameter from the carry flag. Already wrong for
  `Alu::iff2_flags_for(u8, Flags, bool iff2)`, whose `bool` is not carry.
- `is_supplied_by_framework` did the same for `Flags` and `Cpu &`.

All three became one operand concept — constant, immediate, name, field reference, or discard —
where `a`, `carry` and `flags` are just names the CPU resolves. Vocabulary members may append an
operand (`add:add8+0`, `adc:add8+carry`), so the carry policy is data in the table. Destinations
are a list, so `{q} a, flags <- a {r:z}` destructures whatever the primitive returns, and the last
assumption — that a result type has a member called `flags` — went with it.

### What the framework relies on instead

Three properties of the primitive, all read by reflection, none of them Z80-specific:

- its arity, checked against the number of operands the row supplies
- its parameter types, which each operand converts to — so a 16-bit location handed to an 8-bit
  parameter is a `-Wconversion` error, not a truncation
- its return type: `void` means the row may name no destination, a scalar means exactly one, and a
  class means one destination per non-static data member, in declaration order

A `-` destination discards a component, which is how `cp` uses `cmp8` without writing the result
back to `a`.
