# v4: notes

Notes on v4, the Z80 core that the `refract` library (in `refract/`) generates at compile time from
[`z80.cpu`](z80.cpu): what it does today, what is still open, and where the rest of the record lives.
[README.md](README.md) is where to start reading. The `.cpu` format itself, its grammar, semantics
and worked examples, is [CPU_FORMAT.md](CPU_FORMAT.md). These notes are about *why* it is that shape.

Known bugs in v1/v2/v3 found while researching this are filed as issues rather than recorded here.

## The rest of the notes

This file is the current state. Everything else lives next door, and each file has a rule about
what belongs in it, so that this one stops growing:

| | holds | what goes in it |
|---|---|---|
| [notes/FINDINGS.md](notes/FINDINGS.md) | what C++26 actually did, on a real compiler | any new language fact |
| [notes/MEASUREMENTS.md](notes/MEASUREMENTS.md) | speed, build cost, what accuracy buys | any number, with its method and date |
| [notes/PREFIXES.md](notes/PREFIXES.md) | how the prefix design was reached, and why DDCB is not `cb` renamed | that one argument, kept whole, as a dated record |
| [notes/WASM.md](notes/WASM.md) | getting v4 into the browser, and what that took | anything about the wasm build |
| [notes/6502.md](notes/6502.md) | the 6502, refract's second machine, as a test of the format | what a second CPU shows about the format |
| [notes/JOURNAL.md](notes/JOURNAL.md) | what was decided and why, in order | **new "Done:" entries** |

How prefixes, views and latched tables work today is CPU_FORMAT.md's business, not PREFIXES.md's.

The journal is a record, not a reference: every entry was true when it was written and describes
the code at that moment. When it disagrees with this file, this file wins; when this file
disagrees with the code, the code wins.

---

## What v4 does

`z80.cpu` is `#embed`ed, parsed during constant evaluation by refract, and drives two artefacts.
Every opcode of every table decodes, prefixed pages included: `base`, `cb`, the `ix`/`iy` views of
`indexed`, the `indexed_cb` pages that `dd cb` and `fd cb` reach, and `ed` are complete, and the
instruction set is finished. That is a compile-time check, so a description with a gap does not
build.

- **Disassembly** (`refract/include/refract/Disassemble.hpp`) walks the row's lowered pieces, following a `goto`
  through a prefix table.
- **Execution** (`refract/include/refract/Execute.hpp`) generates one function per *body*: a row together with the
  slices it reads, so that every opcode which would generate the same code shares one. Each is an
  `execute_one<Table, BodyKey, Index>` instantiation, made by a `template for` expansion statement,
  and resolves its verbs by reflection over the target's palettes and the machine's marked members.
  Each declared table gets a 256-entry table of pointers to those functions; a view is a run-time
  argument rather than a second table. No handler returns to a loop: each ends in a
  `[[clang::musttail]]` call to the next instruction's handler, or, for a prefix, to the next
  table's, so a run is one chain of tail calls.

The pipeline is `#embed` → parse during constant evaluation → lower to validated pieces →
`template for` → splice.

A row has three columns: an encoding token sequence, a mnemonic, and an ordered list of steps. Each
column is checked against the others; what is fetched comes from the encoding (and, for a
displacement, from what the operands resolve to), never from the display text.

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
table. Every correctness hole found in the first cut of v4 was in the half left at runtime.
Lowering the table to a validated fixed shape at parse time removes that half entirely.

---

## Where the framework/CPU boundary sits

`refract/` is the library, and knows no CPU; it is a CMake target of its own, whose tests build with
nothing of the Z80 on the include path. The Z80's whole side of the contract is `z80.cpu`,
`Target.hpp`, `Operations.hpp`, and `Z80.hpp` with its definitions in `Z80.cpp`; `Disassembler.cpp`
only says where the disassembler's bytes come from. Retargeting means writing these and nothing
else:

- `Target`: which machine, which description, and which palettes
- `Z80`, the machine state, marking with `[[=refract::operation]]` the verbs that touch it
- `Operations`, a palette of verbs that touch nothing (the shared `Alu` is the other palette)
- `read`/`write` overloads: how to touch storage, and, by marking each `read` with
  `[[=refract::location]]`, what storage a description may name
- `read_memory`/`write_memory` and their 16-bit forms: how to touch memory through an address
- `fetch_opcode`/`fetch_immediate`: how to read the instruction stream
- `displaced_address` and `displacement_window_bytes`, for a description with displaced rows: how a
  base and a signed displacement combine, what forming the address costs, and how many bytes the
  window it takes holds
