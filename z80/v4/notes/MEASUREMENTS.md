# Measurements

How fast it runs, how long it takes to build, and what the accuracy costs. **Any number with a
method behind it goes here**, together with how it was taken, because more than one of these
reversed a conclusion the laptop had already sold us.

Part of [v4's notes](../NOTES.md).

---

## Speed, measured

zexdoc, sequential, same machine, same `release-reflection` build, one run each. All four execute the
identical program, so the ratio is relative interpreter throughput.

| | | vs v4 |
|---|---:|---:|
| v1 | 262.2s | 2.45× slower |
| v2 | **88.8s** | 1.20× faster |
| v3 | 108.4s | 1.4% slower |
| **v4** | **106.9s** | n/a |

The number that matters: **v4 is level with v3**, the code-generated one. Generating an interpreter
from a table costs nothing against generating one from a C++ generator. And both are 2.4× faster than
v1's decode-then-execute.

**v2 is 20% faster than both, and it is not dispatch.** v2 does
`impl::table<impl::build_execute_hl>[opcode](*this)`, a 256-entry function-pointer table, exactly
v4's shape, and both keep `read`/`write` out of line. So the `template switch` theory (that
reflection cannot generate the jump table a hand-written `switch` gets) does not explain this: v2
does not have one either.

Where the difference actually is has not been established, and guessing is not worth much. The
hypothesis worth testing first is that v4 routes *every* idle cycle through `Z80::bus`, an
out-of-line call that switches on the access kind and stores the bus address, where v2 charges its
internal cycles directly. That would be the price of *Time passes in exactly one place*, a design
choice made deliberately so contention has somewhere to live, and one worth knowing the cost of
before the Spectrum needs it. **Profile before believing any of this.**

Caveats: one run each, no repeats, on a laptop; and zexdoc's instruction mix is ALU-heavy, so this
under-reports dispatch cost relative to a program doing more loads and jumps.

## Compile time, measured

The other half of the trade, and the one that is easy to forget because `ccache` hides it. Same
compiler for all four (gcc 16.2, `-O0 -g`, `-freflection`), `ccache` bypassed, each translation unit
compiled on its own. The sweep was run in both orders and the **minimum** of each TU taken, because
this laptop moves a compile by 30% depending on what ran before it, v4's disassembler TU measured
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
because that is where the 1792 `execute_one` instantiations land. `Disassembler.cpp`, the same
parse and every `static_assert`, but no handlers, is the other 15s.

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

- `-ftime-report` and `-ftime-report-details`, a table of *passes*, not of symbols. Useful, and
  used below, but it cannot tell you which instantiation or which `consteval` call was expensive.
- `-fmem-report`, `-fpre-ipa-mem-report`, `-fpost-ipa-mem-report` (allocation by pass.
- `-Q`) prints each function as it is compiled. Crude attribution, and it says nothing about the
  front end, which is where this workload lives.

So on gcc the only way to see inside is to **profile `cc1plus` itself**. That works: the
compiler-explorer build carries no debug info, but it keeps 48,954 dynamic symbols, which is enough
for a flat profile. `perf record -F 199 -- g++ …` follows the driver's children automatically.

The other way, which is what eventually gave the sharpest answers, is to get the thing building
under clang and use `-ftime-trace` there. See "`-ftime-trace` finally answers the question" below.
Everything between here and there is what could be established without it, and it is worth reading
in that light: it took three separate experiments to bound what one traced clang run then measured
directly.

### Where the 90 seconds actually goes

gcc's own accounting for `Z80.cpp`:

| phase | wall | share |
|---|---:|---:|
| parsing | 12.0s | 15% |
| **lang. deferred** (template instantiation and constant evaluation) | **41.3s** | **51%** |
| **opt and generate** (the back end) | **26.1s** | **32%** |
| last asm | 1.0s | 1% |
| (*of which* overload resolution | 11.0s | 14% |
|) *of which* garbage collection | 6.7s | 8% |

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
written them. Another seventh is overload resolution, every `cpu.read([:location:])` is an overload
set to resolve, and there are thousands. Actual constant evaluation is under a tenth.

One entry is worth calling out: `consteval_only_p_walker::walk` at 4.2%, roughly three and a half
seconds, is gcc deciding *whether an expression contains an immediate call*, the analysis P2564's
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
description, the size of the instruction set times the number of decoding tables.

### So what could reasonably change

Everything below reduces to one sentence: **the cost is the number of functions the compiler is
asked to write, so the only changes that matter are ones that ask for fewer.**

