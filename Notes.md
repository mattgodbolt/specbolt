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

`in` and `out` need to poke about with other information, and stuff like `exx` needs to switch all the registers, as does
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
