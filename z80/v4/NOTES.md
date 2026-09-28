# v4: notes

Working notes for the table-driven, compile-time v4: what it does today, what is still open, and
where the rest of the record lives. For the `.cpu` format itself, its grammar, semantics and worked
examples, see [CPU_FORMAT.md](CPU_FORMAT.md). These notes are about *why* it is that shape.

Known bugs in v1/v2/v3 found while researching this are filed as issues rather than recorded here.

## The rest of the notes

This file is the current state. Everything else lives next door, and each file has a rule about
what belongs in it, so that this one stops growing:

| | holds | what goes in it |
|---|---|---|
| [notes/FINDINGS.md](notes/FINDINGS.md) | what C++26 actually did, on a real compiler | any new language fact |
| [notes/MEASUREMENTS.md](notes/MEASUREMENTS.md) | speed, build cost, what accuracy buys | any number, with its method |
| [notes/PREFIXES.md](notes/PREFIXES.md) | how prefixes, views and latched tables work | that one argument, kept whole |
| [notes/WASM.md](notes/WASM.md) | getting v4 into the browser, and what that took | anything about the wasm build |
| [notes/JOURNAL.md](notes/JOURNAL.md) | what was decided and why, in order | **new "Done:" entries** |

The journal is a record, not a reference: every entry was true when it was written and describes
the code at that moment. When it disagrees with this file, this file wins; when this file
disagrees with the code, the code wins.

---

## Where the spike is

`z80.cpu` is `#embed`ed, parsed at compile time, and drives two artefacts. 127 rows in seven decoding
tables decode **every one of 1792 entries**: `base`, `cb`, the `ix`/`iy` views, `ddcb`/`fdcb` and `ed`
are all complete, and the instruction set is finished.

- **Disassembly.** Walks the row's lowered pieces, following a `goto` through a prefix table.
- **Execution.** A 256-entry dispatch table per decoding table, built with a `template for`
  expansion statement, one `execute_one<Table, Opcode, Index>` instantiation per entry, each
  resolving its verb by reflection over the target's palettes and the machine's marked members.

The pipeline is `#embed` → `consteval` parse → lower to validated pieces → `template for` → splice.

A row has three columns: an encoding token sequence, a mnemonic, and an ordered list of steps. Each
column is checked against the others, length and what is fetched come from the encoding, never from
the display text.

Mnemonics are **lowered at parse time** into a fixed `Piece` array (a literal chunk, a vocabulary
reference with its vocabulary and slice already resolved, an immediate slot), so the disassembler
parses nothing at run time.

Malformed tables are compile errors carrying the source line, e.g.

```
z80.cpu:9: reference names a vocabulary that does not exist
z80.cpu:9: vocabulary has the wrong number of members for its opcode bits
z80.cpu:9: this CPU has nothing named 'ld17'
z80.cpu:25: this row overlaps a later one without being contained by it
```

### The argument this exists to make

Everything inside `consteval` is memory-safe by construction, constant evaluation refuses to index
out of bounds or to read a dangling pointer, so a parser bug is a compile error rather than a corrupt
table. Every correctness hole found in the first cut of the spike was in the half left at runtime.
Lowering the table to a validated fixed shape at parse time removes that half entirely.

---

## Where the framework/CPU boundary sits

`Target.hpp`, `Operations.hpp` and `Z80.hpp` are the whole customisation surface. Retargeting
means writing these and nothing else:

- `Target`: which machine, which description, and which palettes
- `Z80`, the machine state, marking with `[[=refract::operation]]` the verbs that touch it
- `Operations`, the verbs that touch nothing
- `read`/`write` overloads: how to touch storage, and, for `read`, what storage there is
- `read_memory`/`write_memory`: how to touch memory through an address
- `fetch_opcode`/`fetch_immediate`/`fetch_immediate16`: how to read the instruction stream
- `delay`: how to spend an idle cycle

A verb that needs the machine is a member of it, called on it; a verb that does not is a static
function of a palette, called on nothing. The machine's public interface is larger than its
vocabulary, so it publishes its verbs one by one; a palette is built to be named, so everything in
it counts.