1. **Fewer tables.** At 11.2s each this is the largest lever, and two are available without
   inventing anything. `fdcb` is `ddcb` with a different index register, and `iy` is `ix` with a
   different index register; both are written out separately today because a rule rewrites
   vocabulary references and not literal text. Making the index register a runtime value in those
   two would take 7 tables to 5 (about **−22s and −0.25 GB, a 25% cut**) at the price of one
   runtime indirection on the rarest instructions in the set. The 338 byte-identical duplicate
   handlers recorded above are the same observation from the other end, and aliasing them would be
   the same win by another route. This is the firmest number here: gcc's scaling curve says 11.2s a
   table and clang's per-instantiation trace independently says about 12s, so the saving is measured
   rather than estimated.
2. **Split the translation unit, for wall clock only.** Seven TUs would each pay the 12.6s fixed
   cost, so total CPU goes *up*, to about 167s; but wall clock on four cores falls to roughly 45s
   and on sixteen to about 25s. Worth doing for a developer's edit-build loop, not for CI throughput.
   It needs a change first: `inline constexpr auto dispatches` is a namespace-scope variable, so
   **merely including `Execute.hpp` instantiates all 1792 handlers**, used or not. Found the hard
   way, trying to measure one table by including the header and touching nothing.

### And two things that look like levers and are not

Worth stating because both are where one instinctively reaches first, and the measurements say
neither would repay the effort.

- **Optimising the parse.** The whole of reading, checking and lowering the description is inside
  the 12.6s fixed cost (14% of the build) of which the `to_array` double evaluation is 3.2s.
  Deleting the parser outright, checks and all, would leave 86% of the build standing. clang's trace
  later put a finer point on it: the six `to_array` instantiations that *are* the entire pipeline
  cost 6.1 of 126 seconds, and the dearest of them is `opcodes_of_each` rather than anything in the
  parser. Everything in it should be optimised for being read, because that is the only thing it is
  expensive in.
- **Blaming the back end.** It is 32%, the single largest phase, and it is not reflection's doing:
  it is 29 MB of object code at `-O0`, and a Python script emitting the same 1792 functions would
  pay it identically. The only thing that moves it is emitting fewer or smaller handlers, which is
  item 1 above rather than a separate idea.

### The other implementations: two clang forks

There is a second implementation of all this, and "gcc 16 only" is a heavy dependency for a talk to
ask of anyone, so it is worth knowing exactly what it does and does not do. There are in fact **two**
clang forks, and the difference between them matters:

| on Compiler Explorer | fork | clang | flags needed |
|---|---|---|---|
| `clang_bb_p2996` | Bloomberg/clang-p2996 | 21.0.0git | `-freflection -fexpansion-statements -fparameter-reflection` |
| `clang_barry` | brevzin/llvm-project | **23.0.0git** | **`-freflection`** |

Bloomberg's is the reference implementation and is two major versions behind; it also splits the
feature across three switches, so the first two errors one hits are just missing flags. `template
for` is P1306 rather than P2996 and has its own; parameter reflection, the whole mechanism by which
an operation's signature decides what a row may say, has a third. **Barry Revzin's fork wants only
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
| clang 23, libstdc++ 16.2 | 119.9s | 1.43 GB | n/a |
| clang 23, libc++ | 144.7s | 1.43 GB | 36.9 MB |

So **clang is about 1.4× slower than gcc on identical source and an identical standard library**,
and the choice of standard library is worth another 20% on top, libc++ is dearer here than
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
quotes, so a human can read it, but nothing carries it to the top of the error, nothing puts it in
a build log, and an editor jumping to the diagnostic shows "subexpression not valid in a constant
expression" rather than the sentence written for the reader.

**Both forks give that same answer**, so this is not the older one lagging: clang 21 and clang 23
are identical here. **The technique this project uses to make bad tables legible is, today, a gcc
feature.** Worth saying out loud in a talk that recommends it, and worth a bug against clang,
because nothing in the standard prevents printing `what()`, gcc simply chose to.

### And with clang building, `-ftime-trace` finally answers the question

The whole reason the gcc section above is assembled from a pass table and a symbol profile is that
gcc has no `-ftime-trace`. clang does, and it attributes time to *individual template
instantiations*. Same TU, clang 23 with libstdc++, 126s traced:

| phase | seconds | count |
|---|---:|---:|
| Frontend | **119.2** | |
| (`PerformPendingInstantiations` | 85.2 | |
|) `EvaluateAsConstantExpr` | 60.2 | **451,161** |
| (`EvaluateAsInitializer` | 24.4 | 44,675 |
|) `Source` (headers) | 27.9 | |
| Backend | **6.3** | |
|, `CodeGen Function` | 5.9 | 13,982 |
| `CheckConstraintSatisfaction` | 1.7 | 720,105 |

