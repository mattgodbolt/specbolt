# The `.cpu` format

A reference for the instruction-set description that v4 compiles. The file this
describes is [`z80.cpu`](z80.cpu); the parser that reads it is `Table.hpp`, and
the code generator that consumes the result is `Execute.hpp`. For why the format
is shaped this way rather than some other way, see [NOTES.md](NOTES.md).

---

## Overview

A `.cpu` file describes an instruction set as data. It is `#embed`ed into the
build, parsed during constant evaluation, and used to generate two things: a
disassembler and an interpreter. Nothing in it is read at run time — by the time
the program starts, the file has become code.

The format is not specific to any one processor. Every example here is drawn
from `z80.cpu` because that is the description this repository has, and where a
passage explains *why* a feature exists it usually cites the Z80 for the same
reason — but the feature is the general one, and the paragraph will say so.

The file has three kinds of line, in any order except that a name must be
declared before it is used:

- **`field`** declares a *vocabulary*: the list of things a group of opcode bits
  can select between.
- **`table`** declares a *decoding table*: 256 opcodes' worth of rows. A table
  may be *derived* from another, re-reading its rows with some vocabulary
  members renamed.
- **rows** describe instructions. Every row has three columns separated by `|`.

### The three columns

```
00110100     | inc (hl)        | inc8 (hl), flags <- (hl) flags
^^^^^^^^^^^^   ^^^^^^^^^^^^^^^   ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
encoding       mnemonic          action
```

- The **encoding** says which opcodes the row matches and what bytes follow it.
- The **mnemonic** says how to print it.
- The **action** says what it does, as an ordered list of steps.

The three are cross-checked against each other at compile time. The encoding is
the authority on length: if the mnemonic renders a different number of immediate
bytes than the encoding fetches, or the action uses an immediate the encoding
never read, the build fails with the line number.

### The guiding rule

**The table names things; it does not define them.** Every operation it names
(`inc8`, `add16`, `is_set`) and every location it names (`a`, `hl`, `carry`,
`pc`) is looked up by reflection in the CPU description — for the Z80 that is
`Z80Cpu.hpp`, and it is the whole of what retargeting means. A name the CPU does
not supply is a compile error naming the line that asked for it.

---

## Lexical structure

Lines are trimmed of leading and trailing spaces and tabs, and of a trailing
carriage return. After trimming:

| line | meaning |
|---|---|
| empty | ignored |
| begins with `#` | a comment, ignored |
| begins with `field ` | a vocabulary declaration |
| begins with `table ` | a table declaration |
| contains `\|` | a row |
| anything else | ignored — see the warning below |

> **Comments must be on their own line.** `#` is only special at the start of a
> line, so a trailing comment on a row becomes part of the action column and
> will fail to parse as one.

> **A line with no `\|` is silently ignored.** A mistyped row that loses its
> separators does not become a diagnostic; it becomes an opcode that fails to
> decode at run time. This is a known sharp edge, recorded in NOTES.

---

## Grammar

```ebnf
file            = { line } ;
line            = comment | field-decl | table-decl | row | empty ;
comment         = "#" , { any } ;

field-decl      = "field" , name-char , "=" , member , { member } ;
member          = hole | ( operand-text , [ ":" , primitive , [ "+" , operand-text ] ] ,
                           [ "/" , attribute ] ) ;
hole            = "-" ;
attribute       = "delay" , "=" , digit ;

table-decl      = "table" , table-name , [ "=" , table-name , "with" , rules ] ;
rules           = rule , { "," , rule } ;
rule            = name-char , "." , member-text , "->" , member ;

row             = encoding , "|" , mnemonic , "|" , steps ;

encoding        = pattern , { encoding-byte } ;
pattern         = 8 * pattern-bit ;
pattern-bit     = "0" | "1" | slice-char ;
encoding-byte   = "n" | "d" ;

mnemonic        = { literal | reference | "$nn" | "$nnnn" | "$e" | "+d" } ;
reference       = "{" , name-char , [ ":" , slice-char ] , "}" ;

steps           = step , { ";" , step } ;
step            = goto-step | if-step | apply-step ;
goto-step       = "goto" , table-name ;
if-step         = "if" , verb , { operand } ;
apply-step      = verb , [ { operand } , "<-" ] , { operand } ;
verb            = identifier | reference ;

operand         = "-" | "n" | number | reference | indirect | name , [ "/" , attribute ] ;
indirect        = "(" , ( name | reference ) , [ "+d" ] , ")" ;
number          = digit , { digit } | "0x" , hex-digit , { hex-digit } ;
```

