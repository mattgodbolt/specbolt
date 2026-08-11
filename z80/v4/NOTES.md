# v4: notes from the spike

Working notes for the table-driven, compile-time v4. Covers what the spike does today, what was
learned about C++26 along the way, and what remains between here and a working core.

Known bugs in v1/v2/v3 discovered while researching this are filed as issues rather than recorded
here.

---

## Where the spike is

`z80.cpu` is `#embed`ed, parsed at compile time, and drives two artefacts. 28 rows in two decoding
tables cover 182 of 256 base opcodes and 192 of 256 CB opcodes:

- **Disassembly.** Walks the row's lowered pieces, following a `goto` through a prefix table.
- **Execution.** A 256-entry dispatch table per decoding table, built with a `template for`
  expansion statement, one `execute_one<Table, Opcode, Index>` instantiation per entry, each
  resolving its verb by reflection over the CPU's `primitive_scopes()`.

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
out of bounds, so the parser cannot overflow its arrays even if the counting pass desynced. Every
correctness hole found in the first cut of the spike was in the half left at runtime. Lowering the
table to a validated fixed shape at parse time removes that half entirely.

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
  splice: `[: find_verb(row.verb) :]`, never `[: stored.fn :]`.
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
- **Clang has no reflection at all** — 22.1 and trunk both lack `<meta>`. The wasm build is clang, so
  v4 is excluded in CMake via `if (SPECBOLT_HAS_REFLECTION)` rather than by `#ifdef`s in source.
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
  `define_static_array` requirement, not a constexpr one. So count-then-fill into a `std::array`
  sidesteps the whole problem and lets `Row`/`Field` keep plain `string_view`s into the blob.
- Transient allocation is fine: a `std::vector` may be created and destroyed inside one constant
  evaluation and passed between `consteval` functions freely. It just cannot escape into a
  namespace-scope `constexpr` variable.
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
- The range must be a constant expression. A `std::array` built inside the same lambda does not
  qualify; hoist it to namespace scope as `inline constexpr`.
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

**Fix: resolve members to real C++ entities by reflection**, the same trick `find_verb` already uses
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

### Status: the table switch works; the views do not exist yet

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
- **The immediate is fetched once, before any step**, rather than at the token that names it. Fine
  for every row that exists; wrong for `ld (ix+d), n`.

Found by this audit and fixed rather than recorded: `scf` and `ccf` had `a` as a destination, but
`Alu::scf`/`ccf` return the accumulator unchanged, so the rows claimed a write that never happened.
They now discard, as `cp` does. Harmless in behaviour, wrong as documentation -- and the table is
documentation.

**Not done, and the hard half:** DD/FD as *views* over an existing table, and DDCB. What exists is a
table switch, which CB and ED need and which is the easy case. A view has to rewrite `{field}`
references without touching literal text, and DD re-enters the table it came from — a cycle. The
generator unrolls by instantiating one table from another, so a cycle would not terminate;
`check_no_goto_cycles()` currently rejects one with a clear message rather than melting the
compiler. Lifting that is the loop model below, and it is the next real piece of work.

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

### The model: `goto <table> [with <view>]`

One primitive. Prefix rows are ordinary rows.

```
table base
  11001011 | (cb) | goto cb
  11101101 | (ed) | goto ed with view=hl     # ED discards a pending DD/FD
  11011101 | (dd) | goto base with view=ix   # re-enter the same map, renamed
  11111101 | (fd) | goto base with view=iy

table ix = base with hl->ix, h->ixh, l->ixl, (hl)->(ix+d)
  11001011 | (dd cb)          | goto ddcb          # override: a jump, not a rename
  01yyy110 | ld {r8:y}, (i+d) | ld_from_indexed    # override: half-registers stay real
  01110zzz | ld (i+d), {r8:z} | ld_to_indexed
```

A derived table may carry **override rows** that shadow the derived ones by first-match-wins — which
§5 already requires anyway, so overrides cost no new mechanism. That is what removes the two
special-purpose mechanisms an earlier sketch needed: no `view=` guard for routing DD CB, and no
`view-rule` for half-register suppression. Both become rows a reader can see.

Stating DD as "re-enter the table you were already in" is also more honest than "set a mode": it
makes clear a full opcode fetch follows, with its 4 T-states and R increment.

State remains (table, view) — seven legal combinations, exactly v2's seven tables. Prefix chains
(`DD DD FD`) fall out: each just re-enters with a different view, last wins.

**One rule to keep: only `{field}` references are rewritten; literal text never is.** `ex de, hl`
written literally is therefore immune by construction. Substitution-by-default would reproduce the
exact bug v2 and v3 both have.

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

The undocumented register copy is `-> {r8:z}`, with `.` as the "no destination" member for `z == 6`.

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

## Review findings still open

A review of the whole spike found eighteen things. The correctness ones are fixed and each has a
case in `DiagnosticsTest.cpp` where it can be tested at all; these are the rest, kept here so they
are deferred rather than forgotten.

- **Argument evaluation order.** `value_of` is not pure — it can advance the clock and set the
  address bus — and pack-expansion argument order is unspecified. No row today has two bus-touching
  operands, so nothing is observably wrong, but that is a property of `z80.cpu` rather than of the
  framework. Fix: materialise into a braced `std::tuple{…}`, which is sequenced, and `std::apply`.
- **A mistyped line vanishes.** `is_row` needs a `|`, so `00000000 nop nop` parses as nothing and
  fails at *runtime* with "no row decodes opcode 0x00"; `feild r = …` is reported much later, at
  whatever references `r`. Fix: after blanks and `#`, every line is a directive or a row, so let the
  missing `|` be the diagnostic. Related: `next_word` splits on spaces only, so a tab-indented
  `field` is not recognised at all.
- **`SPECBOLT_CPU_TABLE` lives in `TableError.hpp`**, so a framework header names the CPU
  description; `Execute.hpp` includes `Z80Cpu.hpp` by name for the same reason. Both should be
  `target_compile_definitions`, which is also what would let one binary hold two CPUs. Until then,
  "retargeting means writing one `Z80Cpu.hpp`" needs an asterisk.
- **`Operand` carries jobs that already have types.** `field_index` + `slice_index` *are* a
  `Reference`, spelled a third time in `Piece`. `write_back_delay` exists on both `Member` and
  `Operand`, copied down by `resolve` with nothing saying so.
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
4. **Prefixes.** Table switch and override rows: done, CB works. Still to do, and the hard half:
   DD/FD as *views*, DDCB, and the goto cycle that views require — see the Prefixes section.
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

Also parallel: **write a small compile-time vector and use it everywhere instead of
`std::array` plus a separate count member.** The idiom appears three times already —
`Field::values`/`num_values`, `Matched::slices`/`num_slices`, `Row::pieces`/`num_pieces` — and each
site hand-rolls its own bounds check and denies itself range-`for` and algorithms. One
`constexpr`-friendly fixed-capacity vector with `push_back` (throwing on overflow, which is a compile
error during constant evaluation), `size()`, `begin()`/`end()` and `operator[]` removes all of that.

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

`Z80Cpu.hpp` targets `v4::Z80 : Z80Base` rather than a stand-in struct, so v4 can be dropped
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

`Z80Cpu.hpp` is the whole customisation surface — 82 lines. Retargeting means writing one of these
and nothing else:

- `Cpu` and `Ops` — the machine state and its non-ALU primitives
- `primitive_scopes()` — where the table may name operations from
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
