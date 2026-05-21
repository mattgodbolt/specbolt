### v4: reflection-driven dispatch (2026-05)

A fourth Z80 implementation built around Barry Revzin's clang fork, intended as
keynote material. Lives under `z80/v4/`, gated by `SPECBOLT_V4=ON`. CI doesn't
exercise it (apt-clang-20 doesn't accept `-freflection-latest`).

**The reframing.** Instead of writing per-instruction code (v1's switch),
per-instruction templates (v2's `requires`-constrained specialisations), or
per-instruction generated text (v3's `MakeZ80.cpp`), v4 treats the instruction
set as **data**: a `constexpr` array of `{opcode, mnemonic, body}` records
where `body` is a `std::meta::token_sequence`. A class-scope `consteval {}`
block walks the array, builds a `list_builder` of switch arms, and
`queue_injection`s a `dispatch_base()` member.

**Why a token_sequence, not a lambda.** The obvious alternative —
`InsnDef{0, "nop", [](Z80&){ ... }}` — collides with the type system:

- `std::function<void(Z80&)>` makes the field type uniform (one `std::array`),
  but every call goes through type-erased indirection. No inlining.
- `template<class F> struct InsnDef` keeps inlining but each entry is its own
  type, so they don't fit in a `std::array`. v2 effectively pays this cost.
- `void (*)(Z80&)` is uniform and direct-call, but turns every opcode into an
  indirect call. v2's `execute_ptr_t` table does this; LTO can sometimes see
  through it but the compiler has been *asked* to defeat the obvious.

A `token_sequence` is a value of *syntax*, not a callable. At injection time
the body tokens splice literally into the synthesised switch arm — the
compiler sees a plain switch with inline bodies and produces an ordinary jump
table. There's nothing for LTO to undo.

**The real win: parameterised shapes.** A single `LdRR::expand()` covers all
63 `ld r, r'` opcodes from one nested loop over (dest, src) — splicing the
right `R8::B`/`R8::C`/... identifier into each body via
`\(std::meta::id(...))`. The DD/FD-prefix variants reuse the same generator
with an `IX`/`IY` register-set parameter. v2 needs `template<HlSet>` plus three
`IndexReg` specialisations for the same effect; v3 emits the cross-product as
strings in `MakeZ80.cpp`. Shapes worth tackling:

- `LdRR` (63 opcodes from `ld r, r'`)
- `AluA` (8 ops × 8 sources = 64 from `add/adc/sub/sbc/and/xor/or/cp a, r`)
- CB rotates/shifts (8 × 8 = 64)
- CB `bit`/`res`/`set` (3 × 8 × 8 = 192 — one shape, three axes)
- DD/FD as a parameterisation on the above

**Things to remember for fairness when comparing to v2/v3.** C++26 brings
several things constexpr that previously weren't. If `std::format` becomes
`consteval`-callable, v2's `Op<"mnemonic", lambda>` can build mnemonics from
parameters too; if not, both v4 and v2 have to fall back to hand-rolled
string concat or `std::meta::static_array_of` tricks. Keep the comparison
honest — same C++26 baseline for all four versions.

**Bonus for slides.** `std::meta::report_tokens("label", ts)` makes the
compiler print the synthesised token sequence back as a diagnostic at the
point of injection. Useful for "look — here's the table, here's the switch
the compiler built from it" shots.

---

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
