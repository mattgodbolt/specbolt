# The `.cpu` format

A reference for the instruction-set description that v4 compiles. The file this
describes is [`z80.cpu`](z80.cpu). The code that reads it is the `refract`
library, in `refract/include/refract/`:

- `Lexical.hpp` parses a fragment of text at a time, and `Parse.hpp` the
  declarations;
- `Decode.hpp` works out which opcodes each row claims;
- `Checks.hpp` holds the rules a whole description must obey;
- `Compiled.hpp` turns a description into constants and runs those checks;
- `Machine.hpp` states what a machine must provide;
- `Execute.hpp` generates the interpreter, and `Disassemble.hpp` the
  disassembler.

`Target.hpp` embeds `z80.cpu` and names the machine and its palettes of
operations, and `Disassembler.cpp` tells the library's disassembler where the
bytes come from; those two belong to the Z80 rather than to the library. For
where to start reading, see [README.md](README.md); for why the format is shaped
this way rather than some other way, [NOTES.md](NOTES.md).

To edit one in VS Code, symlink [`tools/vscode-cpu`](../../tools/vscode-cpu)
into the extensions directory and reload the window:

```sh
ln -s "$PWD/tools/vscode-cpu" ~/.vscode/extensions/cpu-instruction-table
```

Over a remote connection a grammar still runs locally, so it has to be installed
on the local machine, from a checkout there or as a package; the extension's
README says how.

---

## Overview

A `.cpu` file describes an instruction set as data. It is `#embed`ed into the
build, parsed during constant evaluation, and used to generate two things: a
disassembler and an interpreter. Nothing in it is read at run time. By the time
the program starts, the file has become code.

Examples are drawn from `z80.cpu`, the description v4 runs, except where one is
marked as the 6502's (`refract/test/m6502/6502.cpu`), as a counter-example, or
as a spelling `z80.cpu` does not use. Where a passage explains *why* a feature exists it usually cites the
Z80, but the feature itself is stated generally and the paragraph will say which
is which.

The file has three kinds of line. Vocabularies and tables are collected in
whole-file passes before any row is read, so a row may name either before its
declaration appears; `z80.cpu` does exactly that, jumping to a table declared
further down. What order *does* decide is which table a row lands in (a row
belongs to the nearest `table` line above it, and a row above the first one is
an error) and, within a table, which of two overlapping rows wins.

- **`vocab`** declares a *vocabulary*: the list of things a group of opcode bits
  can select between.
- **`table`** declares a *decoding table*: 256 opcodes' worth of rows. A table
  may be *derived* from another, re-reading its rows with some vocabulary
  members renamed.
- **rows** describe instructions. Every row has three columns separated by `|`.

### How general is it

Nothing in the format names the Z80, and the framework knows no Z80
instruction. But the format has been written against one processor, and it
shows in these places:

