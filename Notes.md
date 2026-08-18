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

### The two functions every implementation waits on

The zexdoc times used to look like a statement about how each implementation
dispatches. Some of what they were saying was really about two small functions
underneath all of them, neither of which has anything to do with dispatch:
`Memory::read` and `Scheduler::tick`. Both were being left out of line, and what
that cost depended on what else happened to be in the binary.

`z80/test/Bench.cpp` is the harness. `z80_bench` holds every implementation and
is what the emulator's own link looks like; `z80_bench_v1` .. `z80_bench_v3`
hold one each, and are what a comparison *between* implementations should be
read from.

**Wall clock on a laptop cannot see an effect this size.** The same binary varies
by tens of per cent between runs. Everything below is `perf` retired
instructions, which reproduce to better than 0.01% for the same binary on the
same input. Cycles moved the same way, but with a couple of per cent of noise on
top, so they are the column to trust least.

Retired x86 instructions per emulated Z80 instruction, over 20M instructions of
zexdoc, gcc 16.2 at `RelWithDebInfo`:

| all three in one binary | before | `Memory` inline | `tick` fast path | both |
|---|---:|---:|---:|---:|
| v1 | 360.9 | 329.7 | 348.4 | **302.2** |
| v2 | 182.2 | 162.8 | 167.3 | **146.6** |
| v3 | 191.1 | 170.7 | 167.0 | **146.2** |

| one binary each | before | `Memory` inline | `tick` fast path | both |
|---|---:|---:|---:|---:|
| v1 | 377.0 | 307.2 | 362.1 | **292.2** |
| v2 | 202.9 | 200.1 | 132.9 | **125.9** |
| v3 | 210.4 | 207.1 | 130.5 | **123.4** |

**The same edit is worth 1% or 34% depending on what else is linked beside it.**
In its own binary v2 gets almost nothing from inlining `Memory::read` (-1.4%)
and almost all of its win from the `tick` fast path (-34.5%); in the combined
binary the two contribute about equally. Same source, same compiler, same input,
and only the link differs. Interprocedural optimisation is on for the whole
build, so whichever function loses the inlining lottery decides where the time
goes, and what it is competing against changes when the unit changes size. This
is also why editing one implementation can move another's instruction count
without its source changing.

**v1 is the mirror image.** It gains from `Memory::read` (-18.5% in its own
binary) and next to nothing from `tick` (-3.9%), because it ticks once per
instruction: it decodes into an `Instruction` carrying its own T-state count and
spends it in one go, from four call sites in the whole implementation. v2 and v3
tick per bus access, from forty-odd. Making a per-access function cheap only
helps the implementations that call it per access.

**What is left is a much smaller spread.** After both changes v2 and v3 execute
within 0.3% of the same number of x86 instructions per Z80 instruction in the
combined binary, having been 5% apart before. Some of what looked like a verdict
on how each one dispatches was the layer underneath all of them.

#### What this says about "we do not need to inline by hand, that is what LTO is for"

Writing `inline` in a header tells the compiler nothing it could not already
work out: with LTO on, every definition is visible at link time whichever file
it sits in. What it changes is which budget gcc spends.
`max-inline-insns-single` applies to functions *declared* `inline`, and a much
stingier `max-inline-insns-auto` applies to everything else. `Memory::read` has
thousands of call sites, so `inline-unit-growth` vetoes it under the auto
budget.

So the claim survives as a statement about *capability* and fails as one about
*policy*. Nobody has to arrange code for the linker's benefit any more, but
`inline` is still a hint to the cost model, and for a couple of tiny leaf
functions on the hot path with a thousand callers it is the difference between a
call and no call.

#### Reading the benchmark

`--snapshot FILE --frames N` runs a `.sna`/`.z80` through the whole `Spectrum`
(ULA, display and all) instead of running zexdoc through the bare CPU; the games
themselves are not in the repository. Two traps, both of which produced wrong
answers before they were fixed: never run it while anything else is on the
machine, and never run the implementations in a fixed order within a repetition,
since whoever goes last meets the hottest core and the coldest caches. The
harness alternates direction and reports the best repetition for that reason.