Note the split: **95% front end, 5% back end**, where gcc spent 32% in "opt and generate". The two
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

**Six instantiations of `to_array` are the entire compile-time pipeline**, the `#embed`, the parse,
the checks, the coverage, the decode tables, and they cost 6.1 of 126 seconds. Individually:

```
3.1s  to_array<Table.hpp:43>   row_opcodes: opcodes_of_each, the cartesian product walk
1.4s  to_array<Table.hpp:42>   rows:        parse_rows
1.0s  to_array<Table.hpp:44>   decoded:     decode_tables
1.0s  all_dispatches<0..6>
0.4s  to_array<Table.hpp:40>   vocabularies
```

That is the "don't bother optimising the parse" claim, confirmed to the individual expression by a
different compiler: **reading the description is 5% of the build.** And 1792 `execute_one`
instantiations at 85.1s is 47ms each, so a 256-entry decoding table costs about 12s, against the
11.2s per table gcc's scaling curve gave. **Two compilers, two entirely different measurement
techniques, agreeing to within 10% on what a decoding table costs.**

The most expensive single handlers are the multi-step rows (`call nz,nn`, `call nn`, `rst`) at
about 0.3s each, six times the average. Nothing surprising, but it is the first time the question
"which instruction is expensive to compile?" has had an answer at all.

To regenerate: add `-ftime-trace -ftime-trace-granularity=200` to the clang build and read the
`.json` beside the object file; it is about 16 MB for this TU.

### What compilers could do, since we are going to keep asking for this

- **Give gcc a `-ftime-trace`.** The gcc half of this section is guesswork assembled from a
  pass-level table and a symbol profile of a stripped binary; neither can answer "which
  instantiation cost me a second", which is the only question an author actually has. Getting clang
  building was worth it for this alone, it answered in one run what three gcc experiments had only
  bounded, and it agreed with them. Nobody should have to port to a second compiler to find out
  where their build went.
- **Make the escalation analysis cheaper.** 4.2% spent asking "is this consteval?" scales with
  exactly the feature it is checking for.
- **The tree representation is not built for this.** 5.4 GB allocated and 8% of the build in the
  collector, to evaluate a 147-line text file and stamp out functions from it.
- **Print `what()`.** gcc does; neither clang fork does, tested on both. A `consteval` function
  that throws is the idiom the whole ecosystem is converging on for compile-time diagnostics, and
  half the implementations currently discard the message.
- **Peak memory is the real ceiling.** 1.35 GB for one TU, growing 0.12 GB per table, is what stops
  this scaling, a CI box running several of these in parallel runs out of memory long before it
  runs out of patience.

## What the real Z80 buys, measured

The description targets `v4::Z80 : Z80Base` rather than a stand-in struct, so v4 can be dropped
straight into `z80/test/OpcodeTests.cpp`, that suite is already a template over the
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
carries: `nop` 4, `add a, b` 4, `add a, n` 7, `ld bc, nn` 10, `ld c, (hl)` 7, `ld (hl), n` 10, all
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

Implemented as design decision 6 says, cost lives in an ordered step list, not in an annotation on
the row. The mechanism is one separator:

```
00pp0011 | inc {p}  | inc16 {p} <- {p} ; delay 2
00110100 | inc (hl) | inc8 (hl), flags <- (hl) flags ; delay 1
```

`delay` is not a framework concept: it is an ordinary primitive in the CPU's `Ops`, and the only
new framework rule is that **a primitive may take `Cpu &` as its first parameter**, which the
framework supplies. That is not the `is_supplied_by_framework` mistake returning, that one
special-cased `Flags`, a *domain* type. `Cpu` is the single type the framework is parameterised on,
so it is the one thing it can always hand over, and it is what `jp`, `call`, `push` and `in`/`out`
will all need.

The conditional part (`inc (hl)` costs one more than `inc r`, and `{4,5,6}` is not maskable) needs
no mechanism either. A specific row placed before the general one wins by first-match-wins, which
§5 already requires. This is the same override mechanism the prefix design depends on, so prefixes
now have a working precedent rather than a promise.

Result: the four timing failures are gone. On the unprefixed suite the failure count is now exactly
equal to the undecoded-opcode count, **zero wrong answers of any kind**.

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
discarded, a *write* on the NMOS 6502 and a *read* on the 65C12. I/O is in, because the Z80
genuinely has a separate address space with its own wait state, and separate address spaces are not
unusual.