- **An addressing mode cannot fetch its own operand.** Only the encoding column
  fetches, so a vocabulary member may neither render nor pass an immediate (see
  [Members](#members)). A 6502's `bbb` field, whose addressing modes fetch zero,
  one or two bytes, therefore cannot be one vocabulary, and each operation
  group needs a row per mode.
- **A displacement is one signed byte.** It is what the encoding's `d` fetches
  and what `+d` renders, and a machine cannot say otherwise (see
  [Displacement](#displacement)). An index register scaled by a word, or an
  unsigned offset, cannot be described.
- **An opcode is eight bits** (see [Limits](#limits)).
- **A sixteen-bit immediate is little-endian.** The framework fetches its two
  bytes low first and the disassembler reads them the same way, so a machine
  with big-endian immediates cannot be described.

A 6502 description, refract's second machine (`refract/test/m6502/`), got
further than that list suggests: `LDA ($20),Y` and `LDA $1234,X` can be written
today, as steps through a location standing for the chip's address latch rather
than as addressing modes. [NOTES.md](NOTES.md#what-a-second-cpu-would-need) has
what it found and the design it points to. Treat "not Z80-specific" as a design
intent tested against two processors, one of them only as far as smoke tests,
not as a promise.

### The three columns

```
00yyy100     | inc {reg:y}     | inc8 {reg:y}, flags <- {reg:y} flags
^^^^^^^^^^^^   ^^^^^^^^^^^^^^^   ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
encoding       mnemonic          action
```

- The **encoding** says which opcodes the row matches and what bytes follow it.
- The **mnemonic** says how to print it.
- The **action** says what it does, as an ordered list of steps.

The three are cross-checked against each other at compile time: if the mnemonic
renders a different number of immediate bytes than the encoding fetches, or the
action uses an immediate the encoding never read, the build fails with the line
number.

That check covers *immediates*, the `n` bytes. It is not a check on total
instruction length, because a displacement is not declared by the encoding at
all (see [Displacement](#displacement)): under a view, an inherited row can
match a longer instruction than its own encoding column describes.

### The guiding rule

**The table names things; it does not define them.** Every operation it names
(`inc8`, `add16`, `is_set`) and every location it names (`a`, `hl`, `carry`,
`pc`) is looked up by reflection in the CPU description. A name the CPU does not
supply is a compile error naming the line that asked for it.

### What the CPU description must supply

This document describes the table; the other half of the contract is the CPU
description, and a `.cpu` file means nothing without it. That half says what a
row's operations mean, what its names mean, and how the framework drives the
chip. `Machine.hpp` states the last of these as a concept. Locations are
marked, as the machine's operations are: a location is an enumerator of an enum
taken by a member marked `[[=refract::location.reads]]` or
`[[=refract::location.writes]]`, and the generator reads and writes it by calling
those members, whatever they are called. For the Z80 the palettes
are `Operations.hpp` and the shared `Alu`, and the machine itself, in `Z80.hpp`,
supplies the rest. Be warned that the operations are not a small file: the easy
majority of an instruction set becomes rows, and what stays behind is the
awkward remainder: the block moves, the exchanges, the flag minutiae.

| the table writes | the CPU supplies |
|---|---|
| an **operation**: `inc8`, `add16` | a static function of that name in a palette the target lists, or a member the machine publishes with `[[=refract::operation]]` |
| a **location**: `a`, `hl`, `pc` | an enumerator of that name, in an enum a public member marked `[[=refract::location.reads]]` takes, if a row reads it, and one marked `[[=refract::location.writes]]` takes, if a row writes it; either mark is what makes it a location, and a read and a write of one enum agree on its type |
| a **value** an operation takes as an enum: `left`, `i` | an enumerator of the parameter's enum, under its [spelling](#spellings) |
| a **view reference**: `{index:view}` | nothing of its own: every member is a location, and the view picks between them |
| an **indirect operand**: `(hl)` | `read_memory` / `write_memory`, and `read_memory16` / `write_memory16` |
| an **immediate**: `n` | `fetch_immediate`, once per byte, low byte first |
| any **opcode fetch** | `fetch_opcode`, except a latched table's, which is `fetch_immediate` |
| the start of every instruction | `start_instruction`, which answers whether to run another; where a machine takes an interrupt or idles a halt |
| `delay`, and any `/delay=` | `delay`, published as an operation too if rows write `delay` steps |
| a **displacement**: `(ix+d)` | `displaced_address`, given the offset as a signed byte and told how many bytes were read after it, and `displacement_window_bytes`; only a machine with displaced rows needs either |

An operation's signature is the interface:

- an operation that needs machine state, or needs to charge time, is a member
  of the machine, marked `[[=refract::operation]]`. It reaches the
  machine as `this`, and one that only reads it is `const`; the row does not
  mention the machine either way, and an operation that asks for the machine as
  a parameter is refused.
- its remaining parameters are filled from the row's operands, **positionally**,
  or by name if the row writes them that way (see
  [keyword operands](#keyword-operands)). The parameter's type is what decides
  how wide an access is, whether a constant fits, and whether a name is a
  location or a [value](#spellings).
- its **return type decides destinations**, as described under
  [the action column](#the-action-column), and whether it is a condition: one
  that returns `refract::Continue` decides whether the rest of its row runs,
  and must not reach the machine (be a non-static member of it, or take it), so
  that the row states everything the branch depends on. `refract::continue_if(bool)` makes one.

Everything the format leaves unsaid is settled there: how wide a location is,
what endianness a 16-bit memory access uses, what a "cycle" counts in, and how
an addressing mode is formed and paid for.

### Spellings

A name in a row is a location, unless the parameter it reaches is an enum. Then
it is a *value*: an enumerator of that parameter's enum, looked up in that enum
alone, so it may share a spelling with a register and mean something else
entirely. `rl:rotate8(left,carry)` hands `left` to a parameter of type
`Alu::Direction`; `vocab dir : BlockDirection = i d` names two directions, not
the I and D registers.

An enumerator is matched by its identifier, ignoring case, unless it declares a
spelling of its own with a C++26 annotation:

```cpp
enum class BlockDirection : std::uint8_t {
  Up [[=refract::Spelling{"i"}]],
  Down [[=refract::Spelling{"d"}]],
};
```

Annotate only where the assembly spelling and the C++ identifier really differ:
`Up` is written `i` because Z80 assembly says so, while `Alu::Direction::Left`
is written `left` and needs nothing. Location lookup honours a spelling too;
without a scope to search, it consults spellings only when no identifier matches.

A number passed to an enum parameter is an error naming the line, whether the
row wrote it or the opcode carries it: the enum has names for its values, so a
row must use one.

---

## Lexical structure

A line ending in `\` continues onto the next, and the two are read as one. The
join happens before anything below: a continued line is one line, and it is
reported at the number it *started* on. Nothing else changes, so wrapping a
declaration is only ever a matter of taste.

Lines are trimmed of leading and trailing spaces and tabs, and of a trailing
carriage return. After trimming:

| line | meaning |
|---|---|
| empty | ignored |
| begins with `#` | a comment, ignored |
| begins with the word `vocab` | a vocabulary declaration |
| begins with the word `table` | a table declaration |
| contains `\|` | a row |
| ends with `\` | joined to the line below, and read as one |
| anything else | a compile error |

> **Comments must be on their own line.** `#` is only special at the start of a
> line, so a trailing comment on a row becomes part of the action column and
> will fail to parse as one, and on a `vocab` line it becomes members.

> **Every line must mean something.** A line that is none of the above is a
> mistyped one of them, so it is rejected rather than skipped: *this is not a
> comment, a declaration, or a row; a row needs its '|' separators*. A row that
> loses its separators is the case this is for.

---

## Grammar

```ebnf
file            = { line } ;
line            = comment | vocab-decl | table-decl | row | empty ;
(* a line ending in "\" is joined to the next before any of the above *)
comment         = "#" , { any } ;

vocab-decl      = "vocab" , vocab-name , [ ":" , scope-name ] , "=" , member , { member } ;
member          = hole
                | member-operand , [ "/" , attribute ]
                | member-operand , ":" , identifier , [ arguments ] ;
member-operand  = number | name | "(" , ( number | name ) , [ "+d" ] , ")" ;
arguments       = "(" , argument , { "," , argument } , ")" ;
argument        = [ identifier , "=" ] , member-operand ;
hole            = "-" ;

table-decl      = "table" , table-name , [ view-decl ] ,
                    [ "=" , table-name , "with" , rules ] ;
view-decl       = "(" , view-name , ":" , vocab-name , ")" ;
rules           = rule , { "," , rule } ;
rule            = vocab-name , "." , display-text , "->" , ( member | view-reference ) ;

row             = encoding , "|" , mnemonic , "|" , action ;

encoding        = pattern , [ "d" ] , [ "n" , [ "n" ] ] ;
pattern         = 8 * pattern-bit ;
pattern-bit     = "0" | "1" | slice-char ;

mnemonic        = { literal | reference | "$nn" | "$nnnn" | "$e" | "+d" } ;
reference       = "{" , vocab-name , ":" , ( slice-char | view-name ) , "}" ;
view-reference  = "{" , vocab-name , ":" , view-name , "}" ;

action          = transfer | steps ;
transfer        = "goto" , table-name , [ "(" , ( display-text | view-name ) , ")" ] ;
steps           = step , { ";" , step } ;
step            = operation , [ { operand } , "<-" ] , { operand } ;
operation       = identifier | reference ;

operand         = [ parameter-name , "=" ] , ( reference | simple-operand ) ;
simple-operand  = ( "-" | "n" | number | indirect | name ) , [ "/" , attribute ] ;
indirect        = "(" , ( "n" | number | name ) , [ "+d" ] , ")" ;
attribute       = "delay" , "=" , digit ;
number          = digit , { digit } | "0x" , hex-digit , { hex-digit } ;

(* terminals *)
vocab-name      = ? a word, no space. Compared exactly, so `reg` and `Reg`
                    would be different vocabularies ? ;
scope-name      = ? the identifier of an enum a marked location accessor takes,
                    or of one some operation takes as a parameter. Compared
                    *exactly*, unlike a member: it names a C++ type rather than
                    something written the way assembly is written ? ;
slice-char      = ? one character other than "0" or "1", compared exactly ? ;
name            = ? no space, and no longer than `Name::capacity` characters.
                    Resolved against the CPU's locations, or against the
                    parameter's enum, ignoring case ? ;
identifier      = ? no space. Resolved against the CPU's operations, ignoring
                    case ? ;
parameter-name  = ? letters, digits and "_", no longer than `Name::capacity`
                    characters, matched against the operation's declared
                    parameter names ? ;
table-name      = ? no space ? ;
display-text    = ? a member's text as the vocabulary writes it, up to the ":"
                    or "/", so a rule matches `arith.adc`, not
                    `arith.adc:add8(carry)`; in a goto, a member of the target's
                    view vocabulary ? ;
literal         = ? mnemonic text containing no "{", "$" or "+d" ? ;
```

Some things the grammar is stricter about than it may look:

- **A reference must be a whole operand.** `{reg:z}` is fine and `({reg:z})` is not;
  indirection through a vocabulary comes from the *member* being written `(hl)`,
  not from parenthesising the reference.
- **Numbers are unsigned.** There is no `-2`. Where a row means a signed value
  it writes the byte, as in `relative pc <- pc 0xfe`, and the operation it feeds
  decides how to read it. A constant is checked against the parameter's type, so
  `0xfe` fits an 8-bit parameter and `0x1ff` does not.
- **`n` is the row's whole immediate**, not one byte of it. A row that fetches
  `n n` has a 16-bit `n`; there is no way to name the two bytes separately, and
  no Z80 instruction needs to.
- **A member is a single word**, so an argument list contains no spaces, and it
  carries an operation or a delay, never both. An empty attribute (`b/`) or an
  empty operation (`add:`) is an error.

Commas mean something only in a member's argument list and between a derived
table's rules; in a step they are decoration (see
[the action column](#the-action-column)).

---

## Vocabularies (`vocab`)

```
vocab pair = bc de hl sp
```

A vocabulary's name is a word. (The real `pair` also names its scope, `: R16`,
described below.) Its members are listed in the order
the opcode bits select them, so a vocabulary of four members belongs to a
two-bit slice and one of eight members to a three-bit slice. A mismatch is a
compile error.

### Where its members are looked up

```
vocab pair : R16 = bc de hl sp
```

A vocabulary may name the scope its members come from. Without one they are
looked up in every location the CPU offers, which is what most vocabularies
want. With one the search is that enum and nothing else, so a member that is not
one of its enumerators is an error naming the line, and a name that means two
things elsewhere means only one thing here.

The scope may be an enum of the machine's locations, one a marked accessor takes, or
an enum some operation takes as a parameter, which pins a vocabulary of values
to that enum. (What makes a name a value rather than a place is the type of the
parameter it reaches, not the scope; see [spellings](#spellings).)

```
vocab dir : BlockDirection = i d
```

An enum the machine declares but neither marks nor takes, such as the Z80's
`Bus`, is not a scope. Members are still matched ignoring case, and against an
enumerator's [spelling](#spellings) where it declares one. The *scope* is
matched exactly, because it names a C++ type.

Not every vocabulary can have one. `reg` is `b c d e h l (hl)/delay=1 a`, whose
names come from two different enums: seven registers and one addressing mode
built on a pair. That is a vocabulary of mixed things, and it keeps the default
search.

### Members

A member is written `display[:operation[(arguments)]]` or `display[/delay=N]`.
The `display` is also the member's operand, so it must be something an operand
may be: a name the CPU resolves, a constant, or either of those as an address.
A member carries an operation or a delay, not both, since a delay is what a
write-back through an operand costs.

| form | example | means |
|---|---|---|
| plain | `bc` | the member is that operand |
| bound operation | `and:and8` | naming this member as an *operation* applies `and8` |
| with arguments | `adc:add8(carry)` | …and passes `carry`, the row filling the rest |
| with an access cost | `(hl)/delay=1` | reading through it and writing back idles one cycle |
| hole | `-` | **the row does not cover that opcode at all** |

A hole is how a general row leaves room for a specific one. Slot 3 of `logic` is
`cp`, which writes only the flags (its row discards `cmp8`'s result with `-`),
so the general row leaves it to a row of its own:

```
vocab logic = and:and8 xor:xor8 or:or8 -
101wwzzz | {logic:w} {reg:z} | {logic:w} a, flags <- a {reg:z}
10111zzz | cp {reg:z}        | cmp8 -, flags <- a {reg:z}
```

### What a member decides

A member names an operation and may decide some of its arguments. The row fills
the ones the encoding varies; these are the rest, and they are written as a call
because that is what they are:

```
adc:add8(carry)                             the row gives two, this gives the third
rl:rotate8(left,carry)                      the row gives one, this gives two
rlca:fast_rotate_circular8(direction=left)  …and this one lands in the middle
```

Arguments may be named, exactly as a row's may be, which is what lets a member
supply an argument that is not the last one. Naming is all or nothing across the
whole call, so a row whose member names one must name its own too.

**A member may neither render nor pass an immediate**: only the encoding column
fetches those. This is the first of the limits under
[How general is it](#how-general-is-it).

---

## Tables

```
table base
```

Decoding starts in the **first table declared**; there is no reserved name
for the entry table.

A table is entered from another by a row whose action is a `goto`. This is not a
jump inside the decoder: it makes the machine *read another byte and decode it as
an opcode*, paying whatever that machine charges for an opcode fetch. A prefix is
therefore just an instruction whose entire job is to fetch another opcode, and it
costs what one costs.

On the Z80 that is four T-states and a refresh-register increment, which is why
`cb` costs four cycles before the instruction it introduces has been read at
all, and why `dd dd dd 23` is a legal instruction, each extra `dd` costing four
cycles.

(A *latched* table, below, is the exception: its opcode arrives by an operand
read rather than an instruction fetch.)

Every table must be reachable from the entry table by following gotos, and a
table that is not derived must have rows of its own. Reachable *from the entry*,
rather than merely named by some goto: two tables that only reach each other are
as dead as one nothing names at all, and would otherwise be generated and
checked in full.

### Derived tables

```
table ix = base with pair.hl -> ix, spair.hl -> ix, reg.h -> ixh, reg.l -> ixl, reg.(hl) -> (ix+d)/delay=1
```

A derived table decodes its parent's rows with some vocabulary members renamed,
and may carry rows of its own that override them by first-match-wins.

This is for the prefix that does not introduce a *new* instruction set but
re-reads an existing one under different names. Writing it as a derivation says
so, and costs one line instead of a duplicated table. The Z80's `DD` and `FD`
are exactly this: two thirds of their entries are the unprefixed instruction,
untouched.

A rule is written `vocabulary.member -> replacement`. **It names the vocabulary
it rewrites**, because the same spelling can mean different things in different
vocabularies and only some of them should change. The replacement is a whole
member, so it may bring its own operation or its own `/delay=`.

(In `z80.cpu`, `reg.h` is renamed by a view and the `real.h` of an indexed load is
not, even though both are written `h`.)

Rules apply to every row the table decodes, inherited or its own. A row escapes
a rename by naming a vocabulary no rule mentions. **Literal operands are never
rewritten**: a rename reaches `{field}` references only, so anything spelled out
in a row is immune by construction. That immunity is what keeps the Z80's
`ex de, hl` correct under `DD`, which two of the hand-written implementations in
this repository once got wrong (#39).

The same silence can hide a mistake, so it is checked: a row that spells out a
name its table renames, and is inherited unchanged by that table, is rejected.
Writing the row *in* the derived table is how one says the literal was meant.
This is what forces the override rows in `z80.cpu`'s `indexed` table, and it was
added after a missing one made `dd e3` do `ex (sp), hl` where the chip does
`ex (sp), ix`.

A parent must be declared above its children. A derived table with no rows of
its own is legal: it *is* its parent, renamed.

(The example above is what `DD` looks like on its own. `z80.cpu` does not write
it that way, because `FD` would then be the same thing again with two letters
changed: see views, next.)

### Views: a table that takes a parameter

```
table indexed(view:index) = base with pair.hl -> {index:view}, reg.h -> {index_hi:view}, …
```

`DD` and `FD` differ in one thing only: which index register they mean. Written
as two derivations that is two of everything: two tables, two sets of rules,
and two generated copies of every instruction on the prefixed page.

A **view** says it once. `(view:index)` declares that this table takes a
parameter called `view`, whose value is a member of the vocabulary `index`. The
table is then decoded once *per member* without being generated once per
member: the parameter is a run-time value the prefix supplies, so `ix` and `iy`
share every function between them.

What follows is the whole feature:

- **A reference may be selected by the view instead of by opcode bits.**
  `{index:view}` reads "the member of `index` that this table's view picked".
  Everywhere else `{reg:z}` names a slice of the opcode; here the instruction's
  own bytes do not carry the answer, because a byte already gone by chose it.
  A vocabulary selected this way must have as many members as the view's own.
- **A rule's right-hand side may be a view reference.** `pair.hl -> {index:view}`
  renames `hl` to whichever of `ix`/`iy` is in play, rather than to a fixed one.
- **A `goto` says which view it enters under.** `goto indexed(ix)` chooses a
  member by name. `goto indexed_cb(view)` *forwards* the view this table was
  itself decoded under, which is how `dd cb` keeps hold of the register `dd`
  chose. Both tables must draw their view from the same vocabulary for that to
  mean anything, and it is checked.

A table takes a view or it does not, and its gotos must agree: entering a
parameterised table without saying which member, or supplying one to a table
that takes none, is an error rather than a default.

Derivation and parameterisation are **separate mechanisms**. `z80.cpu` happens
to use both on one line, since `indexed` is derived from `base` *and* takes a
view, while `indexed_cb` takes a view and derives from nothing. A derived table
need not take a view, as the `table ix` example above shows.

#### Every member of a vocabulary a view selects must have the same shape

Nothing that runs at compile time can know which member a view will pick, so
every check resolves such a reference at member 0 and applies the answer to all
of them. That is only sound if the members agree about everything except which
location they name: whether they are indirect, whether they are displaced, what
a write-back costs, and how they render. So this is rejected:

```
vocab index_mem = (ix+d)/delay=1  (iy)
```

Without the check it would compile, and the `FD` page would *execute* `(iy+d)`
while *printing* `(iy)`, the one mistake in this format that would otherwise
produce a wrong emulator rather than a line number.

For the same reason a member of such a vocabulary may **only** name a location,
so `vocab m = ix:ld16 iy:inc16` is rejected too. An operation is spliced once,
from member 0, so a member bringing its own would be obeyed for the first view
and ignored for every other; and a member bringing the *same* operation as its
siblings only spells out something the row could say once.

A hole is excluded for the same reason. A hole exists so a general row can leave
room for a specific one in the *opcode space*, and a view has no opcode bits to
leave room in.

Each of these is reported against the line that selects the vocabulary by a
view, since that is what makes the rule apply, and the message names the line
the vocabulary was declared on, since that is where the fix goes. A vocabulary
nothing selects by a view may hold whatever it likes.

---

## Rows

### The encoding column

```
00pp0001 n n
```

Eight characters, then zero or more byte tokens.

- `0` and `1` are fixed bits.
- Any other character names a **slice**: a run of bits a vocabulary is selected
  by. Bits sharing a letter must be contiguous.
- A slice need not be referenced. `01yyy100 | neg` uses `yyy` only to say those
  bits are ignored, and all eight encodings mean `neg`.
- A pattern of nothing but one repeated letter is therefore the idiom for a
  **catch-all**: it matches every opcode, so it must come last, where it picks
  up whatever the rows above did not claim.

  ```
  xxxxxxxx | nop | nop
  ```

  This is how a table says what an undefined encoding does, and every table
  must say: an opcode that no row decodes is an error naming it. (`z80.cpu` ends
  its `ed` table with exactly this row, because on real hardware an undefined
  `ED` encoding behaves as a do-nothing instruction two bytes long, the `ed`
  prefix having already been fetched by the row that transferred here.)

Trailing tokens say what follows the opcode:

| token | meaning |
|---|---|
| `d` | a displacement byte this row reads but does not use. See [latched tables](#latched-tables) |
| `n` | one immediate byte; two of them make a 16-bit immediate |

At most one `d` and two `n`, and a `d` comes before any `n`, because that is the
order the bytes are fetched in (see [Where time goes](#where-time-goes)). A `d`
written after an `n` is an error: *'d' must come before 'n': the displacement is
fetched before the immediate*.

`d` in the encoding column and `+d` in an operand both concern a displacement,
but they are not the same statement and rarely appear together:

- `+d` in `(ix+d)` says **this operand is displaced**. Whether a byte is fetched
  for it is worked out from the operands, not declared. See
  [Displacement](#displacement).
- `d` in the encoding column says **this row reads a displacement it will not
  use itself**, and hands it to the table it transfers to. Only a prefix row
  needs it, and `check_immediates` ignores it: the "the action must use them"
  rule is about `n`.

### The mnemonic column

Literal text, plus:

| form | renders |
|---|---|
| `{reg:z}` | the vocabulary member the slice selects |
| `$nn` | an 8-bit immediate, as `0x3f` |
| `$nnnn` | a 16-bit immediate, as `0x1234` |
| `$e` | a **relative** target: the address the jump lands on, not the offset. Measured from the end of the instruction, so it must be the last byte the row reads, which it always is: `$e` counts as the row's one immediate byte, and a displacement is read before it |
| `+d` | an index displacement, as `+0x02` or `-0x01` |

The mnemonic is lowered into a fixed array of pieces at parse time, so the
disassembler renders without parsing anything at run time.

### The action column

Either a transfer to another table (`goto`, below), or an ordered list of steps
separated by `;`. Cost lives here: an idle cycle is a step like any other.

```
operation  destination... <- operand...
```

Three pieces of punctuation, and only two of them mean anything:

| | |
|---|---|
| `;` | separates one step from the next |
| `<-` | separates destinations from operands. Without it, everything after the operation is an operand |
| `,` | **nothing at all**, stripped from the end of a word, so a row can be punctuated like assembly |

So `inc8 {reg:y}, flags <- {reg:y} flags` has one step, two destinations
(`{reg:y}` and `flags`) and two operands (`{reg:y}` and `flags`); the comma could be
left out and the meaning would not change.

| step | example |
|---|---|
| apply | `inc8 {reg:y}, flags <- {reg:y} flags` |
| apply with no destination | `out_c bc {reg:y}` |
| apply with no operands | `exx` |
| condition | `{cond:y}`, whose operation returns `Continue` |
| idle | `delay 2` |

`delay` is an ordinary operation, one the CPU supplies, and takes a whole
number. The `/delay=` attribute on an addressing mode is a different thing that
happens to charge the same way, and it takes a single digit.

**How destinations are filled** depends on what the operation returns:

- returns nothing → the row may name no destination;
- returns one value → the row names one or more destinations, and every one
  receives it (which is how the undocumented `DD CB` register copy is written);
- returns an aggregate, a bundle of public fields → the row names one
  destination per member, filled in declaration order. Most Z80 arithmetic
  returns `{result, flags}`, which is why so many rows read
  `something dest, flags <- …`.

Any other class is one value; the Z80's `Flags` is. An aggregate with only one
member, or with a base class, is refused, since a row could not tell what it was
being given.

`-` discards a result and may only be a destination. A destination is a
location, an address, or `-`; a constant or `n` can only be written through, as
`(n)`.

**An operation may be a reference.** `{arith:q} a, flags <- a {reg:z}` takes its operation
from the vocabulary member the opcode selects, so one row is the whole
`add/adc/sub/sbc` group.

**A condition guards the rest of the row.** A step whose operation returns
`refract::Continue` is a condition: it is applied like any other step, and
`Continue::no` abandons the steps after it. Nothing in the row marks it, because
the operation's signature does, which is also why a condition names no
destination. `Continue` is a type of its own rather than `bool` because a `bool`
is a value, and a value still needs somewhere to go: a step whose operation
returns one and names no destination is an error, not a condition. An
operation taken from a vocabulary is a condition for every member or for none:
a vocabulary whose operations mixed the two would make one row branch at some
opcodes and not others, so it is refused against its declaration. There is no
`else`, and none is needed for a conditional whose conditional part comes last,
which is every conditional on the Z80.

The pay-off is that the extra cycles of a taken branch come from the steps the
condition guards, so no row states two cycle counts:

```
11yyy000 | ret {cond:y} | delay 1 ; {cond:y} ; ld16 pc <- (sp) ; inc16 sp <- sp ; inc16 sp <- sp
```

Five T-states when not taken, eleven when taken, with neither number written
down. Two conditions in a row are an "and".

**A `goto` is the whole of its row.** A row that transfers renders nothing, so
allowing it to do anything else would make the two columns disagree.

Such a row still has to have a mnemonic column, because a row has three columns.
It is never printed: the disassembler follows the transfer and renders whatever
the destination row says. `z80.cpu` writes `(cb)`, `(dd)` and so on there, purely
so a human reading the table can see what the prefix is.

---

## Where time goes

No row states a cycle count. Every number in the timings quoted throughout this
document comes from the same few places, and it is worth having them in one
list because nothing else here says so.

| what | charged by |
|---|---|
| the opcode fetch, including every prefix byte | the CPU's `fetch_opcode` |
| each byte of an immediate, any displacement, and a latched table's opcode | the CPU's `fetch_immediate` |
| each read or write through an indirect operand | the CPU's `read_memory` / `write_memory`, or their 16-bit forms |
| forming a displaced address | the CPU's `displaced_address` |
| an explicit `delay` step, or a `/delay=` on an addressing mode | the CPU's `delay` |

**The unit is whatever the CPU counts in.** The format has none of its own:
`delay 2` passes 2 to the CPU's `delay`, and what that buys is the CPU's
business. For the Z80 it is T-states, which is why this document says both
"cycles" and "T-states" and means the same thing.

**Everything the encoding names is fetched before any step runs**, in the order
the bytes appear: displacement first, then immediates, which is why the encoding
column must write them in that order. A step list can therefore never make a
fetch cheaper, which is correct, since the machine has to read the bytes before
it can know it did not need them. This is why `jr nz` costs seven T-states even
when not taken: the displacement was read before the condition.

**A write-back delay is charged only for a read-modify-write.** A `/delay=1` on
an addressing mode is the idle *between* reading through it and writing back, so
it applies only where the same addressing mode is both an operand and a
destination of the same step. That is the difference between these two rows,
which share a vocabulary:

```
01yyyzzz | ld {reg:y}, {reg:z} | ld8 {reg:y} <- {reg:z}
00yyy100 | inc {reg:y}         | inc8 {reg:y}, flags <- {reg:y} flags
```

`ld (hl), b` is 7 T-states: it writes through `(hl)` without having read through
it, so it pays 4 + 3. `inc (hl)` is 11: it names `(hl)` on both sides, so it
pays 4 + 3 + 1 + 3.

### What this model cannot say

Cost is a total per instruction, ordered at **step granularity**. Two things
follow, and both matter to anyone building a cycle-exact core:

- **A multi-byte access is indivisible.** `ld16 pc <- (sp)` is one operand, and
  its two bus cycles happen back to back; no step can be scheduled between them.
- **Nothing below a step can be reordered or observed.** A step's own reads and
  writes happen in the order the operation performs them, and the table has no
  say in it.

For total cycle counts, and for a machine that contends on the address bus at
instruction granularity, that is enough. For one that needs a *schedule* of bus
cycles, with every access placed at a known offset within the instruction, it is
not, and the format would need to grow. [NOTES.md](NOTES.md) has where that
stands.

### What this model does not cover at all

Interrupts, reset, wait states and bus arbitration are **outside the format**.
There is no way to write a row for an interrupt-acknowledge sequence, no
construct for an instruction that changes whether the *next* one can be
interrupted (the Z80's `ei` says so only by writing a location of the machine's,
`deferred`, and `start_instruction` decides what that means), and no "wait here
until something external happens" step. All of it
belongs to the machine that drives the decoder, through `start_instruction`, not
to the table. The one thing the table does contribute is that a repeating
instruction is written as a rewind rather than a loop, so it re-enters the
decoder between iterations and an interrupt has somewhere to land.

---

## Operands

| form | example | meaning |
|---|---|---|
| name | `a`, `hl`, `carry`, `pc` | a location the CPU supplies |
| name, to an enum parameter | `left`, `i` | a value: that enum's enumerator, by its [spelling](#spellings) |
| constant | `7`, `0x38` | a literal, checked to fit the parameter |
| immediate | `n` | the bytes the encoding fetched |
| reference | `{reg:z}` | whichever member the opcode selects |
| indirect | `(hl)`, `(n)` | *the address*: read or written through |
| displaced | `(ix+d)` | …offset by the displacement byte |
| discard | `-` | destination only |

Parentheses are a modifier, not a kind: `(hl)` is `hl` used as an address. How
wide the access is comes from the parameter it feeds, so `ld16 hl <- (n)` reads
two bytes and `ld8 a <- (n)` one, with the row saying neither. A parameter read
through an address must be `std::uint8_t` or `std::uint16_t`, the two widths the
machine reads at.

An operand may carry `/delay=1` exactly as a vocabulary member can, for an
addressing mode written out in a row rather than named by one.

### Keyword operands

An operand may name the parameter it feeds instead of relying on its position:

```
01bbbzzz | bit {bit:b}, {reg:z} | test_bit flags <- value={reg:z} bit={bit:b} flags=flags bus={reg:z}
```

The names are the parameter names in the CPU's own declaration, read off it by
reflection. Nothing restates them, so a parameter that is renamed in C++ is
renamed here, and a row that still uses the old name fails to build with the
line that wrote it.

This exists because position is a silent coupling. `test_bit` above takes three
`std::uint8_t` parameters, so a row that swaps two of them compiles, runs, and
quietly tests the wrong bit. Types cannot catch it and neither can a reader.

Two rules:

- **All or nothing within a step.** A half-named argument list needs a rule
  about what "the next one" means, and a description is easier to read if there
  is no such rule to remember.
- **A destination may not be named.** It is where the result goes, not something
  handed to the operation.

Naming changes which argument an operand becomes, never **when it is read**.
Operands are still resolved in the order the row writes them, which matters
because resolving one can read memory and move the address bus.

A keyword is an identifier followed by `=`, and nothing else is, which is what
keeps `(hl)/delay=1` from looking like one: what precedes its `=` is not an
identifier.

### Displacement

`(ix+d)` is an address formed from a base and a signed byte. **Nothing declares
that the byte is fetched**: the row says `{reg:z}`, a view says that member is now
`(ix+d)`, and the framework asks what the operands resolve to. One displacement
per instruction, shared by every operand that uses it: `inc (ix+d)` reads and
writes through one address, formed once, and an instruction displaced through
two different bases is an error.

Because it is worked out per opcode rather than declared, the mnemonic is
checked per opcode too: a row must render `+d` exactly when the opcode it
renders is displaced, so neither a missing nor a spurious `+d` survives.

The CPU description decides how a base and an offset combine *and what forming
the address costs*. A machine has one `displaced_address`, so every displaced
mode combines alike; a processor whose modes wrap or charge differently forms
those addresses in steps, as the 6502 description does through `ea`. It is told
how many bytes were read after the displacement and before the address is formed
(the row's immediates, and a latched table's opcode), because on some machines
those reads happen inside the same window. (That is why the Z80's `ld (ix+d), n` is 19
T-states and not 22.) The machine also says how many bytes its window holds,
as `displacement_window_bytes`, and a row that reads more is the error.

What the CPU description does *not* decide is the offset's width or sign. One
signed byte is baked into the format: it is what the encoding column's `d`
fetches, `displaced_address` is handed it as a `std::int8_t`, and the
disassembler renders `+d` as `+0x02` or `-0x01` and `$e` as a target measured
from the end of the instruction, with no way for a machine to say otherwise.
This is one of the limits under [How general is it](#how-general-is-it).

---

## Latched tables

Most encodings put their opcode first and their operands after. Where an
encoding interleaves them, with a byte that must be read *before* the opcode
that decides what to do with it, the row that meets that byte reads it and hands it
on to the table it transfers to.

The Z80 has exactly one such encoding, `DD CB d op`:

```
table indexed(view:index) = base with …
11001011 d | (dd cb) | goto indexed_cb(view)
```

Everything else is derived from that `d`. A table reached by a row that reads a
displacement is *latched*, and two things follow without being declared:

- its rows use the incoming displacement instead of reading their own;
- its opcode arrives by an **operand read** rather than an instruction fetch,
  because the machine has already committed to an instruction, and it is no longer
  deciding what to run. On the Z80 that is three cycles instead of four, and is
  why the refresh register does not increment for that byte.

A table reached both with and without a displacement is a compile error.

---

## What is checked

All of this happens during constant evaluation, and each failure names the line
in the `.cpu` file:

- **Precedence**: overlapping rows nest; no row is shadowed or matches nothing.
  [How rows are ordered](#how-rows-are-ordered).
- **Column agreement**: the mnemonic renders the immediate the encoding fetches,
  and the action uses it ([The three columns](#the-three-columns)); `+d` appears
  exactly where an operand is displaced ([Displacement](#displacement)).
- **Encoding tokens**: at most one `d` and two `n`, `d` first
  ([The encoding column](#the-encoding-column)).
- **Vocabulary size** against the slice or view that selects it
  ([Vocabularies](#vocabularies-vocab), [Views](#views-a-table-that-takes-a-parameter)).
- **Names**: each operation and location resolves to exactly one thing, ignoring
  case; scopes and names inside the format compare exactly
  ([The guiding rule](#the-guiding-rule), [Spellings](#spellings), [Grammar](#grammar)).
- **Arity and shape**: operands match parameters and destinations match the
  result ([The action column](#the-action-column), [Operands](#operands)).
- **Conditions**: no machine, no destination, and all or none of a vocabulary
  ([The action column](#the-action-column)).
- **Totality**: every opcode of every table decodes
  ([The encoding column](#the-encoding-column)).
- **Reachability**: every table is reachable from the entry, and every table not
  derived has rows ([Tables](#tables)).
- **Views**: gotos agree with the table, and a view's members share one shape
  ([Views](#views-a-table-that-takes-a-parameter)).
- **Override containment**: a derived row fits inside what it overrides
  ([How rows are ordered](#how-rows-are-ordered)).
- **Renamed names spelled out**: no inherited row spells out what its table
  renames ([Derived tables](#derived-tables)).
- **Latch consistency** ([Latched tables](#latched-tables)).
- **Displacement**: one base per instruction, within the machine's window
  ([Displacement](#displacement)).
- **Every line means something** ([Lexical structure](#lexical-structure)).
- **Capacity**: each limit reports itself rather than overflowing
  ([Limits](#limits)).

One mistake is reported less well. A location's value converts to the parameter
it feeds the ordinary C++ way, so a row handing a 16-bit location to an 8-bit
parameter is caught by the compiler's own conversion warning (`-Wconversion`,
an error in this project's build), which points into the generated code rather
than at the `.cpu` line.

### How rows are ordered

Precedence is by position in the file. Where two rows in a table overlap, the
earlier must be wholly contained in the later, which is an override; a partial
overlap is an accident. A row that would be completely shadowed is rejected, as
is one that matches no opcode at all, which can only happen through holes,
since every eight-bit pattern matches something otherwise.

A derived table's own rows all come **before** everything it inherits, whatever
line they are written on. So a derived table's row always wins over the parent
row it overlaps, which is what makes it an override, and it must fit inside that
row: one that took opcodes the parent row meant to keep would silently change
instructions nobody was looking at.

Which raises a question the derivation example does not answer on its own: `DD 76` is
`halt` on real hardware, so how does the parent's `01110110` survive the derived
`01yyy110`? Not by precedence, which it would lose. It survives because `real`, the
vocabulary that row selects with, has a **hole** at slot 6, so `01yyy110` does
not match `0x76` at all and the opcode falls through to the inherited row. Holes
are what make a match set non-rectangular, and containment is computed over the
actual match sets, so a row with holes is compared by what it really covers.

---

## Limits

Fixed capacities, chosen to fit what exists rather than on principle. Each one
reports its own limit when reached, so raising it is a change to what is named
here, made in response to a message rather than a guess. The figures are
the constants' values as this is written; the constants are the authority.

| | | constant |
|---|---:|---|
| vocabularies per description | 256 | `max_vocabularies` (Parse.hpp) |
| tables per description | 256 | `max_tables` (Parse.hpp) |
| vocabulary members | 8 | `Vocabulary::max_members` |
| substitutions per derived table | 6 | `Rules` (Model.hpp) |
| steps per row | 6 | `Row::max_steps` |
| operands, and destinations, per step | 4 | `max_operands` (Model.hpp) |
| pieces per mnemonic | 12 | `Row::max_pieces` |
| pieces per vocabulary member | 3 | `Member::max_pieces` |
| arguments a member may fix | 3 | `Member::Operation::max_arguments` |
| slices per opcode pattern | 4 | `Pattern::max_slices` |
| immediate bytes per row | 2 | `parse_encoding` (Parse.hpp), and the 16-bit immediate a handler carries |
| characters in a name | 15 | `Name::capacity` |

The vocabulary and table counts are held in a byte wherever one is referred to,
so those two are bounded by the byte rather than by a judgement, and raising
them means widening it. A name here is an operand, a parameter, a scope or a
spelling.

One limit is not a capacity but a shape: **an opcode is eight bits**
(`Pattern::num_bits`). A pattern is always eight characters and a table always
has 256 entries. Both machines described so far have byte opcodes, so this has
never been tested against one that does not.

---

## Worked examples

**One row, one instruction.**

```
00000000 | nop | nop
```

**One row, four instructions.** `pp` selects a pair; two `n` make a 16-bit
immediate; the mnemonic renders it.

```
00pp0001 n n | ld {pair:p}, $nnnn | ld16 {pair:p} <- n
```

**One row, sixty-four instructions.** Both operands come from the same
vocabulary, selected by different slices.

```
01yyyzzz | ld {reg:y}, {reg:z} | ld8 {reg:y} <- {reg:z}
```

Slot 6 of `reg` is `(hl)`, so this row also covers `ld b,(hl)` and `ld (hl),b`,
with their memory access and its timing, and nothing says so twice. `01110110`
would be `ld (hl),(hl)`, which is really `halt`, declared earlier, so it wins.

**The operation from the vocabulary.**

```
vocab arith = add:add8(0) adc:add8(carry) sub:sub8(0) sbc:sub8(carry)
100qqzzz | {arith:q} a, {reg:z} | {arith:q} a, flags <- a {reg:z}
```

`add` and `adc` are the same operation with a different final argument, which is
what the chip does too.

**Cost that belongs to the addressing mode.**

```
vocab reg = b c d e h l (hl)/delay=1 a
00yyy100 | inc {reg:y} | inc8 {reg:y}, flags <- {reg:y} flags
```

`inc b` is 4 T-states and `inc (hl)` is 11 (four to fetch, three to read, one
idle, three to write) with the row mentioning no numbers at all.

**A conditional, and where its extra cycles come from.**

```
001jj000 n | jr {jcond:j}, $e | {jcond:j} ; delay 5 ; relative pc <- pc n
```

Seven T-states not taken, twelve taken.

**A view.**

```
table indexed(view:index) = base with pair.hl  -> {index:view}, \
                                      spair.hl -> {index:view}, \
                                      reg.h    -> {index_hi:view}, \
                                      reg.l    -> {index_lo:view}, \
                                      reg.(hl) -> {index_mem:view}

01yyy110 | ld {real:y}, {index_mem:view} | ld8 {real:y} <- {index_mem:view}
```

One table for both `DD` and `FD`: `{index:view}` is whichever index register the
prefix chose. The override row exists because `ld h,(ix+d)` uses the *real* `h`.
It says so by naming `real`, the vocabulary of true registers, which no rule
rewrites.

**A repeat, and a direction the opcode carries.**

```
vocab dir : BlockDirection = i d
1011w000 | ld{dir:w}r | block_load flags <- {dir:w} flags ; nonzero16 bc ; delay 5 ; \
                        relative pc <- pc 0xfe
```

One row for `ldir` and `lddr` both. Bit 3 *is* the direction, so the row hands
it to the operation rather than spelling out two rows that differ in an
argument. The parameter's type makes the two members values rather than places
to read from, and the scope clause pins them to `BlockDirection`, by their
[spellings](#spellings).

The rewind is what the chip actually does, re-executing the opcode, which is why
an interrupt can land in the middle of an `ldir`.
