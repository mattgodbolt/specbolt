# The journal

What was decided, in the order it was decided, and why. Each entry is a snapshot: it was true
when written and describes the state at that moment, so read it as a record rather than as a
reference. What is true *now* is in [NOTES.md](../NOTES.md), and the code.

**New "Done:" entries are appended here.**

---

## The data-model decisions, and how each landed

*Written when these were open, and kept because the reasoning is why the data has the shape it
has. All but one are settled: what remains open is WZ/MEMPTR, in §8 below and in
[NOTES.md](../NOTES.md). The types named here were later renamed, `Field` to `Reference` and
`Matched` to `Pattern`, and the references were spelled `{p}` before a vocabulary had to be named,
so read the examples for their argument rather than their syntax.*

These dictate the row data shapes and are expensive to retrofit. Everything else is additive.

### 1. Encoding is a byte sequence, not one 8-bit pattern (partly done)

The encoding column is now a token sequence rather than a single pattern:

```
00pp0001 n n | ld {p}, $nnnn | ld16 {p} <- n
110qq110 n   | {q} a, $nn    | {q} a, flags <- a n
```

`length` is derived from it, where it used to be inferred by counting `$n`s in the *display* text, a human-facing string deciding how many bytes the CPU fetches. The fetch itself is now driven by
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
step runs, which cannot express `ld (ix+d), n`, two immediates read at different points. Fetching
per-token, at the point the token appears, is the fix, and it is the same change that makes multiple
pattern tokens work.

### 2. Vocabulary members must be structured, the timing half is done

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

That works, and the mechanism cost nothing, **a latch is just a location**, so the CPU declares it
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

### 3. Rows need several slots, and slot ≠ letter (done)

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
  C++ spelling, `(hl)` has no enumerator).

**There is more than one kind of vocabulary**, and a member should declare what it binds to:

| vocabulary | binds to | example |
|---|---|---|
| `p`, `rp`, `r` | an enumerator | `bc` → `RegisterFile::R16::BC` |
| `alu` | a primitive function | `add8` → `Alu::add8` |
| `bit`, `rst` | a plain number | `0..7`, `0x00..0x38 step 8` |
| `cc` | a condition primitive plus display text | `nz` |
| memory members | nothing nameable | `(hl)`, `(ix+d)`, display text plus a `mem:` kind |

This is the same axis as the structured-members decision in §2 and subsumes it: "is this member
special?" and "what does this member bind to?" are one question. Design the member record to answer
it once.

### 4. Row is parse-time only (partly done)

`decoded[table][256]` exists: it killed the runtime linear scan and is what precedence checking and
coverage are computed from. Text is projected into `pieces` at parse time. Cycles and
flags-affected are not projected, cost is resolved during execution from the addressing mode and
the step list, and nothing consumes a flags-affected table yet. The `line` field propagates
everywhere, so every diagnostic reports `z80.cpu:N`.

### 5. First-match-wins needs compile-time checking (done)

`halt` (`01110110`) collides with `ld r,r'` (`01yyyzzz`), and the winner was decided by line order,
silently. All three prior implementations treat this as a hazard needing an explicit statement.

Implemented as `check_row_precedence()`, a `static_assert` over the winner array. Two rules:

- a row must win at least one opcode
- where two rows overlap, the earlier must be **wholly contained** in the later, that is an
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

Coverage needs no ratchet: `check_tables_total` proves every opcode of every table decodes, so a
count asserted by a test could only ever restate what the build already refuses to do without. It
was there for a while and has been removed. Because precedence is checked too, coverage cannot be
gained by silently shadowing another row.

### 6. Timing attaches to the micro-op sequence, done, and it moved

Landed, but not where this section predicted. Cost turned out to belong to the **addressing mode**
for anything to do with an operand, with an explicit `delay` step only for idle cycles belonging to
the operation itself. See "Where cost actually lives" and "Time passes in exactly one place" below.
The original argument, which still stands:

- v3 has **no cycle numbers anywhere**. Timing emerges from the primitives (`read`/`write` = 3,
  opcode fetch = 4) plus `pass_time(n)` interleaved at the right points.
- v1 *did* attach times to operands and it collapsed, `access_time(Operand)` carries the comments
  "Doesn't make sense but I am just trying to fix up the bad timings" and "Heinous hack to make this
  flawed approach agree with reality".
- Cost is conditional (`djnz` 8/13), depends on a vocabulary member (`y == 6` adds 1), and depends on
  a per-row quirk (the DD/FD displacement prologue costs 5 normally but 2 for load-immediate rows, v3 carries a whole `is_load_immediate` field just for this).

So: primitives declare their own cost, the semantic column is an **ordered list of steps** with
explicit idle delays, and a row-level `t=` is an *assertion* checked at compile time rather than a
definition. Conditional rows want `t=min/max`, which is also what a timing view wants.

This is floooh/chips' micro-op model, and converging on it is fine, chips is cycle-exact and was
retargeted to the 6502, so it is evidence the model generalises. The difference is the mechanism:
chips generates ~1600 numbered step cases from a Python script emitting C, with nothing type-checking
the output until the next compiler run. Here the steps are a compile-time description, resolved
against real C++ signatures by reflection, with errors pointing at the `.cpu` line.

**Critically, the steps are unrolled away, nothing resembling a state machine survives into the
binary.** Verified rather than assumed: `DD CB d 00` built as an 8-step sequence dispatched through
`template for`, each step spliced by reflection, produces assembly **byte-identical** to the
hand-written equivalent at `-O2` (17 instructions), and the five separate `pass_time` contributions
(3+5+3+1+3) fold into a single `addq $15`. The latch never reaches memory. Variable-length sequences
fold too.

Code size measured at 58.5 bytes/opcode unrolled, against **94 bytes/opcode that v3 already ships**
(165 KB for its seven dispatch functions). Unrolling is smaller than the status quo, not larger.

What unrolling forfeits is mid-instruction suspension, the ability to drive the CPU cycle by cycle,
which is exactly why chips keeps its runtime state machine. specbolt has never had that: v1, v2 and
v3 are all instruction-stepped with `pass_time` accumulating T-states, and there is no memory
contention model anywhere in the repo. Contention stays reachable later, since each step carries its
cycle offset. What is genuinely given up is floating-bus reads and interrupt acceptance at a
sub-instruction boundary.

**The step model is for the semantic column only.** The encoding column stays a bit-pattern token
sequence, because instruction length, `winner[256]` and coverage checking all need patterns, you
cannot derive them from a step list without symbolically executing it. chips does not have this
constraint because chips does not disassemble; v4 produces two artefacts from one table, which is the
whole point.

It also retires v3's `is_load_immediate` boolean. The 5T-vs-2T displacement prologue exists only
because the fetches of `d` and `n` interleave with the internal delay; as an ordered step list that
is simply *stated* rather than special-cased.

### 7. Flags come from the primitive signature (mostly)

The table never mentions F. The CPU holds the flags; `Alu` returns `ResultT<T>{result, flags}` and
the framework routes the flags half into the register file. **Spiked and working** against the real
`Alu` shapes, all four calling conventions fall out of reflection alone:

| primitive | derived wiring |
|---|---|
| `R8 add8(u8, u8, bool)` | carry spliced in; result → destination, flags → F |
| `R8 and8(u8, u8)` | no flag input at all |
| `R16 add16(u16, u16, Flags)` | whole flag word spliced in |
| `Flags bit(u8, u8, Flags, u8)` | flags only, destination untouched |

Parameter types decide what is read; the return type decides whether a value is written. Verified by
`static_assert` that `bit` leaves A alone while `add8` writes it, purely from the signatures.

**But the signature is not sufficient on its own.** `add8` is shared by ADD and ADC: v3 calls
`add8(a, rhs, false)` for one and `add8(a, rhs, flags().carry())` for the other. Same for
`sub8`/SUB/SBC. So "a `bool` parameter means splice the carry" is wrong half the time, and nothing in
the signature distinguishes them.

