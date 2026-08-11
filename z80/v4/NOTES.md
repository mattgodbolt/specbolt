# v4: notes from the spike

Working notes for the table-driven, compile-time v4. Covers what the spike does today, what was
learned about C++26 along the way, and what remains between here and a working core.

Known bugs in v1/v2/v3 discovered while researching this are filed as issues rather than recorded
here.

---

## Where the spike is

`z80.cpu` is `#embed`ed, parsed at compile time, and drives two artefacts:

- **Disassembly.** Five rows (`nop`, `halt`, `ld rr,nn`, `inc rr`, `dec rr`) cover 13 opcodes, all
  byte-exact against the pre-existing `DisassemblerTest` expectations copied from v3.
- **Execution.** A 256-entry dispatch table built with a `template for` expansion statement, one
  `execute_one<Opcode>` instantiation per opcode, each resolving its verb by reflection over `Ops`.

The pipeline is `#embed` → `consteval` parse → lower to validated pieces → `template for` → splice.

Mnemonics are **lowered at parse time** into a fixed `Piece` array (literal chunk, field reference
with field and slice indices already resolved, immediate slot). `Row::length` is derived from that.
The disassembler therefore parses nothing at runtime: it walks pieces and emits.

Malformed tables are compile errors carrying the source line, e.g.

```
z80.cpu:9: mnemonic names a field that does not exist
z80.cpu:9: field has the wrong number of values for its opcode bits
z80.cpu:9: no operation 'ld17' in Ops
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

- **`template for` + `-Wshadow` is a gcc bug** — [PR c++/124197][pr124197], unconfirmed as of writing.
  Each expanded copy is reported as shadowing the previous, though nothing is shadowed: every copy is
  its own scope. Minimal repro:

  ```cpp
  constexpr std::array<int, 3> values{1, 2, 3};
  int sum = 0;
  template for (constexpr auto value : values) { sum += value; }   // 3 spurious warnings
  ```

  Since `-Wshadow -Werror` is exactly this project's setting, expansion statements are unusable
  without a local `#pragma GCC diagnostic ignored "-Wshadow"`. Remove the pragma when the PR lands.

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

### 1. Encoding is a byte sequence, not one 8-bit pattern

`parse_opcode_bits` requires exactly 8 characters. Real encodings are up to four bytes, and `DD CB d
op` puts the displacement **between** the prefix and the opcode. Proposal: the first column becomes a
whitespace-separated token list of literal bytes, bit patterns and operand names.

```
DD CB d 01bbbzzz | bit {b}, (ix{d}) | ...
00pp0001 nn      | ld {p}, {nn}     | ld16
```

Length then falls out of the token count and is never stated. Fetch order is textual order, which is
what timing needs. Note the execution path currently takes a single pre-fetched operand, which cannot
express `ld (ix+d), n` (two immediates, fetched at different points).

This also settles the DDCB fetch inversion for free: `DD CB d 01bbbzzz` states left to right that the
displacement precedes the opcode, so no per-table "the operands come in this order" declaration is
needed.

### 2. Vocabulary members must be structured

A member is currently just a name. But in `r = b c d e h l (hl) a`, member 6 changes storage class,
timing (`+1` T-state), and the flag inputs to `Alu::bit` (bus noise comes from `wz` when indirect).
There is also a ninth pseudo-member `$nn` with no bit encoding, used so one ALU row serves both
`add a,(hl)` and `add a,nn`.

Members need a kind and attributes; vocabularies need holes (patterns that must not match) and empty
members (the ED block group concatenates three vocabularies, one containing `""`).

**This is also where 6502 support lives or dies.** On the Z80 exactly one member is special. On the
6502 *every* member of `bbb` is an addressing mode that changes length, cycles and whether the row
exists at all. Design the member record for the 6502 case and the Z80 becomes the easy instance.

### 3. Rows need several slots, and slot ≠ letter

`execute_one` hard-codes `slices[0]`; there is now a `static_assert` so a second field fails loudly
rather than vanishing. `01yyyzzz | ld {y}, {z}` needs the same vocabulary bound to two different
slices, so field name and slice letter must be decoupled — `{vocab:slice}`. Verb arity and parameter
kinds should then be checked by reflecting `parameters_of` on the resolved primitive.

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

### 4. Row is parse-time only

Project it at compile time into artefact-shaped tables: `winner[256]` (kills the runtime linear scan
*and* enables overlap detection), decode, dispatch, text, cycles, flags-affected. The `line` field
should propagate into every projection so any mismatch reports `z80.cpu:N`.

### 5. First-match-wins needs compile-time checking

`halt` (`01110110`) collides with `ld r,r'` (`01yyyzzz`), and today the winner is decided by line
order, silently. All three prior implementations treat this as a hazard needing an explicit
statement. Since the domain is 256, build the winner array at compile time and **error** on
unreachable rows and on overlaps where the earlier row is not a strict subset of the later one,
unless explicitly marked as an intentional override. Report uncovered opcodes.

### 6. Timing attaches to the micro-op sequence

Not to rows, and not to operands. Evidence from this repo:

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

---

## Prefixes

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

## Follow-up work, in order

1. **Multi-slot rows.** `{vocab:slice}`, verb arity from `parameters_of`, remove the
   `num_slices <= 1` `static_assert`. Unblocks `ld r,r'` and the whole ALU group — the bulk of the
   base table.
2. **Compile-time overlap and coverage checking.** Before rows 6–250 are written, because `halt` vs
   `ld r,r'` starts biting the moment the second lands.
3. **Encoding as a byte sequence.** Prefixes, immediates, displacements; length derived. Also
   prerequisite for prefixes, since it is what expresses the DDCB fetch order.
4. **Prefixes** — `goto <table> [with <view>]`, derived tables, override rows.
5. **Timing**, as an unrolled step sequence with `t=` assertions.
6. **WZ**, flags cross-checks, `undoc` marking.
7. **Project `Row` into artefact tables** rather than scanning at runtime.

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

## Where the framework/CPU boundary sits

`Z80Cpu.hpp` is the whole customisation surface — 70 lines. Retargeting means writing one of these
and nothing else:

- `Cpu` and `Ops` — the machine state and its non-ALU primitives
- `primitive_scopes()` — where the table may name operations from
- `location_scopes()` — where it may name storage
- `read`/`write` overloads — how to touch that storage

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