- `delay`: how to spend an idle cycle
- `start_instruction`: what happens between instructions (an interrupt, a halt), and whether to
  run another
- `refract::Spelling` annotations, where an enumerator's assembly spelling differs from its C++ name

`refract/include/refract/Machine.hpp` states the part the framework calls as a concept, and CPU_FORMAT.md, "What the
CPU description must supply", says what each construct in a row needs.

A verb that needs the machine is a member of it, called on it; a verb that does not is a static
function of a palette, called on nothing. The machine's public interface is larger than its
vocabulary, so it publishes its verbs one by one, and its locations the same way, by marking the
`read` of each; a palette is built to be named, so everything public in it counts.

The framework names no CPU type at all: not `RegisterFile`, not `Alu`, not `Flags`. It knows only
that a row has a verb, some operands and some destinations, and that the CPU can resolve a name.
It used to infer meaning from C++ types (an accumulator, a carry flag behind a `bool`); how that
went is in the journal, "Done: the framework stopped inferring meaning from types".

### What the framework relies on instead

The properties of the primitive, all read by reflection, none of them Z80-specific:

- its arity, checked against the number of operands the row supplies
- its parameter types, which each operand converts to, so a 16-bit location handed to an 8-bit
  parameter is a `-Wconversion` error in this project's build, not a truncation; and an enum
  parameter makes a name a value rather than a location
- its return type: `void` means the row may name no destination; `refract::Continue` makes the step
  a condition, with no destination; a single value means one or more destinations, each of which
  receives it; and an aggregate means one destination per member, in declaration order. Any other
  class is one value, and an aggregate with a single member or a base class is refused.

A `-` destination discards a component, which is how `cp` uses `cmp8` without writing the result
back to `a`.

---

## What a second CPU would need

The format has described one real processor. An outside reader given only
[CPU_FORMAT.md](CPU_FORMAT.md), told to know 6502 and Z80 but not to look at the code, predicted
where a 6502 would break it. A 6502 description was then prototyped on 2026-09-27 and 2026-09-28,
on the branch `mg/v4_spike_6502`. Its first form, which needs no change to refract, is now refract's
second machine: the documented NMOS instruction set in `refract/test/m6502/6502.cpu`, run by
`refract/test/M6502Test.cpp` with nothing of the Z80 in reach, and written up in
[notes/6502.md](notes/6502.md). The branch (unmerged, kept on origin) goes on to try the `mode` block
below. What it found:

- **An addressing mode cannot fetch its own operand, and that is the limit that bit.** The 6502's
  `aaabbbcc` puts the mode in `bbb`, which is what a vocabulary is for, but its modes fetch
  different numbers of bytes, and a member may neither render nor pass an immediate. With refract
  unchanged the description was 46 rows plus a catch-all for 151 opcodes, a row per group per mode,
  so the format's headline ("one row, sixty-four instructions") did not transfer.
- **Nested indirection and register indexing did not block.** Addresses are formed in steps through
  `ea`, a location standing for the chip's address latch: `(zp),Y` is
  `zp_pointer ea <- n ; index ea <- ea y`, then `(ea)`. The machine has no `displaced_address`,
  which only a machine with displaced rows needs.
- **Cost that depends on the data moved into the machine.** A page crossing costs a read a cycle
  and a write always: `index` charges only when the page changes, `index_store` always, and the row
  says which. On the branch's `mode` version, `index` records that a fix-up is owed and the access
  that follows settles it, which is what the chip's dummy read does, so one addressing mode serves
  loads, stores and read-modify-writes.
- **Bus timing at step granularity** (CPU_FORMAT.md, "What this model cannot say") was not tested.
  The Spectrum's contention will decide whether it matters.

Eight-bit opcodes, conditions, relative jumps, the stack and page-zero addressing all worked as they
were, and the 6502's flag model was no harder than the Z80's. The gaps are addressing modes, not the
shape of the thing.

### The design it points to: a mode is a block of row fragments

A `mode` declaration opens a block, as `table` does, and each line in it is one member, written like
a row: the slice's value with the bytes the member adds to the encoding, the text it renders, the
operand it stands for, and the steps it runs first. A row names a mode with the ordinary reference
syntax; the member's steps run once, before the row's own, however often the row names it.

```
mode am
000 n   | ($nn,x) | (ea) | zp_index ea <- n x ; zp_pointer ea <- ea
010 n   | #$nn    | n    |
...
aaabbb01 | {alu:a} {am:b} | {alu:a} a, p <- a {am:b} p
```

Prototyped on that branch, it took the description to 28 rows plus the catch-all, every regular
group one row, with the Z80 unchanged. It was cheap because a member's pieces could already render
and consume bytes, and `body_key` already gives each value of a slice its own body, so the width of
the immediate could become a property of the body rather than of the row.

