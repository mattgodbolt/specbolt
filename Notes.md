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
implementation got faster -- v1 -25%, v2 -18%, v3 -31%, v4 -29% -- and v2, v3 and
v4 now execute within 1% of the same number of x86 instructions per Z80
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
definition visible, and gcc still declines to inline `Memory::read` -- so this is
not the visibility problem LTO exists to solve. Raising the inline budget alone
recovers most of the win with no source change, which shows the compiler could
have done it and chose not to.

The reason is that gcc runs two budgets: `max-inline-insns-single` for functions
*declared* `inline`, and a much stingier `max-inline-insns-auto` for everything
else. `Memory::read` has thousands of call sites, so `inline-unit-growth` vetoes
it under the auto budget. Writing `inline` in a header tells the compiler nothing
new about the code; it moves the function into the generous budget.

So the claim survives as a statement about *capability* and fails as one about
*policy*. Nobody has to arrange code for the linker's benefit any more -- but
`inline` is still a hint to the cost model, and for a few tiny leaf functions on
the hot path with a thousand callers it is the difference between a call and no
call. The alternative is a whole-program flag, which is a blunt instrument: it
re-ranked every implementation and helped v2 most, where the keyword helped v4
most. Same win, very different blast radius.

#### Why the `tick` fast path did nothing for v1

v1 ticks once per instruction -- it decodes into an `Instruction` carrying its own
T-state count and spends it in one go (`pass_time(extra_t_states +
instr.decode_t_states)`), from four call sites in the whole implementation.
v2/v3/v4 tick per bus access, from around forty. Making a per-access function
cheap only helps the implementations that call it per access: v1 gained 2%.

v2 gained nothing either, for a different reason -- in the combined binary LTO had
already inlined `tick` for it. In v2's *own* binary it had not, and there the same
change was worth 29%, the largest gain of any implementation. The same edit is
worth 0% or 29% depending on what else is linked beside it.

#### Confirmed on a machine that can actually be measured

All of the above was found on a thermally limited laptop (i7-10510U). Repeating it
on a quiet desktop (i9-9980XE, 18 cores, 24.75MB L3) with the same compiler:

Retired instructions came back **identical to within 0.03%** on every
implementation -- v1 +0.02%, v2 -0.03%, v3 -0.01%, v4 -0.02%. That is the
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
this machine, which they did not on the laptop -- that disagreement was the
laptop, not the code.

The headline is that the original 20% gap is entirely gone. What is left is a few
per cent that changes sign depending on the link, which is not a number to design
against.

#### Reading the benchmark

`z80_bench` holds all four implementations and is what the emulator's link looks
like; `z80_bench_v1` .. `z80_bench_v4` hold one each and are what a comparison
*between* implementations should be read from. Two traps, both of which produced
wrong answers before they were fixed: never run it while anything else is on the
machine, and never run the implementations in a fixed order within a repetition
-- whoever goes last meets the hottest core and the coldest caches. The harness
alternates direction and reports the best repetition for that reason.