**Commas are decoration.** A trailing comma is stripped from any word in a step,
so `inc8 {r:y}, flags` and `inc8 {r:y} flags` mean the same thing. They are
there so a row can be punctuated the way assembly is, and they carry no meaning:
in particular a comma does *not* separate destinations from operands — `<-` does
— and it does not separate steps — `;` does.

---

## Vocabularies (`field`)

```
field p = bc de hl sp
```

A vocabulary's name is **one character**. Its members are listed in the order
the opcode bits select them, so a vocabulary of four members belongs to a
two-bit slice and one of eight members to a three-bit slice. A mismatch is a
compile error.

### Members

A member is written `display[:primitive[+operand]][/delay=N]`.

| form | example | means |
|---|---|---|
| plain | `bc` | the member is that operand |
| bound primitive | `and:and8` | naming this member as a *verb* applies `and8` |
| with appended operand | `adc:add8+carry` | …and passes `carry` as a final argument |
| with an access cost | `(hl)/delay=1` | reading through it and writing back idles one cycle |
| hole | `-` | **the row does not cover that opcode at all** |

A hole is how a general row leaves room for a specific one:

```
field w = and:and8 xor:xor8 or:or8 -      # slot 3 is `cp`, which returns flags only
101wwzzz | {w} {r:z}  | {w} a, flags <- a {r:z}
10111zzz | cp {r:z}   | cmp8 -, flags <- a {r:z}
```

A member's display text is also parsed as an operand, so it must name something
the CPU can resolve. A member may not be an immediate: only the encoding column
fetches those.

---

## Tables

```
table base
```

Decoding starts in the **first table declared**; no name is special.

A table is entered from another by a `goto` step. This is not a jump inside the
decoder: it makes the machine *read another byte and decode it as an opcode*,
paying whatever that machine charges for an opcode fetch. A prefix is therefore
just an instruction whose entire job is to fetch another opcode, and it costs
what one costs.

On the Z80 that is four T-states and a refresh-register increment, which is why
`cb` costs four cycles before the instruction it introduces has been read at
all, and why `dd dd dd 23` is a legal instruction costing four cycles a byte.

(A *latched* table, below, is the exception: its opcode arrives by an operand
read rather than an instruction fetch.)

### Derived tables (views)