Open questions, for when this is taken up for real:

1. **Derived modes can only borrow.** `mode store = am with 010 -> -` removes a member and
   `101 -> other.101` borrows one; a member no other mode has means writing the mode out in full.
2. **A mode must appear in both the mnemonic and the steps, or neither**, since the disassembler
   finds its bytes through the one and the interpreter through the other.
3. **One immediate per instruction** still holds; relaxing it needs a stated byte order.
4. **Precedence reads backwards in places**: the read-modify-write row claims the slots where
   single-opcode instructions sit, so those rows come first. Worth a sentence in CPU_FORMAT.md.
5. **There are now two kinds of vocabulary**: a mode member cannot stand where an operation belongs,
   and a view cannot select from a mode.
6. **A member's steps cannot use `{v:s}` references**, since a member has no slices of its own.
7. **The Z80's `(ix+d)` stays as it is.** A displacement that arrives before the opcode
   (`dd cb d op`) is the part a mode does not model, so latched tables remain.

**Smaller, separate:** a number format the target supplies (`$42` rather than `0x42`); an encoding
letter for a byte that is fetched and ignored (the 6502's `brk`); and the helpers a palette's
operations share are nameable from a row because they are public, which a `private:` section
already prevents.

---

## What is still open

The two lists that used to live here, a review's leftovers and the original plan, are in the
journal now, most of their items struck through. This is what survived them.

- **/INT is a level, and v4 has no way to release it.** Below, in full: it is the only one of
  these where v4 behaves differently from v1, v2 and v3.
- **WZ/MEMPTR is not modelled.** `bit {b}, (hl)` names `h` as its bus-noise source where the chip
  uses W, the high byte of MEMPTR: an approximation, the same one v3 makes. See §8 of the
  data-model decisions in the journal.
- **Nothing would catch an undocumented-flag regression.** The only regression test in the
  repository is zexdoc, and "doc" is documented flags. There is no zexall run.
- **Nothing checks that the two artefacts agree about length.** The disassembler and the interpreter
  take an instruction's length from the same derivation, but no test walks every (table, opcode)
  comparing the disassembled length with how far the program counter moved, or checks that nothing
  renders `??`.
- **A push writes its two bytes low first.** `write_memory16` serves `ld (nn), hl` and `push`
  alike, and the chip pushes the high byte first. The bytes land in the same places, so only
  contention or a watchpoint could tell (PREFIXES.md, "What is papered over").
- **A row is scanned rather than projected.** The disassembler walks a row's pieces at run time
  where it could be handed a table built at compile time.
- **Which core is fastest depends on the machine.** v2 leads `z80_bench` on an AMD desktop and on
  the Intel laptop, v4 led it on the Intel desktop in [the top-level Notes.md](../../Notes.md), and cores whose source did not change
  between two commits moved anyway (MEASUREMENTS.md, "The fetch was the call that mattered"). Not a
  regression: the per-core binaries agree that v4 alone got faster and v2 alone did not move. Why
  the machines disagree about the combined binary is open: indirect-branch prediction on a
  function-pointer chain is the first suspect, and `perf stat -e br_misp_retired.all_branches` per
  core on each machine is the first measurement.
- **Diagnostics are built by concatenation, waiting on a `constexpr` `std::format`.** Every message refract throws
  during constant evaluation is a chain of `+` with `std::string(...)` and `decimal(...)` around its parts, because
  libstdc++ 16's `std::format` cannot run there. gcc trunk's can, so the plan is to move to `std::format` once gcc 17
  is released, staying on released compilers for the main build rather than moving to trunk for this. The WASM build's
  libc++ would still need a stand-in then. The run-time tests could use `std::format` today. Details in FINDINGS.md.
- **Peak compile memory rose when the target became a parameter**, because gcc collects only
  between top-level declarations (MEASUREMENTS.md, "Making the target a parameter costs peak
  memory"). A consumer-side workaround is recorded there; a library-side one has not been found.

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
   the documented window. Correct, and it fixes every core at once, but it changes shared
   framework and every front end that raises an interrupt.
2. **Give the request a lifetime inside v4.** Record the cycle it was raised at, and expire it after
   ~32 T-states. Keeps the fix's benefit, needs no shared change, and puts a machine-specific number
   inside the CPU where the machine cannot see it, which is the wrong place for it, but a small
   wrong place.

Until one of them lands, v4 differs from the other cores in a way real software could notice, and
the difference is *more* wrong than what it replaced for long `di` regions, and *less* wrong for
short ones.
