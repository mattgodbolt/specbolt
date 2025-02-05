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