The framework names no CPU type at all: not `RegisterFile`, not `Alu`, not `Flags`. It knows only
that a row has a verb, some operands and some destinations, and that the CPU can resolve a name.

### The collapse that got us here

Three Z80-isms used to live in the framework, and all three were the same mistake, inferring
meaning from a C++ type rather than reading it off the row:

- `Operand::Kind::Accumulator` presumed a CPU has one.
- `CarrySource` filled a `bool` parameter from the carry flag. Already wrong for
  `Alu::iff2_flags_for(u8, Flags, bool iff2)`, whose `bool` is not carry.
- `is_supplied_by_framework` did the same for `Flags` and the machine.

All three became one operand concept, constant, immediate, name, field reference, or discard, where `a`, `carry` and
`flags` are just names the CPU resolves. Vocabulary members may append an
operand (`add:add8+0`, `adc:add8+carry`), so the carry policy is data in the table. Destinations
are a list, so `{q} a, flags <- a {r:z}` destructures whatever the primitive returns, and the last
assumption (that a result type has a member called `flags`) went with it.

### What the framework relies on instead

Three properties of the primitive, all read by reflection, none of them Z80-specific:

- its arity, checked against the number of operands the row supplies
- its parameter types, which each operand converts to, so a 16-bit location handed to an 8-bit
  parameter is a `-Wconversion` error, not a truncation
- its return type: `void` means the row may name no destination, a scalar means exactly one, and a
  class means one destination per non-static data member, in declaration order

A `-` destination discards a component, which is how `cp` uses `cmp8` without writing the result
back to `a`.

---

## What a second CPU would need

The format has described exactly one processor, and an outside reader given only
[CPU_FORMAT.md](CPU_FORMAT.md) (told to know 6502 and Z80 but not to look at the code) went
looking for the seams and found them. Recorded here because "not Z80-specific" is currently a design
intent that has been half-tested, and it should either become true or stop being claimed.

Each of these is a concrete 6502 instruction that cannot be written today.

### 1. An addressing mode must be able to fetch its own operand

The 6502's `aaabbbcc` puts the addressing mode in `bbb`, which is exactly what a vocabulary is
for, but its members are different *lengths*: `#` and `zp` fetch one byte, `abs` two, accumulator
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
(`B1`) (read a pointer from a fetched zero-page address, then index it) has nowhere to go. The
escape is a CPU-supplied `lda_indirect_y` primitive, at which point the table has stopped naming
general operations and the whole argument collapses for that CPU.

### 3. Indexing is hard-wired to a displacement byte

`(ix+d)` means "base plus one signed byte read from the instruction". `LDA $1234,X` (`BD`) is
"16-bit immediate base plus the contents of a register", and there is no syntax for it. The two are
the same idea (a base and an offset) with the offset coming from different places.

### 4. Cost that depends on the data

The 6502 charges one extra cycle on `abs,X`, `abs,Y` and `(zp),Y` **when the index crosses a page
boundary, and only on reads**: `LDA $1234,X` is 4 or 5 cycles, `STA $1234,X` is always 5.

`displaced_address` takes the machine by reference, so it *can* charge conditionally, but it is not
told whether the access it is forming will be a read or a write, so it cannot tell those two apart.
That is a small signature change. The harder half is that we form the address **once per
instruction** (§ *indexed addressing*), which is right for the Z80 and wrong for a machine where the
cost belongs to each access.

### 5. Bus timing is ordered at step granularity

Not a 6502 issue, but the same review raised it and it belongs here. Cost is a total per instruction,
ordered by step; a multi-byte access is indivisible and nothing below a step can be scheduled. Good
enough for totals and for contention at instruction granularity, not enough for a *schedule* of bus
cycles at known offsets. The Spectrum's contention will decide whether this matters, see
*Time passes in exactly one place*.

### What is already fine

Worth saying, so the list above is not read as worse than it is: eight-bit opcodes, no prefixes
needed, conditions, relative jumps, the stack, and page-zero addressing all work today, and the
6502's flag model is no harder than the Z80's. The gaps are addressing modes and their cost, not the
shape of the thing.

