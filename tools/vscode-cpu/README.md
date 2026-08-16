# `.cpu` syntax highlighting for VS Code

A TextMate grammar for the instruction-set description format that v4 compiles.
The format is documented in [`CPU_FORMAT.md`](../../z80/v4/CPU_FORMAT.md); the
file it exists for is [`z80.cpu`](../../z80/v4/z80.cpu).

## Installing

VS Code loads any extension it finds in its extensions directory, so a symlink
is enough: no packaging, no marketplace:

```sh
ln -s "$PWD/tools/vscode-cpu" ~/.vscode/extensions/cpu-instruction-table
```

Use `~/.vscode-insiders/extensions` for Insiders. Reload the window afterwards
(`Developer: Reload Window`), and `.cpu` files will highlight.

### Over a remote connection

A grammar is a UI extension: it runs in the *local* extension host, whichever
machine the code is on. So it goes in the local `~/.vscode/extensions`, never
the remote's `~/.vscode-server/extensions`.

The tidy answer is a checkout on the local machine as well, and the symlink
above, which goes on tracking the repository, so an edit to the grammar needs
only a window reload. Failing that, package it and carry the result across:

```sh
npx @vscode/vsce package                                   # in this directory
code --install-extension cpu-instruction-table-0.1.0.vsix  # on the local machine
```

A packaged copy does not track the repository, so changing the grammar means
packaging and installing again.

To check what a given piece of text was scoped as, run
`Developer: Inspect Editor Tokens and Scopes` with the cursor on it.

## What it colours

The three columns are scoped separately, because they are three different
languages sharing a line.

| | scoped as |
|---|---|
| fixed bits of an encoding | `constant.numeric.binary` |
| a slice, the bits a vocabulary is selected by | `variable.parameter.slice` |
| the encoding's `n` and `d` byte tokens | `constant.other.immediate` / `.displacement` |
| mnemonic literal text | `string.unquoted.mnemonic` |
| `$nn`, `$nnnn`, `$e`, `+d` in a mnemonic | `constant.character.format.placeholder` |
| the first word of a step, the operation | `support.function.operation` |
| `goto`, `if`, `vocab`, `table`, `with` | `keyword.control` |
| `<-` | `keyword.operator.assignment` |
| a location the CPU supplies | `variable.other.location` |
| `{vocab:selector}` | vocabulary as a type, selector as a parameter |
| a vocabulary's `: Scope` clause | `entity.name.type.scope` |
| `parameter=` naming the argument an operand feeds | `variable.parameter.operand` |
| `/delay=N` | `storage.modifier.delay` |
| a hole, and a `-` discard | `constant.language` |
| a trailing `\`, joining a line to the next | `keyword.operator.continuation` |

Because the mnemonic column is a format string and the action column is code,
they read differently in any theme: the mnemonic is string-coloured throughout,
with the placeholders picked out, while the action gets the operation, operand
and keyword colours of an ordinary language.

Three things are marked `invalid`, all of them cases the compiler rejects, so
the colour arrives before the build does:

- a line that is neither blank, comment, declaration nor row (the format
  requires every line to mean something, rather than skipping what it cannot
  read);
- an encoding that is not eight pattern characters, on a line that is otherwise
  a row;
- a `#` in the action column. `#` is only special at the start of a line, so a
  trailing comment on a row is not a comment: it becomes part of the action and
  fails to parse as one.

## Testing a change

The grammar is checked by tokenizing the real table and looking at what came
out, which is the only way to be sure about a TextMate grammar:

```sh
npm install vscode-textmate vscode-oniguruma
node tokenize.mjs
```

`tokenize.mjs` prints every token of `z80.cpu` with its scopes; a second
argument filters to tokens whose scopes contain it. Two runs are the actual
test, and both should print nothing but blank lines:

```sh
node tokenize.mjs ../../z80/v4/z80.cpu '<unscoped>'
node tokenize.mjs ../../z80/v4/z80.cpu invalid
```

A filter only shows what you asked about, so "nothing came out" can also mean a
`begin`/`end` block ran away and ate the rest of the file, and nothing inside it
was ever judged. Two checks run whatever the filter says, and make the exit
status non-zero:

- **no line ends inside a block**, unless it ended in a `\` and asked to be
  continued. This is the one that catches a runaway rule.
- **the tokens do not depend on the line terminator.** vscode-textmate appends
  a `\n` to every line it is given, so the editor hands one over without it. A
  grammar that matches newlines can be right one way and wrong the other, so
  both are tried and compared, except across a continuation, where keeping the
  block open is precisely a decision about that newline.

## Limits

This is a grammar, not a parser. It knows the shape of the format but none of
its checks: it cannot tell you that a vocabulary has the wrong number of members
for the slice that selects it, that two rows overlap partially, or that a name
does not resolve; those need the CPU description, and they are what the build
is for. What it can see is one line at a time.

A wrapped line stays inside its declaration or row, so its members and operands
are coloured as they would be unwrapped. What does not survive the wrap is
anything decided by looking backwards along the line: the first word of a step
is an operation because a `|` or `;` precedes it, and after a wrap that
separator is on the line above, so it colours as a location instead. The rows
in `z80.cpu` all fit on a line; wrap one at a `;` and this is what you will see.