Contention and cycle stretching are one commented line inside `bus`. Everything they need is already
there: the kind, the address, and `cycle_count()`, from which frame position is `% 70000`. Nothing in
the repo models either today, the Spectrum contends `0x4000-0x7fff` while the display is drawn, and
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
  Ours is a didactic layer over the same three facts, worth knowing it is a convenience, not a
  requirement.
- **Cycles that cannot stretch pass no address.** jsbeeb uses plain `polltime` for zero-page and
  stack, which are always fast. The Spectrum differs: an internal cycle still contends on whatever
  the address bus holds, which is why `idle` presents `bus_address_` rather than nothing.

Two ideas worth stealing that we have no answer for yet:

- **`split(condition)`** forks the remaining cycle schedule on a runtime condition, page crossing on
  the 6502, and exactly the shape of `djnz` 8/13. Better than the `t=min/max` sketch in §6, because
  the two schedules are both stated rather than a range being asserted.
- **The interrupt is sampled at a named position in the schedule**, jsbeeb injects `checkInt()`
  before the penultimate cycle. Since v4 does not handle interrupts at all yet, that is the detail
  that makes them exact rather than approximate, and it argues for adding them as a step position
  rather than a check at the top of `execute_one`.

#### Where cost actually lives

Three places, and none of them is a number written on a row:

- **the fetch cycle**, which the CPU's `read_opcode`/`read_immediate` already pay for
- **the addressing mode**, via `read`/`write` costing 3 and `/delay=1` for the idle cycle in a
  read-modify-write
- **an explicit `delay` step**, for an idle cycle that belongs to the operation rather than to an
  operand, `inc {p} ; delay 2`, and `bit {b}, (hl)`

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
trivial: when a step's primitive is itself out-of-line, `Alu::inc8`, `Z80::read`, `Z80::write`, gcc declines to inline the `apply<…>` specialisation into the handler, so `inc (hl)` pays one extra
call boundary. The body is still fully specialised straight-line code with no branches; it is just
not merged. Whether to force it with `always_inline` is a tuning question, not a design one, and
the 94 bytes/opcode v3 already ships is the number to beat.

### Addresses are a modifier, not a kind

`(hl)` is not a fifth kind of operand alongside constant, immediate, name and field reference. It is
any of those with `indirect` set, "work out the operand, then use it as an address". That
composes for free: `(bc)`, `(de)` and eventually `(nn)` need no new mechanism, and a vocabulary
member may be written `(hl)` because a member's text is parsed by the same function that parses an
operand in a row. Filling the `-` hole in `field r` with `(hl)` was therefore a one-word change to
the table, and it unlocked 24 opcodes across `ld r,r'`, the ALU group and `inc`/`dec r`.

---

## Memoising the reflection queries buys nothing, and nearly said otherwise

`arity_of<Fn>` is a variable template rather than a function, with a comment saying the point is that
the answer is computed once. `takes_cpu<Fn>()` and `operand_for_parameter<Fn, C>()` were not, and
were called five and three times per step, so making them variable templates too looked like free
speed on a translation unit that costs a minute and a half.

Three runs before, three after, of `Z80.cpu`'s own compile with ccache bypassed:

| | run 1 | run 2 | run 3 | mean |
|---|---|---|---|---|
| before | 124.45 | 116.56 | 110.20 | 117.1 |
| after | 92.33 | 95.44 | 96.31 | 94.7 |

Nineteen per cent, and false. The three "before" runs were simply the first three runs of the
session. Alternating the two builds instead, one after the other:

| round | baseline | memoised |
|---|---|---|
| A | 94.66 | 93.31 |
| B | 90.83 | 96.18 |
| C | 94.25 | 96.82 |

Mean 93.2 against 95.4, the memoised build marginally *slower*, everything inside the spread. Peak
RSS is 1.1324 GB either way, to 0.02%. So gcc is already folding the repeated `consteval` calls, or
their cost is far below what this measurement can see.

**This is the third time the same trap has been walked into on this machine**, after the LTO
inlining lottery and the two generation schemes, and the shape is identical every time: a sequence
of A-runs followed by a sequence of B-runs, on a laptop whose first runs of a session are its
slowest. The rule that keeps working is to interleave, and to distrust any wall-clock difference
that a reordering can produce.

`takes_cpu` kept its variable-template form, because five call sites read better without the
parentheses, which is a reason that survives the measurement. `operand_for_parameter` went back to
being a function: memoising it needed a second name, `compute_operand_for_parameter`, and paying a
name for nothing is a bad trade. The claim in `arity_of`'s own comment is now unproven, and is left
standing only because nothing argues against the form it already has.
