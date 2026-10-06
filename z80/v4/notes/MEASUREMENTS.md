# Measurements

How fast it runs, how long it takes to build, and what the accuracy costs. **Any number with a
method behind it goes here**, together with how it was taken, because more than one of these
reversed a conclusion the laptop had already sold us.

Part of [v4's notes](../NOTES.md).

Each section says when it was first written. A number is true of the code as it stood that day, and
the names it uses are that day's names. Where the mechanism has moved since, a dated note in
*italics* says so; the measured results are left as they were taken.

---

## Speed, measured

*First written 2026-08-12. Superseded: on a quiet desktop the gap below is gone (the top-level
Notes.md, "Confirmed on a machine that can actually be measured"), and threading the interpreter has changed
v4's speed since (the top-level Notes.md, "Attempted, and it is worth more than the estimate"). Read this as the
first measurement, and its hypotheses as history.*

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
before the Spectrum needs it. **Profile before believing any of this.** *(2026-10-03: idle cycles
no longer go through `bus`; `Z80::delay` passes time directly. See "Time passes in exactly one
place", below.)*

Caveats: one run each, no repeats, on a laptop; and zexdoc's instruction mix is ALU-heavy, so this
under-reports dispatch cost relative to a program doing more loads and jumps.

## Compile time, measured

*First written 2026-08-13, when v4 generated one handler per (table, opcode) over seven tables, 1792
in all, and dispatched through `all_dispatches` and an `execute_instruction` loop. Views, generation
per body and a vocabulary that is its own slice have cut the handlers a long way since, and the
dispatch is a chain of tail calls; JOURNAL.md measures each change as it landed. This section and
its subsections, to "What compilers could do", describe that earlier build.*

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
- `-fmem-report`, `-fpre-ipa-mem-report`, `-fpost-ipa-mem-report`: allocation by pass.
- `-Q`: prints each function as it is compiled. Crude attribution, and it says nothing about the
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
| *of which* overload resolution | 11.0s | 14% |
| *of which* garbage collection | 6.7s | 8% |

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
   rather than estimated. *(2026-10-03: done, by views. JOURNAL.md, "Done: a view is a parameter,
   not a copy", measured what it saved.)*
2. **Split the translation unit, for wall clock only.** Seven TUs would each pay the 12.6s fixed
   cost, so total CPU goes *up*, to about 167s; but wall clock on four cores falls to roughly 45s
   and on sixteen to about 25s. Worth doing for a developer's edit-build loop, not for CI throughput.
   It needs a change first: `inline constexpr auto dispatches` is a namespace-scope variable, so
   **merely including `Execute.hpp` instantiates all 1792 handlers**, used or not. Found the hard
   way, trying to measure one table by including the header and touching nothing. *(2026-10-03: no
   longer: the dispatch tables are members of `Interpreter<Target>`, instantiated only for a target
   something runs.)*

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

**Both are capable.** Every idiom v4 depends on was tried against both: enumerator splices
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
| *of which* `PerformPendingInstantiations` | 85.2 | |
| *of which* `EvaluateAsConstantExpr` | 60.2 | **451,161** |
| *of which* `EvaluateAsInitializer` | 24.4 | 44,675 |
| *of which* `Source` (headers) | 27.9 | |
| Backend | **6.3** | |
| *of which* `CodeGen Function` | 5.9 | 13,982 |
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

*First written 2026-08-11, while the table was being built up a row at a time; every opcode of every
table has decoded since. The subsections below, to "Addresses are a modifier", date from the same
week and use that week's names.*

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
new framework rule is that **a primitive may take the machine by reference as its first parameter**, which the
framework supplies. That is not the `is_supplied_by_framework` mistake returning, that one
special-cased `Flags`, a *domain* type. The machine is the single type the framework is parameterised on,
so it is the one thing it can always hand over, and it is what `jp`, `call`, `push` and `in`/`out`
will all need.

*2026-10-03: both halves of that have moved. An operation that needs the machine is now a member of
it, published with `[[=refract::operation]]`, and one that asks for the machine as a parameter is
refused (JOURNAL.md, "Done: the machine publishes its verbs"). And `delay` is now part of what the
framework calls (`refract/Machine.hpp`), since the framework charges a write-back delay itself; the
Z80 publishes the same member as an operation so that rows can write `delay` steps.*

The conditional part (`inc (hl)` costs one more than `inc r`, and `{4,5,6}` is not maskable) needs
no mechanism either. A specific row placed before the general one wins by first-match-wins, which
§5 already requires. This is the same override mechanism the prefix design depends on, so prefixes
now have a working precedent rather than a promise.

Result: the four timing failures are gone. On the unprefixed suite the failure count is now exactly
equal to the undecoded-opcode count, **zero wrong answers of any kind**.

### Time passes in exactly one place

*Rewritten 2026-10-03. As first written on 2026-08-11, `bus` was the only function in the CPU that
advanced the clock, idle cycles included, and the accessors had other names (`read_opcode`,
`read_immediate`, `read`, `write`, `idle`). Idle cycles now take the shorter route below.*

Every access advances the clock through `Z80::bus(Bus kind, uint16_t address)`: `fetch_opcode`,
`fetch_immediate`, `read_memory`, `write_memory` and the port accesses all route through it. It takes
the address and runs *before* the transfer, so anything scheduled sees the machine as it was at the
moment of the access.

Idle cycles do not go through it. `Z80::delay` passes time directly, because an internal cycle
transfers nothing and presents whatever address the last access left on the bus, so a run of them
only moves the clock, and spending them in one go fires the same tasks at the same cycles as one at a
time. Both routes end in `pass_time`, which is the one place time passes. A machine that contends
each internal cycle separately would loop inside `delay`.

```cpp
enum class Bus : std::uint8_t { opcode, operand, read, write, io_read, io_write, internal };
```

The enum is **per-CPU**, declared in `Z80.hpp` rather than the framework. A 6502 declares its own,
and would add the one kind the Z80 has no use for: a dummy cycle the bus sees but whose value is
discarded, a *write* on the NMOS 6502 and a *read* on the 65C12. *(2026-10-04: the 6502 now in
`refract/test/m6502/` has no bus at all: it counts a cycle per `read_memory` and `write_memory`, and
charges its dummy cycles inside the operations that cause them, such as `zp_index`.)* I/O is in, because the Z80
genuinely has a separate address space with its own wait state, and separate address spaces are not
unusual.

Contention and cycle stretching belong inside `bus`, and a comment there says so; nothing models
either yet. Everything they need is already there: the kind, the address, and `cycle_count()`, from
which frame position is `% 70000`. The Spectrum contends `0x4000-0x7fff` while the display is drawn,
and none of v1, v2 or v3 attempt it either.

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
  *(2026-10-03: `idle` is now `delay`, which keeps `bus_address_` as the last access left it, for a
  contending machine to read.)*

Two ideas worth stealing that we have no answer for yet:

- **`split(condition)`** forks the remaining cycle schedule on a runtime condition, page crossing on
  the 6502, and exactly the shape of `djnz` 8/13. Better than the `t=min/max` sketch in §6, because
  the two schedules are both stated rather than a range being asserted.
- **The interrupt is sampled at a named position in the schedule**, jsbeeb injects `checkInt()`
  before the penultimate cycle. Since v4 does not handle interrupts at all yet, that is the detail
  that makes them exact rather than approximate, and it argues for adding them as a step position
  rather than a check at the top of `execute_one`. *(2026-10-03: v4 now takes interrupts in
  `start_instruction`, between instructions, which is the approximate version; a named position in
  the schedule is still not done.)*

#### Where cost actually lives

Three places, and none of them is a number written on a row:

- **the fetch cycle**, which the CPU's `read_opcode`/`read_immediate` already pay for
- **the addressing mode**, via `read`/`write` costing 3 and `/delay=1` for the idle cycle in a
  read-modify-write
- **an explicit `delay` step**, for an idle cycle that belongs to the operation rather than to an
  operand, `inc {p} ; delay 2`, and `bit {b}, (hl)`

A row states a number only in the third case, which is the case where the number is genuinely a
property of that instruction. Everything else falls out. *(2026-10-03: the accessors are now
`fetch_opcode`, `fetch_immediate`, `read_memory` and `write_memory`; CPU_FORMAT.md, "Where time
goes", has the current list.)*

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
trivial: when a step's primitive is itself out-of-line, `Alu::inc8`, `Z80::read`, `Z80::write`, gcc declines to inline
the `apply<…>` specialisation into the handler, so `inc (hl)` pays one extra
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

*First written 2026-08-16. `takes_machine` has since gone, with the rule it served: an operation
that needs the machine is a member of it now, and `machine_member` and `asks_for_machine` say which.
`arity_of`'s comment has since been rewritten too, and now claims only that the form reads better.*

`arity_of<Fn>` is a variable template rather than a function, with a comment saying the point is that
the answer is computed once. `takes_machine<Fn>()` and `operand_for_parameter<Fn, C>()` were not, and
were called five and three times per step, so making them variable templates too looked like free
speed on a translation unit that costs a minute and a half.

Three runs before, three after, of `Z80.cpp`'s own compile with ccache bypassed:

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

`takes_machine` kept its variable-template form, because five call sites read better without the
parentheses, which is a reason that survives the measurement. `operand_for_parameter` went back to
being a function: memoising it needed a second name, `compute_operand_for_parameter`, and paying a
name for nothing is a bad trade. The claim in `arity_of`'s own comment is now unproven, and is left
standing only because nothing argues against the form it already has.

## Bundling the handler's arguments costs 3%

*First written 2026-08-16, and counted rather than timed. "The fetch was the call that mattered",
below, is why a count says whether the work changed but not what a change costs in time; this
result has not been re-measured in nanoseconds.*

`Handler` takes three `std::uint8_t`s positionally through a function-pointer table, so swapping two
of them is caught by nothing. That is the coupling this file objects to on a description's behalf,
and `Decoded` already bundles what an instruction carries for the same reason, so bundling these
looked like tidying with no downside.

Two fixed binaries, alternated, counting retired instructions rather than time:

| | run 1 | run 2 | spread |
|---|---|---|---|
| positional | 14,324,983,856 | 14,323,098,682 | 0.013% |
| bundled | 14,776,249,993 | 14,762,781,388 | 0.09% |

**+3.1%**, which is two hundred times the reproducibility of the measurement. Not neutral, and not
close. The structure is three bytes, trivially copyable, and passed by value; the cost is presumably
gcc materialising it rather than keeping three arguments in three registers, across a call it cannot
see the far side of.

Two things this confirms beside the answer. Retired instructions really do reproduce to about 0.01%
on this laptop, which makes them worth reaching for whenever a question can be phrased in them; and
`Decoded` being free is not evidence that another bundle will be, because `Decoded` is materialised
once per instruction inside a function the compiler can see all of, where this crosses a function
pointer.

So `Handler` stays positional, with a comment saying it was measured rather than merely preferred.
The safety it gives up is real: the two call sites are `execute_one`'s hand-over and
`continue_running`, and nothing but reading them would catch a transposition.

## Making the target a parameter costs peak memory, and where gcc collects is why

*First written 2026-09-21.*

Peak RSS and wall clock of one compile, gcc 16.2 `RelWithDebInfo`, `/usr/bin/time`, same machine,
each configuration compiled twice and agreeing to 0.1% on memory. `Disassembler.cpp` is the parse
and every check with no handlers; `Z80.cpp` is the same plus the interpreter.

| | `Disassembler.cpp` | `Z80.cpp` |
|---|---:|---:|
| namespace-scope constants, macro-named file (before) | 14.6s / 1.17 GB | 52.6s / 1.23 GB |
| `Compiled<Text, File>`, checks and steps all pulled in by one instantiation | 17.7s / 1.79 GB | 55.5s / 1.81 GB |
| the same without the catch-and-rethrow wrapper | 15.5s / 1.55 GB | |
| the same with each step named by its own top-level `static_assert` in `Target.hpp` | 18.3s / 1.42 GB | |

gcc's `-ftime-report` for `Disassembler.cpp`: constant expression evaluation allocated 1399M before
and 1723M after (+23%), but garbage collection took 0.85s before and 0.37s after. The allocation
grew a little; the collections halved. gcc collects between top-level declarations, and the old
layout had six of them where the new one has a single instantiation.

Three arrangements inside the library made no difference, because none of them is a top-level
declaration: variable templates at namespace scope instantiated from `Compiled`, member functions
so that `Compiled` itself evaluates nothing, and the seven checks as separate `static_assert`s.

The lines that do help, one per step of the pipeline, in a consumer before its `Target`:

```cpp
static_assert(!refract::steps::vocabularies<Z80Source>.empty());
static_assert(!refract::steps::tables<Z80Source>.empty());
static_assert(!refract::steps::rows<Z80Source>.empty());
static_assert(!refract::steps::row_opcodes<Z80Source>.empty());
static_assert(!refract::steps::decoded<Z80Source>.empty());
static_assert(!refract::steps::latched<Z80Source>.empty());
```

*(2026-10-03: written in today's spelling, where a step takes the `SourceLike` type that carries
the text and the file name, `Z80Source` in `Target.hpp`. It was measured as
`steps::vocabularies<z80_cpu, "z80.cpu">`, when `Compiled` took the two as separate template
arguments. The new spelling has not been re-measured.)*

Not adopted, because it is the boilerplate the change exists to remove, and 1.8 GB a unit fits
the machines that build this. Written down because it is the first thing to reach for if it stops
fitting, and because "where does the compiler collect" is not a question the source usually asks.

## The fetch was the call that mattered, and two ideas that did not

*First written 2026-09-21.*

Nanoseconds per emulated instruction over 20M instructions of zexdoc, `z80_bench_v4` and
`z80_bench_v2`, one core per binary, gcc 16.2 `RelWithDebInfo`, the bench's own best of five,
three rounds alternating the two builds on a laptop with a load average near four. The bests
agree across rounds to within 1%, which is the number to read; the spreads within a round were
15% to 45%, which is the machine.

| | before (`2b2b916`) | after (`27751d0`) | |
|---|---:|---:|---:|
| v4 alone, with `fetch_opcode`, `fetch_immediate`, `fetch_immediate16` inline | 10.79 / 10.84 / 10.74 | **9.58 / 9.56 / 9.62** | **-11%** |
| v2 alone, unchanged source, for reference | 9.08 / 9.05 / 9.17 | 9.06 / 8.98 / 9.05 | 0% |

**The fetches were the call.** `continue_running` is the one function every handler tail-calls,
and the compiler had already inlined `start_instruction` into it, halt loop and interrupt path out
of line, without being asked; the disassembly showed one `call` left on the hot path, to
`fetch_opcode`. The same lesson as `Memory::read` and `Scheduler::tick` in the top-level Notes.md: with LTO on,
`inline` in a header tells the compiler nothing about visibility and everything about which budget
applies, and for a function with a call site in every one of several hundred handlers the auto
budget says no. v2 is still the faster core alone, here as on the desktop in the top-level Notes.md.

**Retired instructions are a change detector, not a benchmark.** They were used first here, being
repeatable to a few parts per billion on this machine, and they moved by the same 11%
(2,884,824,542 to 2,562,452,224 user-space instructions over the run). That agreement is luck: a
count weighs a divide as an add and a stalled load as a register move, so it cannot say what a
change does to time, only whether the work changed. Two of the conclusions below were first drawn
from the count and are stated here as what they are.

**`bus`, `refresh` and `delay` inline too**: no change in the count, so not adopted; not timed.

**Clearing the deferred-interrupt flag only when it is set**, instead of an unconditional store
every instruction, added six instructions per emulated instruction to the count and was not timed.
A predicted branch that skips a store may well be faster than the store; this one is open, and a
timed comparison is what would settle it.

**`[[gnu::preserve_none]]` has nothing to remove.** The attribute makes a function preserve no
registers, which CPython's tail-called interpreter uses so that a handler entered by a jump and
left by a jump does no saving. Counted in the binary before trying it: of the 757 generated
handlers, none saves a callee-saved register and none sets up a stack frame. The compiler already
knows a `musttail` chain returns nowhere, and the handlers' own calls out to the scheduler force
their live values to memory whatever the convention. The one place it would act is a single `rbx`
save in `continue_running`, for a gcc-15-and-clang-only attribute on a function pointer type. Not
measured, because the disassembly said there was nothing to measure.

**The combined binary moves every core when one changes.** In `z80_bench`, which links all four,
the same two commits gave v2 11.1 to 11.8, v3 13.0 to 12.0 and v1 24.4 to 26.3 ns, none of whose
source changed, while v4 went 12.7 to 12.2. That is the link-order effect the top-level Notes.md describes, and
it is why the per-core binaries are the ones to read a change from. It is also why "which core is
fastest" depends on the machine: v4 led the combined binary on the Intel desktop in the top-level Notes.md, and
v2 leads it on this laptop and on an AMD desktop.

## Two server nodes, and v4 no longer leading

*First written 2026-10-05, at `260c0bf`. Matt's runs; the build and the bench's flags were not recorded.*

*Later the same day: most of the combined binary's deficit was the scheduler's slow path, inlined into
every core's hottest functions, and marking it `[[unlikely]]` won back 12% to 18% for v2, v3 and v4 on
the desktop (the top-level Notes.md, "The scheduler's slow path, and the `[[unlikely]]` that moved it").
Pick a game for `--snapshot` that keeps the CPU busy: one that sits halted measures how each core models
`halt` (the top-level Notes.md, "Real games, and two traps in measuring them").*

*2026-10-06: the same nodes after that change are in the section below, and v4 leads on one of them.*

`z80_bench` with all four cores in one binary, zexdoc, nanoseconds per emulated instruction, best of the
bench's repetitions.

| | shared AMD node | spread | dedicated Intel node | spread |
|---|---:|---:|---:|---:|
| v1 | 24.84 | 1.3% | 15.23 | 2.7% |
| v2 | **8.81** | 28.1% | **5.89** | 9.5% |
| v3 | 9.35 | 0.9% | 6.34 | 6.7% |
| v4 | 9.62 | 12.1% | 6.50 | 7.9% |

**The order is the same on both:** v2, v3, v4, v1. v4 is 2% to 3% behind v3 and about 10% behind v2,
so generating the interpreter from a table still costs about what generating it from C++ does.

**v4 led this binary once, before it was threaded.** On the i9-9980XE desktop (the top-level Notes.md,
"Confirmed on a machine that can actually be measured") v4 was 10.05 ns to v2's 10.21 in the combined
binary, and v2 led when each was built alone. That was a gap of under 6% either way and the link-order
effect of the section above; threading came afterwards and was measured v4 against v4. Neither node
reproduces the lead.

**zexdoc cannot show what threading is worth.** The bench drives zexdoc through `execute_one()` one
instruction at a time, to watch for CP/M calls, so a handler's tail call into the next one collects
nothing between instructions; threading measured 8% here and 15% to 17% on games run a frame at a time
(the top-level Notes.md, "Attempted, and it is worth more than the estimate"). `--snapshot` runs a game
through frames, and is the mode in which to look.

**Read the spreads first.** v2's 28% on the shared node says its best may be a lucky repetition, and 7%
to 10% on a dedicated node is wide against the desktop's 0.5% to 1.5%. Neither node is comparable with
the desktop's absolute figures.

### The same nodes after `[[unlikely]]`, with v4 leading one

*2026-10-06, after `fdee005` (#54), which marked the scheduler's slow path `[[unlikely]]`. Matt's runs
again: `z80_bench` on zexdoc with the four cores in one binary, the bench's best of its repetitions. The
build and the bench's flags were not recorded. The change is the time per instruction against the table
above, so a minus is faster.*

| | AMD node | spread | change | Intel node | spread | change |
|---|---:|---:|---:|---:|---:|---:|
| v1 | 26.74 | 1.6% | +8% | 17.88 | 3.8% | +17% |
| v2 | 11.17 | 0.6% | +27% | **5.41** | 5.1% | -8% |
| v3 | 9.61 | 0.4% | +3% | 6.78 | 1.9% | +7% |
| v4 | **8.38** | 1.2% | -13% | 5.59 | 1.5% | -14% |

**v4 gained 13% to 14% on both**, as it did on the desktop, where it gained 17% to 18% in the combined
binary. That is the one result all three machines agree on.

**On the AMD node v4 now leads**, with v3 15% behind and v2 33%. Every spread there is under 2%, where
the first run had v2 at 28% and v4 at 12%, so this is the run to believe. On the Intel node v2 leads by
3%, inside its own 5% spread, in the desktop's unpadded order: v2, v4, v3, v1.

**v2 and v3 did not move as they did on the desktop**, where both gained. v3 lost 3% and 7%, and v2
gained 8% on Intel and lost 27% on AMD. The AMD figure v2 lost against is the one its 28% spread said
might be a lucky repetition, and with the build changed in between, the two cannot be separated. Not
chased.

**v1 lost on both**, 8% and 17%, as it lost 6% to 10% on the desktop. It has lost wherever this change
has been measured.

**For the talk this is still parity.** v4 in the combined binary is first on one machine, second by 3%
on another, and second by 7% on the desktop unpadded. None of those gaps is larger than what inlining
and code layout move.