The policy belongs on the **vocabulary member**, which is where the encoding already puts it, `10ooozzz`'s `ooo` field *is* the add/adc/sub/sbc/and/xor/or/cp vocabulary. So a member binds to a
primitive **and** a calling policy for whatever the signature leaves ambiguous. Verified: `add` and
`adc` route through the same `add8` and differ only in the member's carry source.

Refined claim: **the signature determines the shape; the vocabulary member determines the policy for
what the shape leaves ambiguous.** Weaker than "the signature is the whole declaration", still beyond
anything a text-emitting generator can check.

Residues that need saying explicitly regardless: partial preservation (`in r,(c)`), extra inputs
(`bit`'s bus noise), and the block ops, which will never be table-expressible and get a bespoke
primitive.

### 8. WZ/MEMPTR is per-instruction data

Cannot be inferred, and all three existing versions are incomplete, nothing sets WZ for `ld a,(nn)`,
`ex (sp),hl`, `jp nn`, `call` or `out (n),a`, all of which the real chip updates and all observable
through `bit n,(hl)`. An optional per-row attribute, defaulting to "a memory operand sets WZ to the
effective address" (which is what all three versions already do).

v4 does not model it either. `bit {b}, (hl)` names `h` as its bus-noise source, which is HL's high
byte and therefore right for that one instruction, the same approximation v3 computes. The reason
nobody notices is structural: the only regression test in the repo is **zexdoc**, and "doc" means
documented flags. There is no zexall, so undocumented-flag behaviour is untested repo-wide apart
from a handful of hand-written checks in `OpcodeTests.cpp`, which is exactly where v4's two
failures showed up, and both are now fixed.

---

## Done: a view is a parameter, not a copy

The compile-time work above says the cost is the number of functions the compiler is asked to write,
and that two of the seven decoding tables existed only because the format could not say "the same
table again, with a different register". It can now. **Built, and measured both ways at the end of
this section.**

### What used to be duplicated

```
table ix = base with pair.hl -> ix, spair.hl -> ix, reg.h -> ixh, reg.l -> ixl, reg.(hl) -> (ix+d)/delay=1
table iy = base with pair.hl -> iy, spair.hl -> iy, reg.h -> iyh, reg.l -> iyl, reg.(hl) -> (iy+d)/delay=1
```

Two declarations differing in one register. Below them, `ddcb` and `fdcb` are five rows each, written
out twice because a rule rewrites vocabulary references and never literal text, and those rows name
`(ix+d)` literally. `Operations::ex_sp_ix` and `ex_sp_iy` are a third instance of the same thing:
two functions forwarding to one, existing only so a row can name each spelling.

The bill is 2 table declarations, 10 rows where 5 would do, 2 redundant operations, and **512 of the
1792 handler instantiations**, which the scaling law prices at about 22 seconds and 0.25 GB.

### The one new idea

A vocabulary member is chosen today by *a slice of the opcode*: `{reg:z}` means "vocabulary `reg`,
selected by slice `z`". The whole proposal is that a member may instead be chosen by **a parameter
of the table it is decoded in**, a selector that arrives with the decode state rather than in the
instruction. Everything else is machinery that already exists.

```
vocab index     = ix      iy
vocab index_hi  = ixh     iyh
vocab index_lo  = ixl     iyl
vocab index_mem = (ix+d)  (iy+d)

table indexed(view: index) = base with
    pair.hl  -> {index:view},   spair.hl -> {index:view},
    reg.h    -> {index_hi:view}, reg.l   -> {index_lo:view},
    reg.(hl) -> {index_mem:view}/delay=1

table base
  11011101 | (dd) | goto indexed(ix)
  11111101 | (fd) | goto indexed(iy)
```

`view` is declared as a parameter drawn from `index`; `{index_hi:view}` is an ordinary reference
whose selector happens to be the parameter rather than an opcode slice. A `goto` supplies it by
naming a member, and (the part that kills the second duplication) may also *forward* the parameter
it was itself decoded under:

```
table indexed(view: index)
  11001011 d | (dd cb) | goto indexed_cb(view)

table indexed_cb(view: index)
  00yyy110 | {shift:y} {index_mem:view} | {shift:y} {index_mem:view}/delay=1, flags <- {index_mem:view}
  ...
```

Ten rows become five, and `fdcb` disappears.

### What changes in the model

- `Reference` gains a discriminator: the selector is an opcode slice or the table's parameter.
- `TableDecl` gains an optional parameter, a name and the vocabulary it is drawn from.
- `Transfer` gains a byte, exactly as it already carries the DDCB displacement latch. The decode
  state becomes (table, displacement, parameter) instead of (table, displacement).
- `member_of` (still the only place a reference is followed) takes the parameter alongside the
  opcode. That is the whole of the resolution change.

### How the interpreter lowers it, and this is the real decision

A parameter-selected reference is not known when `execute_one` is instantiated, so `call_for` can no
longer fold it away. Two ways out, and they differ in exactly the thing being optimised:

**(a) Machine-resolved.** The member's *location* becomes one the machine selects at run time: the
CPU declares `read(IndexPair, std::uint8_t which)` and friends, and the framework splices the
vocabulary's location, passing the ordinal. One body per opcode, no duplication at all, 256
instantiations for `indexed` and 256 for `indexed_cb` against 1024 today. **This is the option that
actually collects the 22 seconds.** It costs an indexed load on prefixed instructions, and it is
precisely floooh/chips' `hlx[hlx_idx]` trade, which is evidence it generalises.

**(b) Generated switch.** Expand the row body once per member of the parameter vocabulary under a
runtime compare, using the `template for` the file already leans on. No change to the machine at
all. But a row that names the parameter duplicates its body, so `indexed_cb`, where every row does, saves nothing, and `indexed` saves only the 169 of 256 opcodes that DD leaves alone. Roughly a
third of the win for none of the machine changes.

**(a) is what was built.** (b) is the tempting one because it changes less, and it is worth writing
down that it does not pay: the measurement says cost follows *bodies instantiated*, and (b) keeps
most of them.

A refinement worth taking with (a): in `indexed_cb` every row addresses through the same `(i+d)`,
and the prefix already knows both the register and the displacement. If the prefix formed the
effective address and `Transfer` carried *that*, `indexed_cb` would need no parameter at all for
execution, only for display. That is also closer to the hardware, which forms the address before
fetching the opcode, and it is why the opcode read counts inside the address window.

### The disassembler needs almost nothing

It already resolves references at run time, `member_of` with the opcode it just read. A
parameter-selected reference is the same lookup with a different index, and it already tracks a
displacement latch it can track a parameter beside. This is the half that usually costs the most in
a change like this, and here it is nearly free.

### What must be checked, all at compile time

- A `{v:param}` reference in a table with no parameter, or naming a parameter that is not the
  table's, is an error.
- A `goto` into a parameterised table must supply a parameter, a literal member or a forwarded one, and a `goto` into an unparameterised table must not.
- Parallel vocabularies must have as many members as the parameter vocabulary, exactly as a
  slice-selected vocabulary must match its slice width today. `check_reference` already does this
  arithmetic; it needs a second source for the count.
- No member of a parameter vocabulary may be a hole, because coverage is computed per (table,
  opcode) and must not depend on the parameter. Worth requiring rather than generalising coverage.

### What it does not solve

Nothing about `ed`, `cb` or timing changes. `ex_sp_ix`/`ex_sp_iy` collapse into one operation only
if their rows name `{index:view}` rather than spelling the register out, which is available, and is
the same fix as everything else here. And it does not reduce `base`: DD/FD are rare, so the runtime
cost lands where it is least felt, which is the whole reason the trade is worth making here and
would not be worth making for `hl` itself.

### What it actually cost and saved

Everything below is the same source, one change, measured both ways.

**The description.** Seven tables became five, 127 rows became 111, and `Operations::ex_sp_ix` and
`ex_sp_iy` (two functions that forwarded to a third) went with them. `ddcb` and `fdcb` are one
`indexed_cb`, and `ix` and `iy` are one `indexed`.

**Compile time.** Better than predicted, on both compilers:

| | before | after | |
|---|---:|---:|---:|
| gcc 16.2, `Z80.cpp`, `-O0` | 85.6s / 1.35 GB | **52.6s / 1.17 GB** | **−38%** |
| gcc 16.2, `RelWithDebInfo` (36-core box) | 54.0s | 41.4s | −23% |
| clang 23, traced | 125.8s | 101.1s | −20% |

**And it landed exactly where the design said it would.** clang's per-instantiation trace:

| | before | after |
|---|---:|---:|
| `execute_one` | 85.1s over **1792** | 67.9s over **1280** |
| `apply` | 22.7s over 1191 | 20.5s over 895 |
| `operands_of` | 4.7s over 1230 | 4.0s over 934 |
| `EvaluateAsConstantExpr` | 451,161 calls | 348,413 calls |

1280 is exactly 5 × 256. Not one handler more than the two removed tables predicted.

Two second-order effects worth having. Each surviving handler is *dearer*, 53ms against 47ms, because it now reads its index register through a selector. And `apply` fell further than the tables
alone explain (1191 → 895, −25% against −29% for tables), because merging `ix` and `iy` made `Call`
values that differed only in which register they named identical, so they share one instantiation.

The gcc saving (−38%) is larger than the scaling law's −22s prediction, and larger than clang's
−20%. Two 256-entry tables is the floor; deduplicating `Call` values is the rest, and gcc collects
more of it.

**Run time, and here the design note was wrong.** zexdoc, interleaved on an idle 36-core machine,
minimum of four: **9.07 ns/instruction before, 9.25 after, about 2% slower**, consistently, in
three of four pairs. A game benchmark could not resolve it at all (0.083 to 0.176 ms/frame across
three runs of the same binary).

Two percent is more than expected, and the reason is a real design error rather than the index
indirection. The note above argued the cost "lands where it is least felt", because only prefixed
instructions read through the selector. But **the dispatch table's function pointers must all have
one signature**, so every handler now takes the view whether it uses it or not, and `nop` pays for
`ix` existing. The indirection is confined to prefixes; the *parameter* is not.

Fixing that means not having a uniform handler signature, which means not having a function-pointer
dispatch table, which is the thing expansion statements cannot give us anyway. So it stands as the
price: **a quarter to a third off the build, for about 2% of run time.** For a talk that is a good
trade to be able to state in both directions; for a shipping emulator it is a judgement call, and
the honest framing is that it buys developer time with user time.

**One more thing clang caught.** Two of the new lines had sign-conversion bugs that gcc's
`-Wconversion` accepted, a ternary yielding `int` used as an array index, and the earlier
relative-jump arithmetic. Having a second compiler on the same source paid for itself twice in one
session.

## Done: one function per instruction, which cost 16s and now saves 4

`execute_one` used to be generated per (table, opcode), 256 per table, always.
But a row's body is built only from the slices it *reads*. `ed`'s catch-all row claims 218 opcodes
and reads none of them, so those are one instruction wearing 218 hats; `ld {reg:y}, {reg:z}` reads
both its slices, so its 64 are genuinely 64. Generating per opcode cannot tell the difference.

Generation now walks rows and splats each body across the opcodes it claims. The combinations of the
slices a row reads enumerate its distinct bodies exactly once, so **nothing is deduplicated, because
nothing is generated twice**, and there is no search anywhere: the fill is a direct write.
`template for` is left doing only the thing it must, making the functions; filling 256 entries from
them is an ordinary loop, because by then they are values.

**It is a better shape and it costs 16 seconds.** Measured, on the same TU:

| | gcc `Z80.cpp` | peak | `execute_one` | `apply` | `operands_of` |
|---|---:|---:|---:|---:|---:|
| per (table, opcode) | **52.6s** | 1.02 GB | 1280 | 895 | 934 |
| canonicalised, with a cross-table search | 56.5s | n/a | **864** | 895 | 934 |
| per instruction (this) | 68.9s | 1.17 GB | 1034 | 895 | 934 |

Look at the last two columns. **`apply` and `operands_of` do not move.** They are keyed on
`<Fn, Call>`, so the duplicate handlers were already sharing every expensive instantiation beneath
them, thin shells around work the template system had folded all along. Removing 246 of them saves
nothing, and the analysis that finds them costs. The linker folds the shells too, since gcc runs
`-fipa-icf` at `-O2`, so they were never in the binary either.

Three attempts, all slower: a cross-table search (+4s), a naive row walk that was `rows × 256` where
`256` would do (+15s and a doubled peak), and this one (+16s). The conclusion is not "my
implementation was bad" (the third is O(256) per table with no search at all) it is that
**the instantiations this removes are nearly free, and finding them is not.**

Kept anyway, for the shape: generation follows the description's own grain rather than sweeping an
opcode space, and the 256-entry `template for` is gone. That was a taste judgement with a price tag
on it, which is the honest way to have one.

### The price tag came off

Re-measured after `bit` was demoted (the next section), because that change removed 287 of the
`apply` and `operands_of` instantiations, and those are shared by *both* schemes. The 52.6s baseline
above was therefore taken in a world that no longer existed, and the +16s that justified this scheme
being a judgement call was a comparison against it. Five interleaved runs of each, on an idle
36-core machine:

| | gcc `Z80.cpp` | peak | distinct bodies |
|---|---:|---:|---:|
| per (table, opcode) | 39.26s ± 0.12 | 1.08 GB | 1561 |
| per instruction (this) | **35.22s ± 0.07** | 1.09 GB | **853** |

**It is now 11.5% faster, and the ranges do not overlap.** The +16s became −4s, so there is nothing
left to trade: this scheme is both the better shape and the cheaper one, and the machinery that
implements it earns its keep rather than being paid for.

The mechanism is obvious in hindsight and was not obvious in advance. Demoting `bit` cut the
distinct bodies by nearly half, and *this* is the scheme that profits from there being fewer of
them: less analysis to do, and more duplication avoided by doing it. Per-opcode generation cannot
profit, because it emits one handler per slot whatever the bodies turn out to be. **The two changes
were not independent, and measuring them separately hid that.** Any conclusion of the form "X did
not pay" is only true against the rest of the system as it stood that day.

### The laptop sent us on a merry dance

Worth recording, because the wrong answer was not obviously wrong. The same experiment on a
thermally limited laptop, five interleaved runs each, gave a **25% spread on one scheme and 41% on
the other**, ranges overlapping heavily, and:

- by **median**, per-opcode was 2% *faster*
- by **minimum**, per-opcode was 13% *slower*

Two respectable summary statistics from one honest dataset, disagreeing about the sign. Either would
have been quoted with a straight face. On the idle machine the standard deviation was 0.07s and the
result was never in doubt. The caveat NOTES already carried for the bench applies to compile times
just as hard: read the spread first, and if it is wide, the numbers describe the machine.

Three separate harness failures also reached the point of producing plausible output before being
caught, and all three failed *quietly, in the direction of looking fine*: a `g++` invocation that
died on `too many filenames given` and read as 0.00s; two builds whose peak memory agreed to 0.09%,
which is what prompted checking whether the experiment was switching anything at all; and timings
taken while other work ran on the same machine. The orthogonal check that settled it was counting
`execute_one` symbols in the two objects with `nm`: 853 against 1561, so the schemes really were
different. Measure something the harness cannot fake.

The corollary matters more than the result. If duplicate *handlers* are free, the expensive thing is
the `<Fn, Call>` instantiations underneath, and those only collapse if two instructions genuinely
agree about their operands. That is what a parameterised operand would do, and it is why `bit`,
`res`, `set` and the ALU group are the interesting targets rather than `rst`.

## Done, and it did pay: a vocabulary that *is* its slice

`bit = 0 1 2 3 4 5 6 7` is eight numbers. Its members carry no operation to splice, no location to
name and no addressing mode to pay for, so `bit 0, b` and `bit 7, b` are the same function given a
different number, and the opcode already contains that number. Nothing needs to be specialised on
it; the handler can read the bits.

Detected rather than declared: a vocabulary qualifies when every live member is a plain constant
*and* member n is the number n. **Identity is the load-bearing half, and it is easy to miss.**
`rst = 0x00 0x08 ... 0x38` and `imode = 0 0 1 2 0 0 1 2` are just as numeric, but their member is a
*function* of the slice rather than the slice, so reading the bits gives `rst 3` where `rst 0x18`
was meant. The test suite said so immediately. They keep a function each, and between them are worth
21 bodies of 1034, not worth a lookup table.

The cost is that the handler now needs the opcode at run time, which the dispatch loop already has.

**And this one collapses the expensive half:**

| | before | after |
|---|---:|---:|
| `execute_one` | 1034 | **747** |
| `apply` | 895 | **608** |
| `operands_of` | 934 | **647** |
| `EvaluateAsConstantExpr` | 332,041 | **230,599** |
| gcc `Z80.cpp` | 68.9s / 1.17 GB | **52.0s / 0.94 GB** |
| clang | 129.8s | **100.9s** |

Compare that with the section above, where `apply` and `operands_of` did not move at all. **287
handlers went, and 287 `apply`s went with them**, because demoting an operand is what makes two
instructions genuinely agree about their operands. Deduplicating handlers removes shells;
deduplicating operands removes the substance.

Taken together the two changes are close to free in wall clock, generating per instruction cost
16s, demoting `bit` gave 17s back, and leave 747 handlers where there were 1280, with a third
fewer constant evaluations and the lowest peak memory yet.

**What this ranks next.** Measured by demoting each vocabulary in turn and counting bodies, from a
base of 1034:

| vocabulary | kind | bodies it would save |
|---|---|---:|
| `reg` | mixed (`b c d e h l (hl) a`) | **−492** |
| `bit` | numeric | −287 *(done)* |
| `real` | registers | −156 |
| `shift`, `arith`, `cond`, `logic` | operations | −112, −54, −42, −36 |
| `pair`, `rst`, `spair`, `imode` | | −36, −14, −12, −7 |

`reg` is the biggest prize by a distance and the one that needs a decision: member 6 is `(hl)`,
which is genuinely different code, so it is `mixed` and cannot be demoted as it stands. Splitting the
memory case into its own row (which `cb` already does for `01bbb110 | bit {bit:b}, (hl)`) would
make the rest uniform registers, and then `add a, b` through `add a, a` become one function given an
index. That needs indexed register access on the machine, which `RegisterFile` can do cheaply.
The operation vocabularies (`shift`, `arith`, `cond`, `logic`) can never be demoted: their members
are different functions to splice, not different values.

## Sketch: partition a slice, rather than demoting it

Demoting `bit` treated a slice as all-or-nothing: either every member is interchangeable at run time
or none is. That is the wrong granularity, and it is why `reg = b c d e h l (hl)/delay=1 a` looked
undemotable and led to the idea of splitting fourteen rows.

Seven of `reg`'s eight members share a *shape*: a plain 8-bit register, no indirection, no
write-back delay, no operation of their own. They differ only in **which** register. The eighth,
`(hl)`, is genuinely different code, an address, a memory access, an idle cycle. So the question
is not "can this slice be a run-time value" but **"how many shapes does this slice have?"**

### The rule

Two members are in the same class when the code generated for them differs only in which location it
names: same operand kind, same `indirect`/`displaced`, same `write_back_delay`, no bound operation,
and locations of the same kind. Generate **one body per class**, and inside a class read the ordinal
from the opcode.

It subsumes everything already built rather than sitting beside it:

| vocabulary | classes | bodies per combination |
|---|---|---|
| `bit = 0 1 ... 7` | one, value *is* the ordinal | 8 -> 1 *(what is built today)* |
| `reg` | two: the registers, and `(hl)` | 8 -> 2 |
| `real` | one of seven; a hole is not a class | 8 -> 1 |
| `shift`, `arith`, `cond`, `logic` | one **per member**, each is a different function to splice | no change, and correctly so |

The operation vocabularies needing no special case is the sign the rule is the right one. So is
`(hl)` earning its own class *because of its `/delay=1`*, the member attribute §2 introduced to
avoid splitting these rows is exactly what classifies them now.

### Where the cunning goes, and it is not in the body

The body already branches: `value_of` and `store` do `if constexpr (Op.indirect)` today, and the two
classes take the two arms unchanged. What has to change is the **key**. `body_key` currently folds
in the slice's extracted value; it must fold in the *class* that value selects instead. Once two
opcodes agree on the key, everything downstream generates one shared body with no further work.

### How a class names its location without the framework learning the CPU

The obvious move (have the machine offer `read(RegAt, n)`) does not survive views. Under
`indexed`, `reg` resolves to `b c d e ixh ixl (ix+d) a`, so the ordinal-to-register map differs per
table, and `Operand` cannot hold a `RegisterFile::R8` because `refract` must not know what a Z80 is.

Better: let the *generated code* hold the map. Inside the body, a `template for` over the class's
resolved members builds a local table of spliced enumerators, and the ordinal indexes it:

```cpp
static constexpr auto locations = std::array{[:find_location("b"):], [:find_location("c"):], …};
return cpu.read(locations[Op.slice.extract(opcode)]);
```

The array is built from the members *as that table resolves them*, so `base` gets `h`/`l` where
`indexed` gets `ixh`/`ixl`, and the two rightly do not share a body. `read` stays the existing
overload set: **no machine change at all**, where the view work needed four new accessors.

**Correction, from building it: views do *not* fall out for nothing.** A `Call` is one non-type
template parameter shared by both views, so `call_for` resolves at view 0, and an array frozen
there pins every `iy` row to `ix`. The tests said so immediately, in the plainest possible way:

```
CHECK( regs.iy() == 0x1300 )   with expansion:  4863 == 4864      # 0x12ff: an iy row read ix
```

Hoisting the view test above the shape-class test is not enough, because under `indexed` the classes
*span* the two mechanisms: `reg` resolves to `b c d e ixh ixl (ix+d) a`, so ordinals 0-3 and 7 are
untouched by any rule and want shape classing, while 4 and 5 are view-rewritten and want the runtime
view. One array, two ways of choosing.

Two ways out, and they are not equal:

- **Per-view arrays.** `location_table` becomes `[view][ordinal]`. Correct and general, but the
  operand would have to carry its `Pattern` and table so the table could be rebuilt per view,
  plumbing a good deal of the row into a type that exists to be small.
- **Do not shape-class a vocabulary the table rewrites.** One condition, no plumbing. Gives up the
  collapse in `indexed`/`indexed_cb` and keeps it in `base`, `cb` and `ed`, which is where `arith`,
  `logic`, `ld r,r'` and the `cb` families live, most of the prize.

**Called after a third correction.** The narrower fix, do not shape-class a vocabulary the table
rewrites, was tried and also failed, and the failure moved from `iy` to `ix`, which is the tell:

```
CHECK( regs.ix() == 0x1300 )   with expansion:  4863 == 4864
```

The diagnosis, which is the useful thing to keep: **`body_key` and `resolve` must agree about what a
class is, and only `resolve` was taught the exclusion.** `body_key` went on folding ordinals 4 and 5
onto the class's first member, so two opcodes shared a body while their operands resolved two
different ways, one through the runtime view, one as a plain register. One body, two answers.

That is a ten-minute fix (give `body_key` the same condition) and the spike was dropped anyway,
because three corrections in one sitting is the point at which a design is telling you something.
What it is telling us is that **the class of a member is not a property of the member alone**, it
depends on the rules of the table that decoded it, and every place that computes it has to say so.
Whoever picks this up should start by giving `body_key` and `resolve` a *shared* function that
answers "what class is ordinal n here", rather than two conditions that have to be kept in step.

Worth weighing before picking it up at all: the predicted -430 bodies was never verified, and the
last two predictions in this file were 33% too optimistic and outright wrong respectively. The
compile-time win from `bit` was real and measured; this one is arithmetic.

At run time this is one load from a constant table where there used to be a constant. The class
condition (all members' locations of the same kind) is what makes the array well-typed.

### What it is worth, and what it costs

Demoting `reg` outright measured at -492 bodies of 1034, but that pretended `(hl)` was a register.
Partitioning gets two bodies per combination rather than one, so roughly **-430**, still the largest
item on the board. `real` (-156), `pair` (-36) and `spair` (-12) come under the same rule with no
description change either.

Costs, honestly: the generator grows a partition step and a class-indexed key, which is more than
`is_numeric` was. And **the description does not change by one row**, which, after the last three
experiments, is the part worth caring about. The table is the artefact; the generator is machinery.

## Done: the line between the library and the Z80

The CPU-agnostic half is now `refract/`, in `namespace specbolt::refract`: `Model.hpp`,
`Lexical.hpp`, `Parse.hpp`, `ToArray.hpp`, `Coverage.hpp`, `Pattern.hpp`, `Parser.hpp`, `Vector.hpp`,
`TableError.hpp`, `Machine.hpp`, `Execute.hpp` and `Disassemble.hpp`. The Z80 half is `z80.cpu`,
`Operations.hpp`, `Table.hpp`, `Disassembler.cpp` and `Z80.hpp`/`Z80.cpp`.

**Both artefacts are now the library's.** `Disassemble.hpp` renders any description, so
`Disassembler.cpp` is four lines: it says where the bytes come from and nothing else. Following
prefixes, filling the DDCB latch, applying a view's renaming and walking the lowered pieces were all
facts about the description rather than about the Z80, and having them in the CPU half meant the two
artefacts followed prefixes by two separate pieces of code. The one thing genuinely left behind is
the assembler syntax `0x` and `+0x`/`-0x` are written in; if a second CPU wants `$1234` that becomes
a parameter, and not before.

What made it possible is `Description`, the five spans a consumer of a parsed table needs
(vocabularies, rows, tables, the decode tables, and where decoding starts) as one value. Passing
"the table" rather than five of its parts is what keeps the signature honest.

The two things that stood in the way both went:

1. **`Execute.hpp` no longer includes any Z80 header by name.** It includes `refract_binding.hpp`,
   which is the whole of the coupling in that direction: it points a namespace alias `target` at
   `specbolt::v4` and includes what lives there. What the framework needs is written down as the
   `Machine` concept, so a machine missing a piece is told which piece rather than finding out
   inside a generated instruction. The one thing the concept cannot state is `read`/`write` for
   locations, the shape of that overload set depends on the machine's own `location_scopes()`, so a bad location name is diagnosed at the splice in `find_location` instead.
2. **`SPECBOLT_CPU_TABLE` is a compile definition**, set by `z80/v4/CMakeLists.txt`, with a neutral
   default in `TableError.hpp`. The framework no longer names the description file.

What did *not* go: one binary still cannot hold two descriptions, because `Cpu` and the table
constants are definitions rather than parameters. A second `.cpu` file means a second binary. That
is the limit, and it is what would have to change to test a second CPU alongside the first.

## A review of the whole spike, and what was left of it

*Most of this is now struck through, which is the point of keeping it. What was still open when the
notes were split is carried forward to [NOTES.md](../NOTES.md); this is the record of the review
that found it.*

A review of the whole spike found eighteen things. The correctness ones were fixed and each has a
case in `DiagnosticsTest.cpp` where it can be tested at all; these were the rest, kept so they were
deferred rather than forgotten.

- ~~**Argument evaluation order.**~~ **Fixed, and it was not theoretical.** `bit n, (ix+d)` has two
  bus-touching operands: it reads memory and then asks for the address that read left on the bus.
  gcc evaluates right to left, so the undocumented flags came from the opcode fetch. Operands are
  materialised into a braced `std::tuple` first, whose initialisation is sequenced.
- ~~**A mistyped line vanishes.**~~ **Fixed.** After blanks and `#`, every line must be a
  declaration or a row; `check_every_line_means_something` says so. Still true and unfixed:
  `next_word` splits on spaces only, so a tab-indented `field` is not recognised at all, `trim`
  handles tabs, which shows they were meant to be whitespace.
- ~~**`SPECBOLT_CPU_TABLE` lives in `TableError.hpp`**, so a framework header names the CPU
  description, and `Execute.hpp` includes the Z80's headers by name for the same reason.~~ **Both
  fixed**. See "the line between the library and the Z80" above. What is left is that one binary
  still cannot hold two descriptions.
- ~~**`Operand` carries jobs that already have types.**~~ **Done.** `Reference` is a type, and
  `write_back_delay` now lives only on `Operand`, `parse_member` writes it there directly, so
  `resolve` is a one-liner and there is nothing to keep in step.
- **The write-back-delay rule compares only the name**, not that both ends are indirect, and two
  nameless indirect operands compare equal. Nothing exercises it today; the rule meant is "the
  destination is the same addressing mode as one of the operands".
- ~~**`Matched::matches` is test-only and misleading**~~ **Fixed by deleting it**, along with
  `fixed_mask` and `variable_mask`, which existed only to serve it. It was the obvious way to decode
  (AND with a mask, compare) sitting in the first file a reader opens, in a design that
  deliberately does the opposite: `opcodes_of` *generates* a row's opcodes and decoding is a
  precomputed table. A decoy in production code, kept alive by nothing but its own test.
- **Naming.** "field" means the `.cpu` keyword, the C++ `Field`, a `BitSlice` (in one error message),
  and `Piece::Kind::Field`. One word per concept. `Matched` is a participle for "a parsed opcode
  pattern". `Member::display` is not only for display.
- **The v4 `.cppm` files cannot compile.** v4 is excluded whenever modules are on, so every
  `SPECBOLT_MODULES` branch in v4 is unbuildable by construction, and the partitions do not include
  the headers they would need. They look maintained and are not.
- ~~**`DisassemblerTest` understates coverage.**~~ **Stale, no commented-out `CHECK`s remain**; the
  only markers left say "tested elsewhere". What is genuinely missing is a test that the *two
  artefacts agree*: for every (table, opcode), that `disassemble(...).length` equals how far the
  interpreter moved PC, and that nothing renders `??`. The format's headline claim is "one
  description, two artefacts", and length is checked on one side and PC on the other and never
  against each other.

## A second review, and what came of it

Four passes over the finished spike, each with one lens: modern C++ we were not using, whether
reflection was earning its keep, simplicity and teachability, and API design and latent bugs. The
library claims below were checked against the gcc 16.2 this builds against rather than assumed.

**The worst finding was a hole in the checking, not a bug in the code.** `Model.hpp` said a view
vocabulary's members "are required to share a shape", and nothing required it. Only member *count*
was checked. Three separate mechanisms resolve a view reference at member 0 and apply the answer to
every view, and one of them, `displaced_through`, does not take a view at all; so
`check_displacement_rendered`, whose whole job is catching a mnemonic that disagrees with what runs,
never looked at view 1. `vocab index_mem = (ix+d)/delay=1 (iy)` compiled clean and gave the `FD`
page the `IX` page's addressing mode, timing and text. Every other class of `.cpu` mistake in this
project produces a line number; this one produced a wrong emulator. `check_view_vocabulary` now
rejects it where the reference is written, and `displaced_through`'s missing parameter is justified
in one line instead of being an omission to reverse-engineer.

Alongside it: **all ten view diagnostics were untested**. The newest feature had the thinnest
coverage, which is the wrong way round. They have cases now, and so does the new check.

**Two disassembler defects**, one live and one latent:

- No bound on the prefix loop. `dd` reaches its own table, `offset` is a `std::size_t`, and
  `byte_at` wraps at 16 bits, so a region of `0xdd` was walked to its end before one line was
  rendered, quadratic for a listing, unbounded for a 64K image of it. Note the asymmetry with the
  interpreter, which is *right* to loop forever there: the chip really does spend 4T a byte on
  `dd dd dd …`. A disassembler is asked what is at an address and has to answer.
- `opcode` was read back as `byte_at(offset - 1)` after the loop, which is the *displacement* for
  any row that reads one and does not `goto`. Latent only because `z80.cpu`'s single `d` row is a
  goto; nothing rejects one that is not. The interpreter takes its opcode as a template argument, so
  the failure mode would have been the two artefacts silently disagreeing.

**Two silent `else` branches** in code whose thesis is that mistakes are compile errors: access
width was `std::same_as<std::uint16_t>` with everything else falling through to one byte, so an
operation declaring `unsigned` would have read half of what it asked for and zero-extended the rest.
Two `static_assert`s.

**`check_tables_used` proved the wrong thing**, "some goto names this table", not "reachable from
the entry table". Two tables that only reach each other passed, and were then forced to be total and
generated in full: 512 handlers of dead code from a typo. It walks the decoded tables from `entry`
now, which also accounts for inheritance.

Simplifications taken, all verified by building:

- `OpcodeSet` was 28 lines of hand-rolled `std::array<std::uint64_t, 4>`. C++23 made `std::bitset`
  constexpr, so it is now `std::bitset<256>` plus two free functions, and the four bit-fiddling
  loops are gone.
- `Parser` maintained a line counter that **only the test read**: every diagnostic threads an
  explicit line, and the number handed to the constructor was never read back. `lines_of` is a
  `split | enumerate | transform` pipeline, the counter and its per-token scan are gone, and four
  copies of the same loop became four range-`for`s.
- `location_scopes()` was the one list in the project that mirrored a set of declarations with
  nothing holding them together, and `Z80.hpp` says the enums exist *for* it. It is now every enum
  the namespace declares. A new location enum is a location; forgetting to list it is not a thing
  that can happen. `Bus` is swept up too, harmlessly, and if a name ever did collide `only_match`
  says so, with both scopes named, which it did not before.
- `Pattern::matches` and its two mask helpers deleted; `decoded_count` deleted (it restated what
  `check_tables_total` already refuses to build without); `Vector::back()` deleted (no callers, and
  it would have underflowed on an empty one).
- Three `static_assert(std::meta::is_structural_type(…))` where fifteen lines of comment used to
  explain the requirement and nothing enforced it.

**Deliberately not done.** `std::expected` for parse failures, it would destroy the throwing-
`consteval` diagnostic, which is the best idea in the file. `std::generator` for the lowering
passes, coroutines cannot be constant-evaluated at all, which makes it a better slide than a
change. `std::optional<T&>` for `row_for` (not in libstdc++ 16.2, checked. `views::concat` to
unify the "operands then destinations" loops) absent from libc++ 21, and v4 is libstdc++-only
today, but not worth the portability question for five loops. Splitting `Operand` into parsed and
resolved halves, and bundling the `(vocabularies, matched, opcode, rules, view)` tuple that nine
functions thread: both real, both structural, neither obvious enough to do without deciding what
the talk needs first.

**Unresolved and worth knowing.** `a substitution's reference must be selected by the table's view`
appears unreachable: `parse_substitutions` calls `reference_from_braces` with an empty `Pattern`, so
any reference that is *not* the view fails earlier on "names a slice the opcode pattern does not
define". The throw stays as a guard, but no test can reach it.

## The original plan, in the order it was meant to happen

*A snapshot of the intended order of work. Items 1 to 6 are done, including interrupts, which this
list still describes as "absent entirely"; 4's hard half, DD/FD as views and DDCB, is done too.
What is still open is in [NOTES.md](../NOTES.md).*

1. ~~**Multi-slot rows.**~~ Done.
2. ~~**Compile-time overlap and coverage checking.**~~ Done. See §5.
3. **Encoding as a byte sequence.** Immediates and length derived: done. Multiple pattern tokens
   per row, which is what expresses the DDCB fetch order: folded into 4, because it is the same work.
4. **Prefixes.** Table switch and override rows: done, CB works; the goto cycle a view needs is now
   legal, decoding being a loop. Still to do, and the hard half: DD/FD as *views* and DDCB. See the
   Prefixes section.
5. ~~**Timing**~~ Done, though not as this list expected: cost belongs to the addressing mode, with
   an explicit `delay` step only for idle cycles belonging to the operation. `t=` assertions still
   want static per-step costs, which do not exist: cost is resolved inside `Z80::bus`.
6. **Interrupts.** Absent entirely. jsbeeb samples the interrupt at a named position in the cycle
   schedule rather than at the top of the instruction, which is the difference between exact and
   approximate, worth deciding before writing the easy version.
7. **WZ**, flags cross-checks, `undoc` marking. Nothing will catch a regression here until there is
   a zexall run; zexdoc tests documented flags only.
8. **Conditional cycle schedules.** `djnz` 8/13 has no expression today. jsbeeb's `split(condition)`
   forks the remaining schedule, which states both rather than asserting a range.
9. **Project `Row` into artefact tables** rather than scanning at runtime.

Parallel, not blocking: make `RegisterFile` header-only and `constexpr` so execution tests can be
`STATIC_CHECK`. `static_assert(run(0x21, 0x4000).get(R16::HL) == 0x4000)` is the slide the talk
wants, and it is currently impossible only because the accessors are defined in a `.cpp`.

Also parallel: ~~**write a small compile-time vector**~~, done, `Vector.hpp`, and it is used by
`Field::values`, `Matched::slices`, `Row::pieces` and `Row::steps`. It carries its own count, throws
its caller's message on overflow, and is structural so it can be a template argument.

Worth considering a structural fixed-capacity **string** at the same time. `string_view` being
non-structural has now blocked three separate things, `Row` as an NTTP, `Piece` in an
`inplace_vector`, and a vocabulary member as a template argument, and each time the workaround is to
pass an *index* and look the object up inside. That works, but a structural string type would remove
the class of problem rather than the instances.

The obvious answer for the vector is `std::inplace_vector`, and it is the wrong one *for now*: gcc 16.2 ships
`<inplace_vector>` but its constexpr path supports **trivial types only**, `__builtin_unreachable(); // only trivial types are supported at compile time`. `Piece` holds a
`std::string_view`, which is trivially copyable but not trivially default constructible, so it does
not qualify. Verified both ways. This is a libstdc++ limitation rather than a language one, so
revisit and delete our version when it lifts.

### Does the table need to survive constant evaluation at all?

Currently yes, and that is worth being deliberate about. `rows` is read **at runtime**, the
disassembler does `rows[*index]` per instruction, so the table is static data, which is exactly why
it cannot hold a `std::vector` and needs the fixed-capacity idiom above.

That is only true because disassembly is *interpreted* while execution is *generated*. Generate
disassembly per-opcode too, the way `execute_one` already works, and nothing needs to survive: `Row`
could use `std::vector` freely inside the parse and the fixed-capacity types would disappear
entirely. Worth deciding on purpose rather than by default, because it also decides whether a
debugger view can ask questions of the table at runtime.

---

## Done: annotations (P3394), and the three things they bought

An annotation is a value attached to a declaration and read back by reflection. gcc 16.2 has them,
which was not obvious: they are a separate paper from P2996 and the library half is declared whether
or not the syntax is. Three things cost time before anything worked.

- **On an enumerator the annotation goes after the name.** `Up [[=Spelling{"i"}]]`, not
  `[[=Spelling{"i"}]] Up`. The second does not parse, and the error is about an enum body rather
  than about an annotation.
- **`type_of` on an annotation is const-qualified.** Comparing against `^^Spelling` silently matches
  nothing; it has to be `^^const Spelling`. This is the one that looks like "annotations do not
  work", because `annotations_of` returns the right count and the filter drops them all.
- **The annotation's type must be structural.** `struct Spelling { std::string_view text; }` is
  rejected outright. `Name` was already in the codebase for exactly this constraint, since a
  template argument has the same requirement. The same fixed `std::array<char, 15>` serves both.

### What the table calls a thing, said by the thing

`Spelling` is how a `.cpu` file names an enumerator when that differs from what C++ calls it:

```cpp
enum class BlockDirection : std::uint8_t {
  Up[[= refract::Spelling{"i"}]] = 0,
  Down[[= refract::Spelling{"d"}]] = 1,
};
```

The point is not the annotation. It is that the *parameter type is the scope*: `find_spelling` looks
a bare name up in the enum the parameter asks for, which is the same rule that already lets a
parameter's type decide how wide an access is. That is what keeps these names out of the location
namespace, and it matters here because `i` and `d` are also the I and D registers and mean neither
of them in this row.

An operand that is a number where the parameter is an enum is now an error. A cast would get past
every check the enum was introduced to impose, so the facility cannot be quietly bypassed.

### The annotation is an override, and mostly is not needed

`find_spelling` first matched only annotated enumerators, which made an enum unusable unless it was
annotated in full. That is not what an override is. It now asks each enumerator for its spelling and
gets the annotation if there is one and the identifier if there is not.

The payoff was immediate: `Alu::Direction { Left, Right }` spells itself, so the description can name
it with no edit to a header shared with v1 to v3, and six pure-forwarding wrappers went. `rl8` and
`rr8` keep theirs, because their carry sits in the middle of the argument list where an appended
operand cannot reach.

It also sharpens the claim. Annotate where the assembly spelling and the C++ identifier genuinely
differ. `Up` is written `i` because the Z80's assembly says so, and that is worth stating. `Left`
being written `left` is not.

### Sixteen block rows became eight

Bit 3 of a block operation's opcode is which way it walks memory, so the sixteen rows were eight
pairs differing in one letter of the mnemonic and one digit of the action. The digit was a
`bool increment` whose sense was the *opposite* of the bit it came from: the `i` forms encode 0.

The values are written out rather than left implicit, because they are the hardware's and nothing
should depend on the order enumerators happen to be declared in.

The four families keep a row each rather than collapsing further. Their step lists genuinely differ,
and the repeating forms spell `out` as `ot`.

## Done: a view is an index into the description, not a pact with the machine

A view was a number the machine had to interpret. `Z80::pair_for` read 0 as IX and 1 as IY, which
was correct only while that matched the order `vocab index` listed its members in. Nothing stated
the agreement and nothing could check it, so reordering the vocabulary would have rendered one
register and executed the other: a wrong emulator rather than a diagnostic, which is the same
category as the `check_view_vocabulary` finding.

It was fixed twice, on purpose. First a `static_assert` that the four parallel index vocabularies
list ix before iy, which turns silence into a compile error. Then the coupling itself:
`locations_of_view` walks the vocabulary a view selects from and splices each member's location into
an array, so the view indexes that array and the machine is handed a location it already knows how
to read. `check_view_vocabulary` is what makes this sound, since it already guarantees the members
share a shape and so resolve to one type.

The machine lost `Index`, `IndexHalf`, `pair_for`, `half_for` and four `read`/`write` overloads, the
format lost a requirement from its contract table, and the check from the first fix was deleted
because there was nothing left to check.

Verified by reversing all four index vocabularies: every test still passes, which is exactly the
property that did not hold before. One coupling survives and is now visible in one file, since a
view is an ordinal into all four at once and they must agree with each other. Nothing can check
that, because nothing but the Z80 knows `ixh` is half of `ix`. Reversing one alone fails three
suites.

## Done: keyword operands, because position was a silent coupling

`bit8(value, bit, flags, bus)` takes three `std::uint8_t` parameters and a row filled them by
position. Swapping two compiles, runs, and quietly tests the wrong bit. Types cannot catch it and a
reader has to count.

An operand may now be written `value=(hl)`, and the names are the ones in the CPU's own declaration,
read off it with `identifier_of` on `parameters_of`. Nothing restates them, so renaming a parameter
in C++ renames it here, and a row still using the old name fails to build with the line that wrote
it. `has_identifier` guards the case of a declaration without parameter names.

The design decision that mattered was keeping *when an operand is read* separate from *which
argument it becomes*. Resolving one can read memory and move the address bus, so the tuple is still
built in the order the row writes its operands; `call_with` is where that order and the signature's
order are reconciled. Naming never reorders effects.

Two rules, both diagnosed. All or nothing within a step, because a half-named argument list needs a
rule about what "the next one" means and a description is easier to read if there is no such rule to
remember. A destination may not be named, because it is where the result goes rather than something
handed to the operation.

Worth recording how it was verified, because the first attempt proved nothing. Writing the operands
in reverse *with* names passes. Writing them in the same order *without* names fails to compile,
which looks like a result and is not: `Flags` will not convert to `unsigned char`, so the type
system caught that particular scramble and the ordering was never exercised. The test with teeth
transposes `value` and `bus`, both `std::uint8_t`: it builds clean and fails two suites.

## Done: a line may end in a backslash

`vocab shift` reached 196 characters once the rotate and shift rows started naming `Alu` operations
directly, and the `table indexed … with` rule list 169. Both are lists, and a list that cannot be
wrapped is a list that gets read by scrolling.

Nothing is joined. The description is one buffer, so a continued line is still a single
`string_view` into it, just a longer one that happens to contain the `\` and the newline it was
wrapped at. Those two became blanks to `Parser` alongside space and tab, which is the whole of the
change: all four passes over the text needed nothing, because they read words and a word never
contained either character.

A continued line is reported at the number it started on. Per-token numbering is available if it is
ever wanted, since a token is a view into the description and its line is the count of newlines
before `token.data() - description.data()`, but nothing has needed it.

The TextMate grammar ends a declaration at a newline the line did not escape. A lookbehind on `$`
looks equivalent, and silently is not.

## Done: an expansion statement constrains the calling convention

Threading the interpreter (each handler ending in a `[[gnu::musttail]]` call to
the next rather than returning to a loop) is worth 15-17% on real games. The
measurements are in [Notes.md](../../Notes.md), because they are facts about the
emulator. What belongs here is what it taught us about C++26, because one of the
three things is genuinely surprising and is not written down anywhere else.

**`[[gnu::musttail]]` works on gcc 16.2 through a function pointer**, which is
what a table-driven interpreter needs and was not obvious. Verified in the
disassembly before building anything on it: the call site is a bare `jmp *%r8`,
with no `call` and no `ret`.

All the constraints are one constraint, and gcc states it well through
`-Werror=maybe-musttail-local-addr`: **a tail call abandons the frame, so
nothing the compiler believes lives in that frame may still be addressable.**
Three consequences, in increasing order of interest.

- A lambda capturing `[&]` takes the address of every local it touches, which
  blocks the tail call from anywhere after it. Explicit captures fix it.
- A `constexpr` local is still an automatic object. `static constexpr` is not an
  optimisation here any more than it was for `row`; it is what moves the object
  out of the frame.
- **A tail call cannot be made from inside a `template for` body at all**, because
  the expansion's own induction variable lives in the frame the call would
  abandon. There is no workaround, and it shapes the code: a row that abandons
  its remaining steps `break`s out of the expansion and hands on afterwards
  rather than handing on where it stopped.

The third is the one to remember. An expansion statement looks like a purely
compile-time construct, a way of writing several statements rather than one, and
it reads as though it should leave no trace at run time. It does leave one: while
the expansion is in scope there is an object in the frame, and that is enough to
forbid a tail call. Nothing about `template for` suggests it should have an
opinion about calling convention, and it has one.

Worth noting what got *smaller*, since the usual expectation of a performance
change is the reverse. `Transfer`, the `std::optional` it was returned in, and
the entire dispatch loop are gone: the loop is now the chain of tail calls
itself, and `execute_instruction` is one line.

## Done: a location is a thing the machine can read

Which names a description may write was decided, for most of this project, by a
list. `location_scopes()` named the enums a name could come from, and the
framework searched all of them.

The list became a scan: every enum in the CPU's namespace, so that declaring one
was enough and forgetting to list it could not happen. That was better and still
wrong, in a way that took a while to see. The scan swept up `Bus`, whose
enumerators are `opcode`, `operand`, `read`, `write`, and a description could
name any of them. Nothing collided, so nothing failed, but `ld8 a <- read`
resolved cleanly to a bus cycle kind and then died inside a splice with no line
number at all.

Two fixes were tried. An annotation marking the enums that are *not* locations,
which tags a thing by what it is not. Then a `locations` namespace, so the scan
walked a scope declared for the purpose, which works and cost a namespace and a
using-directive.

The answer was already in the machine. **A location is a thing you can read**,
so the pool is the `read` overloads:

```cpp
for (const auto member: std::meta::members_of(^^Cpu, std::meta::access_context::current())) {
  if (std::meta::identifier_of(member) != read_verb) continue;
  const auto parameters = std::meta::parameters_of(member);
  if (parameters.size() != 1) continue;
  if (const auto type = std::meta::type_of(parameters[0]); std::meta::is_enum_type(type))
    scopes.push_back(type);
}
```

`Bus` is excluded because nothing reads a bus cycle kind. Not by a list, a
namespace or an annotation, but because it is not one. `read_memory` is excluded
by name and an overload taking more than the location by arity.

The same trick answers the other pool. A vocabulary may name the scope its
members come from, and that scope need not be a location: `BlockDirection` is a
*value*. A value scope is an enum some operation takes as a parameter, derived
from `operation_scopes()` exactly as the locations are derived from `read`.

So `Locations.hpp` is gone, the `locations` namespace with it, and the contract
lost a function: a target says where its *operations* live and nothing about its
locations, because it has already said what it can read.

**What this is really an instance of.** Three attempts encoded "these are
locations" as a convention: a list, then a namespace, then an annotation. The
machine had been declaring it all along in the only way that cannot drift, by
being able to do it. Ask what a thing *can do*, not what it says about itself.

Ambiguity is now checked over the whole pool rather than per lookup.
`only_match` catches two locations spelled the same, but only for a name some
description happens to write; a `static_assert` over the derived scopes makes it
a property of the machine. The Z80's 51 names are unique, which is the sort of
thing worth knowing rather than assuming.

## Done: a diagnostic names the line you have to edit

`check_view_vocabulary` fires when a row binds a vocabulary to a table's view,
which is the only moment the requirement exists. So the only line in scope was
the row's, and that is the line it reported:

```
z80.cpu:6: vocabulary 'm' is selected by a view, so all of its members must
have the same shape; '(iy)' does not match '(ix+d)'
```

Line 6 is `00000000 | ld {m:view} | ld8 {m:view} <- a`, a row with nothing wrong
with it. The mistake is on line 1, and line 1 is where the fix goes, every time.
Worse, if two tables select the same vocabulary by a view, the row you are told
about is whichever the parser reached first.

Both lines matter, for the reason C++ prints an error in a template and a note
saying who instantiated it. The declaration is where the edit goes; the use is
why a declaration that would otherwise be legal is not. `Vocabulary` gained the
line it was declared on, so the message can carry both:

```
z80.cpu:1: vocabulary 'm' is selected by a view (at z80.cpu:6), so all of its
members must have the same shape; '(iy)' does not match '(ix+d)'
```

The rule this settles, for the rest of the format's diagnostics: **report
against the line that has to change**, and name the line that made it a
requirement in the text. A check that fires at a use site has to be asked which
of the two it is really about, and it is usually not the one it happens to be
standing on.

## Done: an operand before an opcode, and an operand after one

One `Operand` served three roles: what a row wrote, what a vocabulary member
holds, and what `resolve` produced. Thirteen fields, of which `reference` meant
something only before resolution and `scope`, `from_opcode`, `slice` and
`from_view` only after. Nothing said so, so nothing checked it.

Three places had gone wrong on exactly that seam, and two were live:

- `locations_of_view` read `scope` off a *member's* operand, where only
  `resolve` ever sets it. It was always empty, so every view-selected location
  was looked up unqualified, and the four index vocabularies all declare a
  scope. It gave right answers only because `location_names_are_unique` asserts
  the whole pool is unambiguous, which is the guarantee the scope clause exists
  so a description need not lean on.
- `call_for` pushed a member's appended arguments straight into the call's
  operand list without resolving them, so a `Call` held a mixture of the two.
- `resolve` wrote its answer back over `reference.vocabulary_index`, a field
  whose other meaning was a slice plus a vocabulary.

`Operand` is now what a description wrote and `Resolved` is what an opcode makes
of it. The generated code is templated on `Resolved`, `resolve` is the only way
to obtain one, and the three sites above are no longer expressible: the first
two are type errors and the third is a field called `view_vocabulary`.

**The enum is the part worth stealing.** `Resolved::Kind` has four enumerators
where `Operand::Kind` has five: resolving a vocabulary reference *is* the
lookup, so `Vocabulary` cannot survive it. Before, a vocabulary operand reaching
`direct_value_of` would have fallen out of the bottom of the `if constexpr`
chain and spliced a lookup of an empty name. Now the case does not exist to be
reached, and the compiler knows the chain is exhaustive.

The types did not get smaller: nine fields and thirteen, against thirteen, since
the shape fields are genuinely common to both. What was bought is that no field
on either is meaningless, and that `Resolved resolve(const Resolution &,
Operand)` states the direction of travel in its signature.

**Build cost: unchanged, 747 instantiations before and after** (`nm | sort -u`
on `Z80.cpp.o`). Shrinking an NTTP can only merge instantiations, never split
them, so this was the expected direction, but the hoped-for merge did not
happen: `parameter` has to stay on the resolved side, because
`operand_for_parameter` reads it off `C.operands` to work out the argument
order, and it is the only field two otherwise-identical operands are likely to
differ in.

## Answered: member-function splicing works, and the idea it was gating is v2

A recurring thought has been to delete the text format and let the C++ class *be*
the description: tag member functions with their encodings, reflect over the
class, and drop the parser, the `#embed` and every hand-written diagnostic.

**That design already exists in this repository, and it is v2.** One constrained
variable-template specialisation per opcode, the mnemonic as a template argument
and the behaviour as a lambda:

```cpp
template<Opcode opcode>
  requires(opcode.x == 0 && opcode.y == 1 && opcode.z == 0)
constexpr auto instruction<opcode> =
    Op<"ex af, af'", [](Z80 &z80) { z80.regs().ex(RegisterFile::R16::AF, RegisterFile::R16::AF_); }>{};
```

with `MissingInstruction`'s deleted constructor as its "you have not written this
one yet" diagnostic. So the question is not whether C++ can be canonical. It can,
it was, and v4 exists because we wanted to find out what the other answer looks
like: a description that is *not* C++, and a generator that knows no Z80.

Annotations would make a **better v2**, and that is worth saying plainly, because
it is the strongest form of the idea. v2 pays for its expressiveness twice: the
encoding lives in a `requires` clause, where a pattern like `01yyyzzz` becomes
three comparisons, and every instruction needs a lambda even when it is doing
what fifty others do. A tag can carry structured data instead, so the encoding
stays one string and the shared behaviour stays one function. More flexible than
constraints, and the same axis: C++ as the canonical form.

Everything that flexibility needs from the language is there. gcc 16.2 takes a
non-static member function found by `members_of`, carries it as a template
parameter, and splices it in member-access position:

```cpp
template<std::meta::info Fn, typename... Args>
auto call_on(Cpu &cpu, Args... args) { return cpu.[:Fn:](args...); }
```

`parameters_of` reports 1 for `ld8(std::uint8_t)` and 0 for `twice()`, so **the
implicit object parameter is not counted** and none of the arity arithmetic in
`Execute.hpp` would shift. `is_static_member` separates the two kinds, so both
could be supported at once.

One limit shapes what such a design could look like: **reflection sees
declarations, not bodies.** A member function cannot describe an instruction by
being written as one, so the behaviour has to be named by the tag rather than
read out of the function, which is exactly what v2's lambda-as-template-argument
already does.

What the idea was originally chasing was the forwarding: a name in the table
reaches `find_operation`, which finds a static function, which sometimes turns
straight round and calls a member of `Z80`. Most of that is gone by other means,
since what the framework itself needs is now the `Machine` concept and called
directly on the chip. What is left is a handful of pure forwarders in
`Operations.hpp` (`delay`, `ex_sp_ix`, `ex_sp_iy`), which is a small price for
the property below.

The narrower use, making *operations* member functions rather than static
functions taking `Cpu &`, is declined for a design reason rather than a language
one. An operation that needs the machine currently says so in its signature,
where `takes_cpu` finds it and hands the machine over as argument zero. A member
function reaches the whole machine implicitly, so a row's `<-` would quietly
stop being the whole truth about what an instruction touches.
