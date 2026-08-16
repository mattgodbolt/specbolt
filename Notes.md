### Ideas for C++26

- deducing this throughout
- coroutines for cycles

---

### First implementation

Tried to decode to an "Instruction" parameterising all the registers so I could break into operation, inputs, outputs.
Obviously this _is_ more like how CPUs actually work, but things like `push` `pop` `exx` `in` `out` tricky as they
have complex interactions with the outside world.

Specifically the input/output of the instruction is not just the registers, but also the flags, and the memory, and
maybe a bunch of other stuff too. That is, trying to make the "apply" function work with params like:

```cpp
  struct Input {
    std::uint16_t dest;
    std::uint16_t source;
    std::uint16_t pc;
    std::uint16_t sp;
    Flags flags;
    bool iff1;
    bool iff2;
    std::uint8_t port_fe;
  };
  struct Output {
    std::uint16_t value;
    std::uint16_t pc;
    std::uint16_t sp;
    Flags flags;
    bool iff1;
    bool iff2;
    std::uint8_t extra_t_states{};
    std::uint8_t port_fe;
  };

  [[nodiscard]] static Output apply(Operation operation, const Input &input);
```

was tricky. It _does_ let me hide the "read indirect" part etc, that is I could use the same code to fill in "dest" and
"source", be it from a register, or RAM, or whatever. But that isn't enough by itself.

`in` and `out` need to poke about with other information, and stuff like `exx` needs to switch all the registers, as
does
`ex (sp), af` type things.

Hana's suggestion was to template parameterize each instruction and have each have its own `apply`. I think that's the
way forward honestly.


---

### First working version.

As of `e2d332346520aa722475d21fbf9b267227bf9800`.

A lot of subtleties lost in the above description. Eventually went with routines taking a cut down input and output
but also a mutable "cpu" that has access to everything else, like in/out ports, memory etc.

Eventually gave up on a lot of the "generic" stuff and just wrote a lot of code. Not as testable or tested as I would
like. Passes the zexdoc test and works with manic miner, jetpac, dizzy and a few other games. Timings are way off and
started looking in to that. But really want a more principled way forward.

Plan is to refactor to use micro-ops; then model each instruction as a sequence of those ops. Can fill in a big table
of them and use it to:

- drive code gen for C++14ish version (?) or at least "my default style"
- actually constexpr-ify and generate functions for each instruction.

Using a similar approach to https://github.com/floooh/chips/blob/master/codegen/z80_desc.yml but programmatically done
in C++. http://www.z80.info/decoding.htm is a good reference, as is the python code from André.

Minimally an instruction takes 4 cycles:

- 1 as part of the "previous" instruction (overlapped)
- 3 cycles decode/memory access/execute

On top of that regular reads and writes take 3, IO takes 4.

---

### Why v2 looked 20% faster than v3 and v4 (and wasn't)

For a long time the zexdoc times said v2 was clearly the quickest and v3/v4 were
paying for their fancier dispatch. That turned out to be wrong, and the way it
was wrong is the interesting part. `z80/test/Bench.cpp` is the harness; run it
with `--impl`, or use the per-implementation binaries (see below).

**Wall-clock on a laptop cannot see a 15% effect.** The same binary varied by a
third between runs of the zexdoc suite. Everything below is `perf` counters:
retired instructions reproduce to better than 0.01%, and cycles to about 2%.

**The ranking depended on how the binaries were linked.** Interprocedural
optimisation is on for the whole build, and every test binary contains all four
implementations, so they are optimised against each other. Cycles per emulated
Z80 instruction:

| | four in one binary | one binary each |
|---|---|---|
| v1 | 134.8 | 135.1 |
| v2 | **55.9** | 67.0 |
| v3 | 63.4 | 58.6 |
| v4 | 62.1 | **54.0** |

v2 wins one column and v4 wins the other. This was found by accident: editing v4
moved *v2's* retired instruction count by 7.5% without v2's source changing.

**The mechanism was two shared functions losing an inlining lottery.** Whichever
one stayed out of line decided the loser: `Memory::read` was 18% of v4's runtime
in the combined binary, and `Scheduler::tick` was 28% of v2's in its own binary.
Neither has anything to do with how an instruction is dispatched.

Both are now fixed at the source: `Memory`'s accessors moved into the header, and
`tick` grew an inline fast path over an out-of-line `tick_with_tasks`. Every
implementation got faster (v1 -25%, v2 -18%, v3 -31%, v4 -29%) and v2, v3 and v4
now execute within 1% of the same number of x86 instructions per Z80
instruction. The gap that looked like a verdict on dispatch design was the layer
underneath all of them.

#### What this says about "we don't need to inline by hand, that is what LTO is for"

Three builds of the *unmodified* source, combined binary, cycles per 20M
instructions:

| build | v2 | v4 |
|---|---|---|
| default `-flto=auto` | 1.10G | 1.27G |
| `-flto-partition=one` | 1.10G | 1.27G |
| `--param max-inline-insns-auto=200 --param inline-unit-growth=200` | 0.73G | 0.93G |
| *header `inline` + tick fast path, default LTO* | *0.94G* | *0.85G* |