### What the 6502 spike found, and the design it points to

Tried 2026-09-27 and 2026-09-28 on the branch `mg/v4_spike_6502`, which describes the documented NMOS instruction
set in `z80/v4/m6502/6502.cpu`, runs it beside the Z80, and keeps the detail in its own `notes/6502.md`. Exploratory:
smoke tests only. Against the predictions above:

- **§1 was the one that bit.** With refract unchanged, the description was 46 rows plus a catch-all for 151 opcodes,
  because each group needed a row per addressing mode.
- **§2 and §3 did not block.** Addresses are formed in steps through `ea`, a location standing for the chip's internal
  address latch: `(zp),Y` is `zp_pointer ea <- n ; index ea <- ea y`, then `(ea)`. Nesting and register indexing are
  just more steps, and `displaced_address` is not used at all.
- **§4 moved into the machine.** `index` records that a fix-up is owed; a read pays the extra cycle only if the index
  crossed a page, and a write always pays, which is what the chip's dummy read does. One addressing mode then serves
  loads, stores and read-modify-writes. No framework change.
- **§5** was not tested.

**The design: a mode is a block of row fragments.** A `mode` declaration opens a block, as `table` does, and each
line after it is one member, written like a row with four columns: the slice's value with the bytes the member adds
to the encoding, the text it renders, the operand it stands for, and the steps it runs first.

```
mode am
000 n   | ($nn,x) | (ea) | zp_index ea <- n x ; zp_pointer ea <- ea
001 n   | $nn     | (n)  |
010 n   | #$nn    | n    |
011 n n | $nnnn   | (n)  |
...
mode store = am with 010 -> -

aaabbb01 | {alu:a} {am:b} | {alu:a} a, p <- a {am:b} p
100bbb01 | sta {store:b}  | ld8 {store:b} <- a
```

A row names a mode with the ordinary reference syntax: in the mnemonic it renders the member's text, in a step it
stands for the member's operand, and the member's steps run once, before the row's own, however often the row names
it. The value is written out rather than implied by line order, so reordering cannot change the meaning and a missing
value is a hole; the block a line sits in says how many columns to expect, so a wrong count is an error at once
rather than a line that parses as the other kind. Prototyped there, the description came to 28 rows plus the
catch-all, every regular group one row, and the Z80 unchanged.

**What made it cheap.** The disassembler needed nothing, since a member's pieces could already render and consume
bytes. `body_key` already gives each value of a slice a reference reads its own body, so the number of immediate
bytes became a property of the body rather than of the row, and a member's steps go in front of the row's. The
exhaustive visits over a member's kind flagged every place the new kind needed a decision.

**Open questions, for when this is taken up for real:**

1. **Derived modes can only borrow.** `mode store = am with 010 -> -` removes a member, and `101 -> other.101` borrows
   one; a member no other mode has means writing the mode out in full (the 6502's `ldx`, whose zero-page `$nn,y` is
   unique to it). The spike recommends keeping `-` and `mode.value` as the only right-hand sides, since a member
   written inline would put `|` and `;` inside a comma-separated list.
2. **A mode must appear in both the mnemonic and the steps, or neither.** The disassembler finds its bytes through the
   text and the interpreter through the steps, so they must agree about the instruction's length; the prototype
   checks this.
3. **One immediate per instruction** still holds: a row naming a mode that fetches may not fetch its own. Relaxing it
   needs a stated byte order; a member's bytes before the row's is the `(ix+d)` precedent.
4. **Precedence reads backwards in places.** The read-modify-write row claims the slots where single-opcode
   instructions sit, so those rows have to come first. Correct, but worth a sentence in CPU_FORMAT.md.
5. **There are now two kinds of vocabulary.** A mode member cannot stand where an operation belongs, and a view cannot
   select from a mode; both are refused, but the distinction needs saying.
6. **A member's steps cannot use `{v:s}` references,** since a member has no slices of its own.
7. **The Z80's `(ix+d)` stays as it is.** A displacement that arrives before the opcode (`dd cb d op`) is the part a
   mode does not model, so latched tables remain.

