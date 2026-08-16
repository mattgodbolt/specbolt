// Tokenize a `.cpu` file with this extension's grammar and print every token
// with the scopes it landed in. Looking at the output is the test: a TextMate
// grammar has no other way of saying whether it did what was meant.
//
//   npm install vscode-textmate vscode-oniguruma
//   node tokenize.mjs                       # the whole of z80.cpu
//   node tokenize.mjs some.cpu invalid      # only tokens whose scopes match
//
// A filtered run only ever shows the tokens you asked about, so it cannot tell
// you that a `begin`/`end` block ran away and swallowed the rest of the file,
// which looks exactly like "no bad tokens". The two checks below say so out
// loud, and the exit status is non-zero when either fires.

import * as fs from 'fs';
import { createRequire } from 'module';

const require = createRequire(import.meta.url);
const vsctm = require('vscode-textmate');
const oniguruma = require('vscode-oniguruma');

const here = new URL('.', import.meta.url).pathname;
const grammarPath = `${here}syntaxes/cpu.tmLanguage.json`;
const filePath = process.argv[2] ?? `${here}../../z80/v4/z80.cpu`;
const filter = process.argv[3];

const wasm = fs.readFileSync(require.resolve('vscode-oniguruma/release/onig.wasm'));
const onigLib = oniguruma.loadWASM(wasm.buffer).then(() => ({
  createOnigScanner: (sources) => new oniguruma.OnigScanner(sources),
  createOnigString: (source) => new oniguruma.OnigString(source),
}));

const registry = new vsctm.Registry({
  onigLib,
  loadGrammar: async () => vsctm.parseRawGrammar(fs.readFileSync(grammarPath, 'utf8'), grammarPath),
});

const grammar = await registry.loadGrammar('source.cpu');

const text = fs.readFileSync(filePath, 'utf8');
// A trailing newline ends the last line; it does not start an empty one.
const lines = text.split('\n');
if (lines.length > 1 && lines.at(-1) === '') lines.pop();

// vscode-textmate appends a `\n` to whatever it is handed (`Grammar._tokenize`
// does `lineText += '\n'` unconditionally), so a line must arrive *without* its
// terminator, which is how VS Code hands over a model line. Passing one with a
// `\n` gives the grammar two of them, and a rule whose `end` matches a newline
// then closes one line later than it does in the editor. Tokenizing both ways
// and comparing is the cheapest way to notice a grammar that depends on which
// it got: in the editor it will only ever get the first.
const tokenize = (terminator) => {
  let ruleStack = vsctm.INITIAL;
  return lines.map((line) => {
    const result = grammar.tokenizeLine(line + terminator, ruleStack);
    ruleStack = result.ruleStack;
    const tokens = result.tokens
      // A rule may match into the newline the library added, so a token can
      // reach past the end of the line, or lie entirely beyond it. Only the
      // part you can see is worth printing, or comparing.
      .map((token) => ({
        text: line.substring(token.startIndex, Math.min(token.endIndex, line.length)),
        scopes: token.scopes.filter((scope) => scope !== 'source.cpu').join(' '),
      }))
      .filter(({ text }) => text !== '')
      .map(({ text, scopes }) => `${JSON.stringify(text)} => ${scopes || '<unscoped>'}`);
    return { tokens, depth: ruleStack.depth };
  });
};

const asEditorDoes = tokenize('');
const withTerminator = tokenize('\n');

for (const [at, { tokens }] of asEditorDoes.entries()) {
  const shown = tokens.filter((token) => !filter || token.includes(filter));
  if (shown.length) {
    console.log(`--- ${at + 1}: ${lines[at]}`);
    for (const token of shown) console.log(`    ${token}`);
  }
}

const warnings = [];

// Depth 1 is the grammar's own top level. A `.cpu` line is a whole thing in
// itself, so the only line allowed to leave a block open is one that ends in a
// backslash and has asked to be continued. Anything else left open is a rule
// eating the lines below it, and a line inside a rule that was never meant to
// hold it is highlighted as whatever that rule says: it cannot be reported as
// wrong, however wrong it is. That is invisible in a filtered run, so say it.
const continued = /\\[ \t\r]*$/;
const stillOpen = asEditorDoes
  .map(({ depth }, at) => ({ depth, at }))
  .filter(({ depth, at }) => depth > 1 && !continued.test(lines[at]));
if (stillOpen.length) {
  const { at } = stillOpen[0];
  warnings.push(
    `${stillOpen.length} line(s) end inside a block that should have closed; the first is ` +
      `line ${at + 1} (depth ${stillOpen[0].depth}): ${JSON.stringify(lines[at])}`,
  );
}

// Continued lines are exempt, and have to be: keeping a block open across one
// is the grammar deciding what to do about the newline it was given, so being
// given a second one is bound to change the answer. Everywhere else the two
// runs should agree, and a rule that closes a line late will say so here.
const joined = lines.map((line, at) => continued.test(line) || (at > 0 && continued.test(lines[at - 1])));
const differs = asEditorDoes.findIndex(
  ({ tokens }, at) => !joined[at] && JSON.stringify(tokens) !== JSON.stringify(withTerminator[at].tokens),
);
if (differs !== -1)
  warnings.push(
    `line ${differs + 1} tokenizes differently with and without a trailing newline, ` +
      `so the grammar depends on one the editor never sends:\n` +
      `    without: ${asEditorDoes[differs].tokens.join('\n             ')}\n` +
      `    with:    ${withTerminator[differs].tokens.join('\n             ')}`,
  );

for (const warning of warnings) console.error(`warning: ${warning}`);
if (warnings.length) process.exit(1);