```
table ix = base with p.hl -> ix, k.hl -> ix, r.h -> ixh, r.l -> ixl, r.(hl) -> (ix+d)/delay=1
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
member, so it may bring its own primitive and its own `/delay=`.

(In `z80.cpu`, `r.h` is renamed by a view and the `s.h` of an indexed load is
not, even though both are written `h`.)

Rules apply to every row the table decodes, inherited or its own. A row escapes
a rename by naming a vocabulary no rule mentions. **Literal operands are never
rewritten**: a rename reaches `{field}` references only, so anything spelled out
in a row is immune by construction. (That immunity is what keeps the Z80's
`ex de, hl` correct under `DD`, which two of the hand-written implementations in
this repository get wrong.)

A parent must be declared above its children. A derived table with no rows of
its own is legal: it *is* its parent, renamed.

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
  bits are ignored — all eight encodings mean `neg`.
- A pattern of nothing but one repeated letter is therefore the idiom for a
  **catch-all**: it matches every opcode, so it must come last, where it picks
  up whatever the rows above did not claim.

  ```
  xxxxxxxx | nop | nop
  ```

  This is how a table says what an undefined encoding does. (`z80.cpu` ends its
  `ed` table with exactly this row, because on real hardware an undefined `ED`
  encoding behaves as a do-nothing instruction two bytes long — the `ed` prefix
  having already been fetched by the row that transferred here.)

Trailing tokens say what follows the opcode:

| token | meaning |
|---|---|
| `n` | one immediate byte; two of them make a 16-bit immediate |
| `d` | a displacement this row reads but hands on — see *latched tables* |

At most two `n` and one `d`.

### The mnemonic column

Literal text, plus:

| form | renders |
|---|---|
| `{r:z}` | the vocabulary member the slice selects |
| `{p}` | shorthand when the vocabulary and the slice share a letter |
| `$nn` | an 8-bit immediate, as `0x3f` |
| `$nnnn` | a 16-bit immediate, as `0x1234` |
| `$e` | a **relative** target: the address the jump lands on, not the offset |
| `+d` | an index displacement, as `+0x02` or `-0x01` |

The mnemonic is lowered into a fixed array of pieces at parse time, so the
disassembler renders without parsing anything at run time.

### The action column

An ordered list of steps separated by `;`. Cost lives here: an idle cycle is a
step like any other.

```
verb  destination... <- operand...
```

Three pieces of punctuation, and only two of them mean anything:

| | |
|---|---|
| `;` | separates one step from the next |
| `<-` | separates destinations from operands. Without it, everything after the verb is an operand |
| `,` | **nothing at all** — stripped from the end of a word, so a row can be punctuated like assembly |

So `inc8 {r:y}, flags <- {r:y} flags` has one step, two destinations
(`{r:y}` and `flags`) and two operands (`{r:y}` and `flags`); the comma could be
left out and the meaning would not change.

| step | example |
|---|---|
| apply | `inc8 {r:y}, flags <- {r:y} flags` |
| apply with no destination | `out_c bc {r:y}` |
| apply with no operands | `exx` |
| transfer | `goto cb` |
| condition | `if {c:y}` |
| idle | `delay 2` |

**How destinations are filled** depends on what the primitive returns:

- returns nothing → the row may name no destination;
- returns one value → every destination named receives it (which is how the
  undocumented `DD CB` register copy is written);
- returns a struct of *n* members → the row must name *n* destinations, filled
  positionally. `Alu` returns `{result, flags}`, which is why so many rows read
  `something dest, flags <- …`.

`-` discards a result and may only be a destination.

**A verb may be a reference.** `{q} a, flags <- a {r:z}` takes its operation
from the vocabulary member the opcode selects, so one row is the whole
`add/adc/sub/sbc` group.

**`if` guards the rest of the row.** It applies a primitive that yields a bool
and abandons the remaining steps when it is false. There is no `else`, and none
is needed for a conditional whose conditional part comes last — which is every
conditional on the Z80.

The pay-off is that the extra cycles of a taken branch come from the steps the
condition guards, so no row states two cycle counts:

```
11yyy000 | ret {c:y} | delay 1 ; if {c:y} ; ld16 pc <- (sp) ; inc16 sp <- sp ; inc16 sp <- sp
```

Five T-states when not taken, eleven when taken, with neither number written
down. Two `if` steps in a row are an "and".

**`goto` must be the row's only step.** A row that transfers renders nothing, so
allowing it to do anything else would make the two columns disagree.

---

## Operands

| form | example | meaning |
|---|---|---|
| name | `a`, `hl`, `carry`, `pc` | a location the CPU supplies |
| constant | `7`, `0x38` | a literal, checked to fit the parameter |
| immediate | `n` | the bytes the encoding fetched |
| reference | `{r:z}` | whichever member the opcode selects |
| indirect | `(hl)`, `(n)` | *the address*: read or written through |
| displaced | `(ix+d)` | …offset by the displacement byte |
| discard | `-` | destination only |

Parentheses are a modifier, not a kind: `(hl)` is `hl` used as an address. How
wide the access is comes from the parameter it feeds — `ld16 hl <- (n)` reads
two bytes and `ld8 a <- (n)` one, with the row saying neither.

An operand may carry `/delay=1` exactly as a vocabulary member can, for an
addressing mode written out in a row rather than named by one.

### Displacement

`(ix+d)` is an address formed from a base and a signed byte. **Nothing declares
that the byte is fetched**: the row says `{r:z}`, a view says that member is now
`(ix+d)`, and the framework asks what the operands resolve to. One displacement
per instruction, shared by every operand that uses it — `inc (ix+d)` reads and
writes through one address, formed once.

The CPU description decides how a base and an offset combine *and what forming
the address costs* — a processor that wraps within a page for one mode and
charges for crossing one in another says so there, not here. It is told how many
bytes the instruction has already read, because on some machines those reads
happen inside the same window. (That is why the Z80's `ld (ix+d), n` is 19
T-states and not 22.)

---

## Latched tables

Most encodings put their opcode first and their operands after. Where an
encoding interleaves them — a byte that must be read *before* the opcode that
decides what to do with it — the row that meets that byte reads it and hands it
on to the table it transfers to.

The Z80 has exactly one such encoding, `DD CB d op`:

```
table ix = base with …
  11001011 d | (dd cb) | goto ddcb
