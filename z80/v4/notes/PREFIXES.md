# Prefixes, views and the shape of a decoding table

How one description covers a prefixed instruction set: tables that transfer to each other, a
table that takes a parameter, and the encoding whose opcode byte is not its last. The longest
single argument in these notes, kept whole because it only makes sense in one piece.

Part of [v4's notes](../NOTES.md).

---

## Prefixes

### Status: table switches, views, indexed addressing and DDCB all work

`table <name>` declares a decoding table, and `goto <table>` is a step. A prefix is an ordinary row:

```
11001011     | (cb)            | goto cb

table cb

01bbb110     | bit {b}, (hl)   | bit8 flags <- (hl) {b} flags ; delay 1
01bbbzzz     | bit {b}, {r:z}  | bit8 flags <- {r:z} {b} flags
```

Decoding starts in the first table declared, so no name is special to the framework. Dispatch,
precedence checking and coverage are all per-table; the `(hl)` override rows inside `cb` are checked
for containment exactly as the ones in `base` are.

`goto` is deliberately *not* a CPU primitive. Fetching the next byte is (`fetch_opcode`), but
choosing a table is the framework's own job, it is the one verb the framework understands.

Scored on `z80/test/OpcodeTests.cpp`:

| suite | passing | undecoded | wrong |
|---|---|---|---|
| unprefixed | 139 / 192 | 53 | 0 |
| cb | 23 / 39 | 16 | 0 |

DD/FD are covered by `ExecuteTest.cpp` instead, against the same expectations.

Every failure in both is an opcode the table does not describe. All three CB timings are right: 8
for register forms, 12 for `bit n,(hl)`, 15 for `res`/`set` on memory.

### What is papered over, deliberately

Audited against every suite in `OpcodeTests.cpp`; no suite reports a genuine mismatch. These are
the things the suites do *not* catch, or catch only because we match an approximation v3 also makes:

- **WZ is not modelled.** `bit {b}, (hl)` names `h` as its bus-noise source, because for that one
  instruction WZ's high byte is HL's. It is right for the tested cases and states the approximation
  where a reader can see it, but it is not WZ. §8 stands. The register form is *not* an
  approximation: flags 3 and 5 genuinely come from the operand, which the row now says.
- **Interrupts are not handled at all.** v3 checks `irq_pending_` at the top of `execute_one`; v4
  does not. Not a papered-over difference so much as a missing feature, but it is missing.
  *(Since fixed: `Z80::handle_interrupt` does all three modes, `ei` defers acceptance by one
  instruction, and `ExecuteTest` covers it. What remains open is that nothing ever releases /INT;
  see the section on it below.)*
- **The immediate is fetched once, before any step**, rather than at the token that names it. Fine
  for every row that exists; wrong for `ld (ix+d), n`.
- **A push writes its two bytes in the wrong order.** `write_memory16` goes low byte first, which is
  what `ld (nn), hl` does; hardware pushes high to sp-1 and then low to sp-2. The bytes end up in the
  same places, so nothing can see it until `Z80::bus` starts contending or something watches writes.
  One function cannot serve both orders, the fix is either a second one, or letting the description
  spell the two writes out, which `push`'s two `dec16 sp` steps already half do.

Found by this audit and fixed rather than recorded: `scf` and `ccf` had `a` as a destination, but
`Alu::scf`/`ccf` return the accumulator unchanged, so the rows claimed a write that never happened.
They now discard, as `cp` does. Harmless in behaviour, wrong as documentation, and the table is
documentation.

### What DD/FD needs, in order

What exists is a table switch, which is what CB and ED need and is the easy case. DD/FD are *views*,
and the groundwork is this, roughly in the order it has to happen:

1. ~~**The execution model, before any syntax.**~~ **Done.** `goto base with view=ix` re-enters the
   table it came from, and the old `check_no_goto_cycles` rejected that. The reason was never that
   template instantiation would fail to terminate, `enter<Table>` was forward-declared and mutual
   instantiation is fine, but that each goto was a real opcode fetch made by a *nested call*, so a
   cycle was unbounded C++ recursion, and `DD DD DD…` is legal Z80.

   A handler now **returns** `Next` (the table to decode the next byte in, or nothing) and
   `execute_instruction` loops on it. Every turn of that loop fetches a byte, so it always advances
   both PC and the clock: a cycle is progress, not recursion, and the check is gone along with its
   diagnostic. The handlers no longer instantiate each other at all, which is why the forward
   declaration went too.

   It costs one branch per instruction and nothing per prefix byte: `cb` compiles to
   `mov $0x101,%eax; ret`, and the two-dimensional dispatch folds into a single scaled load indexed
   by `table << 8 | opcode`.
2. ~~**`goto` learns `with view=`.**~~ **Not needed, the item dissolved.** The sketch below spelled
   the same idea twice: `goto base with view=ix` *and* `table ix = base with hl->ix, …`. Only the
   second is necessary. If a view is a **derived table**, then `goto` never changes: it already takes
   a table name, and `ix` is one. `Next` stays a table index rather than becoming a (table, view)
   pair, and the state count stops being a product.

   It also gets `ed` right for free. `ED` discards a pending `DD`, which the first spelling needed an
   explicit `with view=hl` to say. Under derived tables the inherited row is `goto ed`, a goto names
   a table and a substitution rewrites *members*, not table names, so decoding lands in plain `ed`
   with no rule to write.

   What exists now:
   - `table ix = base with hl->ix, h->ixh, l->ixl`, either spacing round the arrow. The right-hand
     side is parsed as a full vocabulary member, so a substitute brings its own primitive and its own
     `/delay=`.
   - `decode_tables` gives a derived table its parent's rows for every opcode it does not claim
     itself, so an override row is just a row and first-match-wins does the rest. A parent must be
     declared above its children, which makes the derivation a forest and lets declaration order
     resolve a chain.
   - `member_of` applies the renaming, and it is still the only place a reference is followed. That
     is the whole of the mechanism: **a view is a function from member to member, applied at the one
     point a `{field}` is resolved.** Literal text is untouched by construction, which is the rule
     stated below, now enforced by there being nowhere else for a rule to act.
   - A derived table with no rows of its own is legal (it *is* its parent, renamed) so
     `check_tables_used` no longer demands rows of one.

   Not yet wired into `z80.cpu`: `(hl)` must become `(ix+d)`, which fetches a displacement byte, and
   that is item 4. Adding `dd` before then would decode `inc (hl)` as `inc (hl)` under DD, a
   knowingly wrong emulator, so the mechanism is tested on its own description in `DiagnosticsTest`
   instead, including that `dd dd` re-enters.
3. ~~**References before views.**~~ **Done.** `Operand` and `Piece` each spelled a reference as two
   loose indices, and four places spelled out the lookup that follows one. Both now hold a
   `Reference`, and `member_of` is the only place one is followed, which is the place a view will
   have to intercept.
4. ~~**Per-token fetching, and the latch it needs.**~~ **Done**, and it needed no multi-token
   encodings after all. See below.

### The displacement is derived, not declared

`(hl)` becomes `(ix+d)` under a view, and that costs a byte the row never mentioned. Who says so?

**floooh's chips says so by hand.** Its description carries `flags: { indirect: true }` on every row
that touches `(HL)`, and the generator turns those into a 256-entry `_z80_indirect_table[]` that the
shared DD/FD fetch consults:

```c
case Z80_DDFD_M1_T4:
    cpu->addr = cpu->hlx[cpu->hlx_idx].hl;
    _goto(_z80_indirect_table[cpu->opcode] ? Z80_DDFD_D_T1 : cpu->opcode);
```

That flag exists because his description does not resolve operands ahead of time, the generator
cannot tell that `INC (HL)` touches memory except by being told, on about forty rows, correctly,
for ever. **We already resolve every operand at compile time, so the same fact is derivable**:
`displaced_through` asks what the operands resolve to under this table's rules and returns the one
they are displaced through, or nothing. No annotation, no table, nothing to forget. The flag floooh
must write is a `consteval` question for us.

(His view is the runtime twin of ours: `hlx[3]` indexed by `hlx_idx`, so every generated case reads
`cpu->hlx[cpu->hlx_idx].h`. Same idea, opposite trade, he pays an indexed load on every H access
for ever, we pay compile time and table count.)

Three things the exploration got wrong first, each caught by a test rather than by reasoning:

- **The address must be formed once per instruction, not once per operand.** `inc (ix+d)` reads and
  writes through the same address; forming it per operand paid for the window twice. So `execute_one`
  forms it up front and hands it to every operand that shares it, which is the "latch", arriving for
  a reason that has nothing to do with DDCB. A row may only be displaced through one base, and that
  is checked.
- **The window absorbs the immediate.** `ld (ix+d), n` is 19 T-states, not 22: the `n` is read
  *inside* the five-T-state window that forms the address, not before it. So the machine is told how
  many bytes were already read. floooh handles this with `if (cpu->opcode == 0x36)`; deriving it
  needs no special case.
- **A rule names the vocabulary it rewrites.** It did not always: rules once matched on a member's
  text alone, so `h -> ixh` reached the `{real:y}` in `ld {real:y}, (ix+d)` and wrote IXH instead of
  H, exactly the bug that row exists to avoid. A `Rule` now carries a vocabulary index and
  `member_of` compares it, so `reg.h -> ixh` leaves `real.h` alone. Rules still apply to every row
  the table decodes, its own as well as inherited ones.

**The customisation point is one function.** Not a DSL attribute, and not framework arithmetic:

```cpp
[[nodiscard]] std::uint16_t displaced_address(Cpu &, std::uint16_t base, std::uint8_t offset,
    std::uint8_t immediate_bytes);
```

It owns both how a base and an offset combine *and* what forming the address costs, because both are
facts about the machine, a 6502 wraps within page zero for one mode and charges for a page crossing
in another. Taking `Cpu &` is what lets the cost live there. Everything else the table already said.

Verified in `ExecuteTest.cpp` against the counts `OpcodeTests.cpp` asserts of v1/v2/v3: 19 for
`ld r,(ix+d)`, `ld (ix+d),r`, `ld (ix+d),n` and `add a,(ix+d)`; 23 for `inc (ix+d)`; 8 for a DD that
renames nothing; and `dd dd dd 23` at 4 T-states a prefix byte. Generated code forms the address once
with `add`, reuses it for the read and the write, and folds both idles into constant clones.

**The disassembler followed.** A member's text is now lowered into `Piece`s at parse time exactly as
a row's mnemonic is (`pieces_of` does both) with `Piece::Kind::Displacement` for the hole `+d`
leaves. A row renders its pieces, and a member renders its own, so `inc (ix-0x01)` falls out without
the disassembler parsing anything at runtime. The displacement is taken before the pieces are walked,
because it precedes any immediate, which also makes the reported length right.
5. ~~**Capacity.**~~ **Checked; nothing to change.** `Field::max_values` is 8 and the `ix` view's
   register vocabulary is exactly 8 (`b c d e ixh ixl (ix+d) a`), it fits, with no headroom.
   `Rules` holds 6 and `ix` needs 4. `Row::max_steps` is 6, which DDCB might exceed, but bumping a
   limit before something reaches it is guessing.

   What was actually missing was any evidence about the edge, so every fixed capacity now has a case
   in `DiagnosticsTest`: steps, operands, destinations, mnemonic pieces, substitutions. All five
   report the table's limit rather than corrupting quietly, so raising one when DDCB needs it is a
   one-line change made in response to a message rather than to a guess.

Nothing in 1–4 is a syntax question. The table language for prefixes is already written down below;
what is missing is the machinery underneath it.

Still untested for real: that a long prefix chain does not grow the stack. `DiagnosticsTest` asserts
only that a self-goto *parses*, because no row uses one yet. The test to write alongside DD is
`dd dd dd … 00`: constant stack, 4T a byte, `r` incremented once a byte.

### What DD actually does, measured

Diffing `execute_one_dd` against `execute_one_base` in v3's generated code, case by case:

| | count |
|---|---|
| byte-identical, DD is a pure no-op | **169 / 256 (66%)** |
| differ | **87 / 256 (34%)** |

Of the 87, **86 are register renames and exactly one is a table switch** (`CB`). So "DD makes every
opcode different" is emphatically false, two thirds are untouched.

The shape of the affected set decides the design. They are vocabulary slices, `r ∈ {4,5,6}`,
`rp[2]`, `rp2[2]`, and the standalone index register, and **`{4,5,6}` is not maskable**. It is not a
bit-pattern family. `01yyyzzz` covers 64 opcodes of which 38 are affected and 26 are not, with no bit
pattern separating them.

### The model: `goto <table>`, where a view is a derived table

No new primitive. Prefix rows are ordinary rows, and a view is a table declaration.

```
table base
  11001011 | (cb) | goto cb
  11101101 | (ed) | goto ed     # lands in plain ed: a rule renames members, not tables
  11011101 | (dd) | goto ix     # re-enter the same map, renamed
  11111101 | (fd) | goto iy

table ix = base with hl->ix, h->ixh, l->ixl, (hl)->(ix+d)
  11001011 | (dd cb)          | goto ddcb          # override: a jump, not a rename
  01yyy110 | ld {r8:y}, (i+d) | ld_from_indexed    # override: half-registers stay real
  01110zzz | ld (i+d), {r8:z} | ld_to_indexed
```

An earlier sketch also gave `goto` a `with view=` clause. That was the same idea written twice; the
declaration form is enough, and keeping only it means the decode state stays a single table index.

A derived table may carry **override rows** that shadow the derived ones by first-match-wins, which
§5 already requires anyway, so overrides cost no new mechanism. That is what removes the two
special-purpose mechanisms an earlier sketch needed: no `view=` guard for routing DD CB, and no
`view-rule` for half-register suppression. Both become rows a reader can see.

Stating DD as "re-enter the table you were already in" is also more honest than "set a mode": it
makes clear a full opcode fetch follows, with its 4 T-states and R increment.

State is one table index, seven of them, exactly v2's seven tables. Prefix chains (`DD DD FD`) fall
out: `ix` inherits base's `goto iy` row, so each byte just re-enters, last wins.

**One rule to keep: only `{field}` references are rewritten; literal text never is.** `ex de, hl`
written literally is therefore immune by construction. Substitution-by-default would reproduce the
exact bug v2 and v3 both have. This is now structural rather than a rule to remember: `member_of` is
the only place a rule is consulted, and it is only reachable through a `{field}`.

Naming a different vocabulary *is* enough, now that a rule carries the vocabulary it rewrites; it
was not when rules matched on member text alone, and `ExecuteTest` caught the difference. What stays
exempt is only the diagnostic: `check_inherited_literals` does not ask an override row to spell out
what its parent spelled literally. The rows above name `{real:y}` rather than `{reg:y}` for a second
reason as well: `real` has a hole where `reg` has `(hl)`, so `01yyy110` does not claim `0x76` and
`dd 76` stays `halt`.

### How DDCB was actually done

`d` in the encoding column, which was reserved for this from the start, the old diagnostic already
said `'d' is not an encoding byte`. A row that reads one does not use it; it hands it to the table it
goes to:

```
table ix = base with hl -> ix, …
  11001011 d | (dd cb) | goto ddcb
```

Everything else is derived from that one token:

- **A table is *latched* if the gotos reaching it read a displacement.** Its rows use the incoming
  value instead of reading their own, and it is an error for a table to be reached both ways. Nothing
  is declared on the table itself.
- **A latched table's opcode arrives as an operand read**, not an instruction fetch, three cycles
  and no refresh, which is exactly what the real chip does for that byte and why `R` does not
  increment for it. The loop picks `fetch_immediate` over `fetch_opcode` on that one bit.
- **The address window absorbs it.** The five-T-state window that forms `ix+d` contains the opcode
  read here, just as it contains the immediate in `ld (ix+d), n`: `5 - 3×1 = 2` idle cycles.
  `displaced_address` needed no change at all, only a truthful count of what was read inside it.

So the latch is `Next` carrying a byte, and it costs nothing: the displacement arrives in a register
parameter. `set 4, (ix+d), b` compiles to sign-extend, `idle`, `add`, `read`, `or $0x10`, `idle`,
`write`, `mov`, the primitive inlined and the undocumented copy a single store.

Two small capabilities came with it, both general rather than DDCB-shaped: an operand written out in
a row may carry `/delay=1` exactly as a vocabulary member can, and one result may name more than one
destination. The second *is* the undocumented copy.

Timings verified against `OpcodeTests.cpp`: 20 for `bit n,(ix+d)`, 23 for `set`/`res`. Flags 3 and 5
come from `wzh`, the high byte of the address the machine last formed, which is a named location now
rather than a papered-over `h`.

Still missing from these tables: the rotate family, because `cb` does not have it either.

### DDCB is different in kind, and substitution provably cannot express it

CB and ED are pure table switches. DD and FD are re-readings of the same map. **DDCB is both, plus a
fetch reordering**, the only encoding in the instruction set where the opcode byte is not the last
byte read. It gets its own four-row table, and that is not a taste judgement:

**v2 and v3 both build DDCB by substitution, and are wrong for 224 of 256 entries.** The generated
code fetches the displacement, then operates on the named register and never touches memory:

```
// Z80Generated.cpp:7454   case 0x00: // rlc b
      const auto lhs = get(R8::B);   // reads the real B; (ix+d) ignored
```

Real hardware: `DD CB d 00` is `rlc (ix+d)` with the result *also* copied into B. Correct only for
the 32 entries where `z == 6`. v1 gets the addressing mode right (`Decoder.cpp:158`) and omits only
the undocumented register copy.

So the repo contains a working implementation of "DD is a view over the CB table", and it is wrong
224 times. Write down *why* `ddcb` is separate, or someone will fold it back into `cb`.

The undocumented register copy is a second destination, and `z == 6` is a separate row reached by
first-match-wins rather than a "no destination" member, `s` already has a hole at 6, so `10bbbzzz`
does not claim it and `10bbb110` does. No new vocabulary syntax was needed.

**A second instance of the same class arrived while this was being written.** #39, fixed in #43:
v2 and v3 both disassembled and executed `DD EB` as `ex de, ix`, because their prefix handling
carries an index-register choice that every `hl` is read through. Real hardware ignores DD and FD
here, `EX DE,HL` is `EX DE,HL` under any prefix.

v4 was right without anyone deciding it should be. A view renames *vocabulary members*, `pair.hl`, `spair.hl`, `reg.h`, `reg.l`, `reg.(hl)`, and `ex de, hl` names `hl` as literal text, so
there is nothing for the rename to reach. Every base row that writes `hl` literally and *does* want
the index register under a prefix (`add hl, rr`, `ld (nn), hl`, `ld hl, (nn)`, `jp (hl)`,
`ld sp, hl`, `ex (sp), hl`) carries an explicit override row in `indexed`, which is the same fact
seen from the other side: substitution here is opt-in, and the default is to leave the instruction
alone.

Which is the argument for the whole design, in one opcode, though a smaller argument than it first
looks. Both schemes can be wrong; what differs is which way they fail when nobody is paying
attention. v2 and v3 substitute by default and must remember to stop, so a forgotten exception is a
*changed* instruction. v4 substitutes only where asked, so a forgotten exception is an *unchanged*
one. Neither is caught by the compiler. But the DD/FD prefix leaves most of the map alone, so the
lazier default is also the more often correct one, and the exceptions are nine rows one can read.

`ddcb` and `fdcb` are written out twice rather than one derived from the other, because they differ
only in a *literal* operand and a rule rewrites only `{field}` references. That is the same rule that
keeps `ex de, hl` safe, so the duplication is the price of it; worth revisiting together when
`ex de,hl`, `jp (hl)` and `ld sp,hl` land, since those are the rows that decide whether literals
should ever be rewritten.

### Pure goto is refuted by v1

Giving DD its own full table is the obvious alternative and the repo already tried it.
`v1/Decoder.cpp:191` is a whitelist override table covering **165 of 256**; the other **91 opcodes
return `??`**, including `DD 00`. The failure mode is invisible, nothing in the file says a row is
missing. Rows would go from ~87 to ~177, with 94 duplicated, and every semantic fix would need making
in three places.

### Row counts for a complete Z80

| | rows | duplicated |
|---|---|---|
| **derived tables (this design)** | **~87** | ~8 |
| DD/FD as full tables | ~177 | 94 |
| 256 longhand per state | 1792 | ~1450 |

Base map is 47 rows by bit-pattern family, `cb` 4, `ed` ~24, `ddcb` 4, plus vocabularies and headers.
Cross-checks against v3's own family count (49 `return` sites in `match_op`). Call it ~90 rows,
110–120 if you decline the tighter vocabularies. It is not copy-paste.

The real duplication, being specific: `rot`/`bit`/`res`/`set` written twice (once for `cb`, once for
`ddcb`, because the semantics genuinely differ); `ld (nn),hl` and `ld hl,(nn)` existing in both base
and ED because the chip really does encode them twice; `inc`/`dec` pairs; `view ix`/`view iy`.

### Loop, not recursion

v2 and v3 recurse on prefixes. A loop with an explicit state, where a handler returns the next state,
means `DD DD DD…` cannot blow the C++ stack, and "are we mid-prefix?" becomes an inspectable value.
That matters because **none of v1/v2/v3 model interrupt acceptance around prefixes**, all three
sample `irq_pending_` once at the top and swallow the whole chain. With recursion it is not even
expressible.

v4 does this now: `execute_instruction` is the loop and `Next` is the state. The interrupt half is
still not done, but it has somewhere to go, the top of the loop is exactly the point the Z80 will
not accept an interrupt at, because a prefix and its opcode are one instruction. Knowing that
requires the state to be a value, which it now is.

### Costs, measured

7 states × 256 = 1792 instantiations, of which **338 are byte-identical duplicates** (169 per index
register). Aliasing those, plus folding `fdcb` into `ddcb` with the index register as a runtime
value, leaves ~1200.

Dispatch machinery floor on gcc 16.2 (`-O1 -freflection`, trivial bodies): 1×256 = 1.59 s / 114 MB;
3×256 = 3.10 s / 154 MB; 7×256 = 4.80 s / 233 MB, roughly **0.53 s and 20 MB per additional
256-entry table**, linear in states. With real step bodies, ~+1 s and +22 MB per 256; 7×256
extrapolates to about **12 s / 390 MB**.

---

### The parser reads whatever it is handed

The parse functions used to read the `#embed`ed description directly, which meant the only way to
see what a malformed table said was to damage the real one, done by hand four times before it was
obvious that it was a smell rather than a technique.

They now take the description, the vocabularies and the table names as parameters, and are
`constexpr` rather than `consteval`, so they run at runtime too. The three names that reach for the
embedded file are declared *below* every function that parses, so a parser cannot reach the
production instance even by accident; `-Wshadow` enforces it, which is how the last two were found.

`DiagnosticsTest.cpp` drives the whole pipeline with its own raw-string tables and asserts the exact
message and line for every diagnostic a malformed table can produce. Writing it immediately found a
dead one: `is_table` matched `"table "` with a space, so a bare `table` line was silently ignored
rather than reporting that it has no name.

The same change is what a second CPU needs, since nothing about the parser now says `z80.cpu` except
the one line that embeds it.