**Smaller, separate:** a number format the target supplies (`$42` rather than `0x42`), an encoding letter for a byte
that is fetched and ignored (the 6502's `brk`), and keeping a palette's `private:` helpers out of what a row may name.

---

## What is still open

The two lists that used to live here, a review's leftovers and the original plan, are in the
journal now, most of their items struck through. This is what survived them.

- **/INT is a level, and v4 has no way to release it.** Below, in full: it is the only one of
  these that can make the emulator behave differently from the hardware today.
- **WZ/MEMPTR is not modelled.** `bit {b}, (hl)` names `h` as its bus-noise source, which is right
  for that instruction and an approximation elsewhere, the same one v3 makes. See §8 of the
  data-model decisions in the journal.
- **Nothing would catch an undocumented-flag regression.** The only regression test in the
  repository is zexdoc, and "doc" is documented flags. There is no zexall run.
- **A conditional cycle schedule has no expression.** `djnz` is 8 or 13 T-states and the format
  can only say one of them plus a `delay` step. jsbeeb forks the remaining schedule on the
  condition, which states both rather than asserting a range.
- **A row is scanned rather than projected.** The disassembler walks a row's pieces at run time
  where it could be handed a table built at compile time.
- **v4's `.cppm` files cannot compile.** v4 is excluded whenever modules are on, so nothing checks
  them; the table is a header included into more than one partition and its definitions duplicate.
- **Which core is fastest depends on the machine.** v2 leads `z80_bench` on an AMD desktop and on
  the Intel laptop, v4 led it on the Intel desktop in Notes.md, and the cores that did not change
  between two commits moved by 5% to 8% anyway (MEASUREMENTS.md). Not a regression; the per-core
  binaries agree that v4 alone got 11% faster and v2 alone did not move. Why the machines disagree
  about the combined binary is open: indirect-branch prediction on a function-pointer chain is the
  first suspect, and `perf stat -e br_misp_retired.all_branches` per core on each machine is the
  first measurement.
- **A handler's tail call depends on a lambda being inlined.** The lambda that forms `indexed` in
  `execute_one` captures by reference (`[&]`, since 2026-09-22, to quiet clang's unused-capture
  warning), so `displacement` and `decoded` have their addresses taken. Built with `-fno-inline`, gcc
  rejects the handler: "address of automatic variable 'displacement' can escape to 'musttail' call".
  Ordinary builds pass only because the lambda is inlined away. Passing those as the lambda's
  arguments rather than capturing them keeps them by value and satisfies both compilers.
- **Peak compile memory rose by half when the target became a parameter**, from 1.2 GB to 1.8 GB a
  unit, because gcc collects only between top-level declarations. The six-line consumer-side
  workaround is in MEASUREMENTS.md; a library-side one has not been found.

### /INT is a level, and v4 has no way to release it

v4 now holds an interrupt request raised while `iff1` is clear, instead of discarding it, which is
right for the case that motivated it, a request arriving inside a one-instruction `di`/`ei` window.
It is wrong for the case it created.

`Z80Base` offers `interrupt()` and nothing else: there is no deassert. `Spectrum::video_line()`
raises the request once a frame, and on real hardware /INT is held for about 32 T-states and then
released. So a routine running under `di` across a frame boundary (loaders, multicolour, border
effects all do this) leaves a request latched, and v4 takes it on the eventual `ei` where hardware,
and v1/v2/v3, take nothing.

Two ways out, both out of scope for the change that found it:

1. **Give the request a release.** `Z80Base` grows a deassert, and `Spectrum` drops the line after
   the documented window. Correct, and it fixes all four cores at once, but it changes shared
   framework and every front end that raises an interrupt.
2. **Give the request a lifetime inside v4.** Record the cycle it was raised at, and expire it after
   ~32 T-states. Keeps the fix's benefit, needs no shared change, and puts a machine-specific number
   inside the CPU where the machine cannot see it, which is the wrong place for it, but a small
   wrong place.

Until one of them lands, v4 differs from the other three in a way real software could notice, and
the difference is *more* wrong than what it replaced for long `di` regions, and *less* wrong for
short ones.