```

Everything else is derived from that `d`. A table reached by a row that reads a
displacement is *latched*, and two things follow without being declared:

- its rows use the incoming displacement instead of reading their own;
- its opcode arrives by an **operand read** rather than an instruction fetch,
  because the machine has already committed to an instruction — it is no longer
  deciding what to run. On the Z80 that is three cycles instead of four, and is
  why the refresh register does not increment for that byte.

A table reached both with and without a displacement is a compile error.

---

## What is checked

All of this happens during constant evaluation, and every failure names the line
in the `.cpu` file:

- **Precedence.** Where two rows in a table overlap, the earlier must be wholly
  contained in the later — that is an override. A partial overlap is an
  accident. A row that would be completely shadowed, or that matches no opcode
  at all, is rejected.
- **Column agreement.** The mnemonic must render exactly the immediate bytes the
  encoding fetches, and the action must use them.
- **Vocabulary size** against the slice that selects it.
- **Names.** Every primitive and every location must resolve to exactly one
  thing the CPU supplies. Matching is case-insensitive.
- **Arity and shape.** The operands a row supplies must match the primitive's
  parameters, and its destinations must match what the primitive returns.
  Constants must fit the parameter they are passed to.
- **Reachability.** A table nothing reaches is never instantiated and so is
  never type-checked; that is rejected, as is a non-derived table with no rows.
- **Latch consistency**, as above.
- **Capacity.** Every fixed limit reports itself rather than overflowing.

---

## Limits

Fixed capacities, chosen to fit what exists rather than on principle. Each one
reports its own limit when reached, so raising it is a one-line change made in
response to a message rather than a guess.

| | |
|---|---:|
| vocabulary members | 8 |
| substitutions per derived table | 6 |
| steps per row | 6 |
| operands, and destinations, per step | 4 |
| pieces per mnemonic | 12 |
| pieces per vocabulary member | 3 |
| immediate bytes per row | 2 |
| characters in a name | 15 |

---

## Worked examples

**One row, one instruction.**

```
00000000 | nop | nop
```

**One row, four instructions.** `pp` selects a pair; two `n` make a 16-bit
immediate; the mnemonic renders it.

```
00pp0001 n n | ld {p}, $nnnn | ld16 {p} <- n
```

**One row, sixty-four instructions.** Both operands come from the same
vocabulary, selected by different slices.

```
01yyyzzz | ld {r:y}, {r:z} | ld8 {r:y} <- {r:z}
```

Slot 6 of `r` is `(hl)`, so this row also covers `ld b,(hl)` and `ld (hl),b`,
with their memory access and its timing, and nothing says so twice. `01110110`
would be `ld (hl),(hl)`, which is really `halt` — declared earlier, so it wins.

**The verb from the vocabulary.**

```
field q = add:add8+0 adc:add8+carry sub:sub8+0 sbc:sub8+carry
100qqzzz | {q} a, {r:z} | {q} a, flags <- a {r:z}
```

`add` and `adc` are the same primitive with a different final argument, which is
what the chip does too.

**Cost that belongs to the addressing mode.**

```
field r = b c d e h l (hl)/delay=1 a
00yyy100 | inc {r:y} | inc8 {r:y}, flags <- {r:y} flags
```

`inc b` is 4 T-states and `inc (hl)` is 11 — four to fetch, three to read, one
idle, three to write — with the row mentioning no numbers at all.

**A conditional, and where its extra cycles come from.**

```
001jj000 n | jr {j}, $e | if {j} ; delay 5 ; relative pc <- pc n
```

Seven T-states not taken, twelve taken.

**A view.**

```
table ix = base with p.hl -> ix, k.hl -> ix, r.h -> ixh, r.l -> ixl, r.(hl) -> (ix+d)/delay=1

01yyy110 | ld {s:y}, (ix+d) | ld8 {s:y} <- (ix+d)
```

The override row exists because `ld h,(ix+d)` uses the *real* `h`. It says so by
naming `s`, the vocabulary of true registers, which no rule rewrites.

**A repeat.**

```
10110000 | ldir | block_load flags <- 1 flags ; if nonzero16 bc ; delay 5 ; relative pc <- pc 0xfe
```

The rewind is what the chip actually does — re-execute the opcode — which is why
an interrupt can land in the middle of an `ldir`.
