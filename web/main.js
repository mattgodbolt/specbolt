import {ConsoleStdout, File, OpenFile, PreopenDirectory, WASI} from "@bjorn3/browser_wasi_shim";

import rom128Url from '../assets/128.rom?url';
import rom48Url from '../assets/48.rom?url';
import wasmUrl from '../build/Wasm/web/web.wasm?url'; // TODO relies on calling the build directory "Wasm" in cmake.

import {AudioHandler} from "./audio-handler";

async function initialiseWasm() {
    const rom48 = await (await fetch(rom48Url)).arrayBuffer();
    const rom128 = await (await fetch(rom128Url)).arrayBuffer();
    const wasi = new WASI([], [], [
        new OpenFile(new File([])), // stdin
        ConsoleStdout.lineBuffered(msg => console.log(`[WASI stdout] ${msg}`)),
        ConsoleStdout.lineBuffered(msg => console.warn(`[WASI stderr] ${msg}`)),
        new PreopenDirectory("assets",
                             [
                                 [ "48.rom", new File(rom48) ],
                                 [ "128.rom", new File(rom128) ],
                             ]),
    ]);

    const result = await WebAssembly.instantiateStreaming(fetch(wasmUrl), {"wasi_snapshot_preview1" : wasi.wasiImport});
    wasi.start(result.instance);
    return result.instance;
}

const SampleRate = 48000;

class Spectrum {
    constructor(exports) {
        this._exports = exports;
        this._instance = exports.create(48, SampleRate);
        this.width = exports.video_width();
        this.height = exports.video_height();
    }

    run_frame() { this._exports.run_frame(this._instance); }

    render_video() {
        const video = this._exports.render_video(this._instance);
        const data = new Uint8ClampedArray(this._exports.memory.buffer, video, 4 * this.width * this.height);
        return new ImageData(data, this.width, this.height);
    }

    render_audio() {
        const audio = this._exports.render_audio(this._instance);
        const length = this._exports.last_audio_frame_size(this._instance);
        return new Int16Array(this._exports.memory.buffer, audio, length);
    }

    key_down(code) { this._exports.key_state(this._instance, code, true); }

    key_up(code) { this._exports.key_state(this._instance, code, false); }
}

// fart about to get SDL keycodes from browser keycodes.
function keyCode(evt) {
    const code = evt.which || evt.charCode || evt.keyCode;
    if (code === 16) {     // shift
        return 0x400000e1; // Spectrum Shift
    }
    if (code === 17 || code === 18 || code === 230) { // control or either alt
        return 0x400000e0;                            // Spectrum symbolshift (control)
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

    const $warning = document.querySelector('#warning');
    const audioHandler = new AudioHandler($warning, SampleRate);

    let next_update = 0;
    let running = true;

    function update(ts) {
        if (ts > next_update) {
            spectrum.run_frame();
            const frame = spectrum.render_video();
            const ctx = $canvas.getContext("2d");
            ctx.putImageData(frame, 0, 0);
            const audio = spectrum.render_audio();
            audioHandler.postAudio(audio);

            next_update = next_update + 20;
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