`-flto-partition=one` changes nothing at all. Whole program, one partition, every
definition visible, and gcc still declines to inline `Memory::read`, so this is
not the visibility problem LTO exists to solve. Raising the inline budget alone
recovers most of the win with no source change, which shows the compiler could
have done it and chose not to.

The reason is that gcc runs two budgets: `max-inline-insns-single` for functions
*declared* `inline`, and a much stingier `max-inline-insns-auto` for everything
else. `Memory::read` has thousands of call sites, so `inline-unit-growth` vetoes
it under the auto budget. Writing `inline` in a header tells the compiler nothing
new about the code; it moves the function into the generous budget.

So the claim survives as a statement about *capability* and fails as one about
*policy*. Nobody has to arrange code for the linker's benefit any more, but
`inline` is still a hint to the cost model, and for a few tiny leaf functions on
the hot path with a thousand callers it is the difference between a call and no
call. The alternative is a whole-program flag, which is a blunt instrument: it
re-ranked every implementation and helped v2 most, where the keyword helped v4
most. Same win, very different blast radius.

#### Why the `tick` fast path did nothing for v1

v1 ticks once per instruction: it decodes into an `Instruction` carrying its own
T-state count and spends it in one go (`pass_time(extra_t_states +
instr.decode_t_states)`), from four call sites in the whole implementation.
v2/v3/v4 tick per bus access, from around forty. Making a per-access function
cheap only helps the implementations that call it per access: v1 gained 2%.

v2 gained nothing either, for a different reason: in the combined binary LTO had
already inlined `tick` for it. In v2's *own* binary it had not, and there the same
change was worth 29%, the largest gain of any implementation. The same edit is
worth 0% or 29% depending on what else is linked beside it.

#### Confirmed on a machine that can actually be measured

All of the above was found on a thermally limited laptop (i7-10510U). Repeating it
on a quiet desktop (i9-9980XE, 18 cores, 24.75MB L3) with the same compiler:

Retired instructions came back **identical to within 0.03%** on every
implementation (v1 +0.02%, v2 -0.03%, v3 -0.01%, v4 -0.02%). That is the
expected result for deterministic work and the same compiler, and it is why the
instruction-count half of this investigation could be trusted from the laptop at
all.

Wall clock became usable for the first time: spreads of 0.5-1.5% against 24-45%
on the laptop. Nanoseconds per emulated Z80 instruction:

| | four in one binary | one binary each |
|---|---|---|
| v1 | 22.09 | 21.71 |
| v2 | 10.21 | **9.84** |
| v3 | 10.61 | 10.38 |
| v4 | **10.05** | 10.42 |

So the inversion is real and reproduces on different hardware: v4 is fastest in
the binary the emulator actually ships, v2 is fastest when each is built alone,
and the difference either way is under 6%. Cycle counts agree with the clock on
this machine, which they did not on the laptop: that disagreement was the
laptop, not the code.

The headline is that the original 20% gap is entirely gone. What is left is a few
per cent that changes sign depending on the link, which is not a number to design
against.

#### Real games, and two traps in measuring them

`z80_bench --snapshot FILE --frames N` runs a `.sna`/`.z80` through the whole
`Spectrum` (ULA, display and all) instead of running zexdoc through the bare
CPU. Milliseconds per emulated frame, 300 frames, on the quiet desktop:

| game | v1 | v2 | v3 | v4 |
|---|---|---|---|---|
| elite | 0.2497 | 0.1247 | 0.1262 | **0.1177** |
| manic miner | 0.2027 | 0.1030 | 0.1093 | **0.1005** |
| atic atac | 0.2324 | **0.1039** | 0.1091 | 0.1134 |
| dizzy 2 | 0.2481 | 0.1067 | 0.1194 | **0.1018** |
| jetpac | 0.2023 | 0.1903 | 0.1519 | **0.0651** |

A real 48K frame is 19.97ms, so all four emulate at 80-300x real speed. v2, v3
and v4 sit within about 10% of each other with the lead changing by game; v1 is
2.0-2.4x behind everywhere. That agrees with what zexdoc says, so as a *ranking*
the exerciser was not misleading.

**Trap one: jetpac is not measuring what the others measure.** It spends most of
each frame halted waiting for the frame interrupt, and the implementations model
`halt` at different granularities:

    v1, v2, v3:  if (halted_) { pass_time(1); return; }
    v4:          if (halted_) { bus(Bus::opcode, pc()); refresh(); return; }

So v1-v3 go round `execute_one` four times per four T-states where v4 goes round
once, and jetpac's 3x is that ratio rather than anything about dispatch. Chronos
behaves the same way. Two of five games sampled, so idling is a real part of
emulator performance rather than an outlier to discard, but it must not be read
as a dispatch result.

There is a correctness difference hiding in the same lines: a halted Z80 keeps
fetching, so R keeps counting. v1-v3 freeze it. A program that reads R for
randomness or timing sees a stopped counter across a HALT.

