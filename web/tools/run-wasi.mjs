// Runs a wasm32-wasip1 command module under Node's WASI, passing on its arguments and exit code, so a test or benchmark
// built for wasm runs from a shell or from ctest the way its native build does. The guest's root is the current
// directory, so relative paths such as `z80/test/zexdoc.com` behave as they would natively.
//
//   node web/tools/run-wasi.mjs build/wasm/z80/v4/test/z80_v4_test [args...]
import {readFile} from "node:fs/promises";
import process from "node:process";
import {WASI} from "node:wasi";

const [, , path, ...args] = process.argv;
if (!path) {
    console.error("usage: node run-wasi.mjs <module.wasm> [args...]");
    process.exit(2);
}
const wasi = new WASI({version : "preview1", args : [ path, ...args ], env : {}, preopens : {"/" : process.cwd()}});
const module = await WebAssembly.compile(await readFile(path));
const instance = await WebAssembly.instantiate(module, wasi.getImportObject());
process.exit(wasi.start(instance));
