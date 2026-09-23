// Boots the web build's spectrum.wasm under Node through the same exports web/spectrum.ts uses in a browser, runs it
// for a number of frames, and writes the screen out as a PPM. A check that a wasm build works end to end without a
// browser, and a rough measure of how fast it runs. Run from the checkout, since the ROMs are read from `assets/`.
//
//   node web/tools/boot.mjs build/wasm/web/spectrum.wasm [--model 48|128] [--frames N] [--out screen.ppm]
import {readFile, writeFile} from "node:fs/promises";
import process from "node:process";
import {parseArgs} from "node:util";
import {WASI} from "node:wasi";

const {values, positionals} = parseArgs({
    allowPositionals : true,
    options : {
        model : {type : "string", default : "48"},
        frames : {type : "string", default : "200"},
        out : {type : "string", default : "screen.ppm"},
    },
});
const [wasmPath] = positionals;
if (!wasmPath) {
    console.error("usage: node boot.mjs <spectrum.wasm> [--model 48|128] [--frames N] [--out screen.ppm]");
    process.exit(2);
}

// The page gives the module stdio and an `assets` directory holding the ROMs; this does the same from the checkout.
const wasi = new WASI({version : "preview1", args : [], env : {}, preopens : {assets : `${process.cwd()}/assets`}});
const {instance} = await WebAssembly.instantiate(await readFile(wasmPath), wasi.getImportObject());
wasi.start(instance);
const exports = instance.exports;

const spectrum = exports.create(Number(values.model), 48000);
if (!spectrum) {
    console.error(`no such model: ${values.model}`);
    process.exit(1);
}
const width = exports.video_width();
const height = exports.video_height();
const frames = Number(values.frames);
const started = process.hrtime.bigint();
for (let frame = 0; frame < frames; ++frame)
    exports.run_frame(spectrum);
const elapsed = Number(process.hrtime.bigint() - started) / 1e6;

// Each pixel is RGBA in memory order, as the page hands it to an ImageData.
const pixels = new Uint8Array(exports.memory.buffer, exports.render_video(spectrum), 4 * width * height);
const rgb = Buffer.alloc(3 * width * height);
for (let at = 0; at < width * height; ++at)
    rgb.set(pixels.subarray(4 * at, 4 * at + 3), 3 * at);
await writeFile(values.out, Buffer.concat([ Buffer.from(`P6 ${width} ${height} 255\n`), rgb ]));
console.log(`${frames} frames in ${elapsed.toFixed(1)} ms (${(elapsed / frames).toFixed(3)} ms a frame, ` +
            `${(20 * frames / elapsed).toFixed(1)}x real time); screen written to ${values.out}`);