**Trap two: a snapshot dropped in and run is in an attract loop.** Mispredict
rates are flat from 300 frames to 3000 (6.6% to 6.8% on elite, 2.0% to 2.1% on
manic miner), so ten times the emulated time is the same behaviour repeated.
These numbers describe title screens and demo modes, not play. Getting to
gameplay needs keyboard input driven into `Spectrum::keyboard()`, which has not
been done.

#### What the games say about the dispatch indirect

Mispredicts split with `br_misp_retired.conditional` against
`br_misp_retired.all_branches`: the difference is indirect branches and
returns, and returns are predicted almost perfectly by the return stack, so it is
mostly the dispatch. As a share of cycles at an 18-cycle Skylake penalty, v4:

| workload | non-conditional misp | % of cycles |
|---|---|---|
| zexdoc | 959K | 2.0% |
| manic miner | 167K | 2.1% |
| dizzy 2 | 276K | 3.4% |
| elite | 633K | 6.8% |

**zexdoc understates this by up to 3x.** Its instruction mix runs in tight loops
that the indirect predictor learns; elite's attract mode is a rotating wireframe
with real line drawing and matrix work, and it mispredicts three times as often.
The ordering across games tracks how much the loop actually does, which is what
it should track if the number means anything.

So a threaded interpreter, each handler ending in a `[[gnu::musttail]]` call to
the next rather than returning to a loop, is competing for **2-7% and probably
more in real play**, not the ~3% zexdoc alone suggests. The cost is that handlers
stop returning per instruction, so `execute_one()` becomes a run loop and the
`Spectrum` and scheduler integration changes with it.

#### Attempted, and it is worth more than the estimate

Done, on gcc 16.2. Five interleaved rounds per workload on an idle 36-core
machine, best of three repetitions each, v4 against v4:

| workload | returning | threaded | |
|---|---:|---:|---:|
| manic miner | 0.0891 | **0.0743** | **17% faster** |
| elite | 0.1085 | **0.0924** | **15% faster** |
| zexdoc, driven per instruction | 10.22 ns | **9.41 ns** | 8% faster |

The prediction held in both directions: the games gain about twice what zexdoc
shows, and they gain it in the order the mispredict counts said they would.

**How you drive it decides what you collect.** Threading only the prefix chain,
so a `dd` hands to the next handler but an instruction still returns, was worth
9.6% on zexdoc. Threading whole runs is worth *less* on that same workload, 8%,
because `z80_bench` calls `execute_one()` per instruction to watch for CP/M
calls, so it pays to set a one-instruction budget and collects nothing between
instructions. The games go through `Spectrum::run_cycles`, which hands over a
whole frame, and that is where the 15-17% is. An interpreter that cannot be
given a long run cannot be threaded, whatever the handlers do.

#### What gcc had to say about it

All three of its objections are the same objection: **a tail call abandons the
frame, so nothing the compiler believes lives there may still be addressable.**
`-Werror=maybe-musttail-local-addr` is unusually good about saying so.

- The lambda that forms the indexed address captured `[&]`, which takes the
  address of every local it touches. Explicit captures fixed it.
- The `constexpr` locals had to become `static constexpr`, exactly as `row`
  already was for an unrelated reason.
- **A tail call cannot be made from inside a `template for` at all**, because the
  expansion's own induction variable lives in that frame. This one has no
  workaround and shapes the code: a row that abandons its remaining steps
  `break`s out and hands on at the end rather than handing on where it stopped.

The first two are worth knowing before starting; the third is worth knowing
because it is not obvious that an expansion statement should constrain calling
convention, and it does.

#### The bug that only a long run could show

An untaken conditional used to `return`. Under threading a `return` ends the
*run*, not the row, so the machine stopped at the first `jr nz` that was not
taken. Every unit test passed: they drive `execute_one()`, and with one
instruction budgeted, stopping the row and stopping the run are the same thing.
Booting the ROM found it immediately, at **1558 cycles where 14 million were
due**. A test that runs one instruction cannot distinguish the two, and after
this change they are no longer the same thing.

Worth noting v2 measures the same rate as v4 (6.3% against 6.6% on elite). Both
dispatch through a function-pointer table, so this is a property of the shape
they share, not of v4's generated one.

Both of these numbers are upper bounds twice over: 18 cycles is the textbook
penalty and out-of-order execution hides some of it, and "non-conditional"
includes returns.

#### Reading the benchmark

`z80_bench` holds all four implementations and is what the emulator's link looks
like; `z80_bench_v1` .. `z80_bench_v4` hold one each and are what a comparison
*between* implementations should be read from. `--snapshot` swaps zexdoc for a
game; the games themselves are not in the repository. Two traps, both of which produced
wrong answers before they were fixed: never run it while anything else is on the
machine, and never run the implementations in a fixed order within a repetition,
since whoever goes last meets the hottest core and the coldest caches. The harness
alternates direction and reports the best repetition for that reason.

---

### Compile time

The measurements above are all about how fast the emulator runs. The other half of the trade (how
long v4 takes to *build*, where that time goes, how it scales, and what two different reflection
implementations cost) is in [z80/v4/NOTES.md](z80/v4/NOTES.md) under "Compile time,
measured", because it is a fact about v4 rather than about the emulator.
