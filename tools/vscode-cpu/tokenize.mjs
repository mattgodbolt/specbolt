// Tokenize a `.cpu` file with this extension's grammar and print every token
// with the scopes it landed in. Looking at the output is the test: a TextMate
// grammar has no other way of saying whether it did what was meant.
//
//   npm install vscode-textmate vscode-oniguruma
//   node tokenize.mjs                       # the whole of z80.cpu
//   node tokenize.mjs some.cpu invalid      # only tokens whose scopes match

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

let ruleStack = vsctm.INITIAL;
for (const [at, line] of fs.readFileSync(filePath, 'utf8').split('\n').entries()) {
  const result = grammar.tokenizeLine(line, ruleStack);
  ruleStack = result.ruleStack;
  const tokens = result.tokens
    .map((token) => {
      const text = line.substring(token.startIndex, token.endIndex);
      const scopes = token.scopes.filter((scope) => scope !== 'source.cpu').join(' ');
      return `${JSON.stringify(text)} => ${scopes || '<unscoped>'}`;
    })
    .filter((token) => !filter || token.includes(filter));
  if (tokens.length) {
    console.log(`--- ${at + 1}: ${line}`);
    for (const token of tokens) console.log(`    ${token}`);
  }
}
