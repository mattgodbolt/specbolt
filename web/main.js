import {ConsoleStdout, File, OpenFile, PreopenDirectory, WASI} from "@bjorn3/browser_wasi_shim";

async function initialiseWasm() {
    const rom48 = await (await fetch('roms/48.rom')).arrayBuffer();
    const wasi = new WASI([], [], [
        new OpenFile(new File([])), // stdin
        ConsoleStdout.lineBuffered(msg => console.log(`[WASI stdout] ${msg}`)),
        ConsoleStdout.lineBuffered(msg => console.warn(`[WASI stderr] ${msg}`)),
        new PreopenDirectory(".", [ [ "48.rom", new File(rom48) ] ]),
    ]);

    const result =
        await WebAssembly.instantiateStreaming(fetch('web.wasm'), {"wasi_snapshot_preview1" : wasi.wasiImport});
    wasi.start(result.instance);
    return result.instance;
}

class Spectrum {
    constructor(exports) {
        this._exports = exports;
        this._instance = exports.create();
        this.width = exports.video_width();
        this.height = exports.video_height();
    }

    run_frame() { this._exports.run_frame(this._instance); }

    render_video() {
        const video = this._exports.render_video(this._instance);
        const data = new Uint8ClampedArray(this._exports.memory.buffer, video, 4 * this.width * this.height);
        return new ImageData(data, this.width, this.height);
    }

    key_down(code) { this._exports.key_state(this._instance, code, true); }

    key_up(code) { this._exports.key_state(this._instance, code, false); }
}

// fart about to get SDL keycodes from browser keycodes.
function keyCode(evt) {
    const code = evt.which || evt.charCode || evt.keyCode;
    if (code === 16) {
        return 0x400000e1; // Shift
    }
    if (code === 17) {
        return 0x400000e0; // control
    }
    // Map uppercase to lower.
    if (code >= 65 && code <= 90) {
        return code + 32;
    }
    return code;
}

const $canvas = document.querySelector('canvas');
let spectrum;

async function initialise() {
    const instance = await initialiseWasm();
    spectrum = new Spectrum(instance.exports);

    $canvas.setAttribute('width', spectrum.width);
    $canvas.setAttribute('height', spectrum.height);

    let next_update = 0;
    let running = true;

    function update(ts) {
        if (ts > next_update) {
            spectrum.run_frame();
            const frame = spectrum.render_video();
            const ctx = $canvas.getContext("2d");
            ctx.putImageData(frame, 0, 0);
            next_update = ts + 20;
        }
        if (running)
            requestAnimationFrame(update);
    }

    requestAnimationFrame(update);

    document.onkeydown =
        (e) => {
            spectrum.key_down(keyCode(e));
            e.preventDefault();
        }

               document.onkeyup = (e) => {
            spectrum.key_up(keyCode(e));
            e.preventDefault();
        }
}

initialise().then(() => {});
