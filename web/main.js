import {ConsoleStdout, File, OpenFile, PreopenDirectory, WASI} from "@bjorn3/browser_wasi_shim";
import wasmUrl from '@spectrum.wasm?url';

import rom128Url from '../assets/128.rom?url';
import rom48Url from '../assets/48.rom?url';

import {AudioHandler} from "./audio-handler";

const $canvas = document.querySelector('canvas');
const $effectiveMhz = document.querySelector('#effective-mhz');
const $currentFps = document.querySelector('#current-fps');

async function initialiseWasm() {
    const rom48 = await (await fetch(rom48Url)).arrayBuffer();
    const rom128 = await (await fetch(rom128Url)).arrayBuffer();
    const snapshots = new Map();
    const wasi = new WASI([], [], [
        new OpenFile(new File([])), // stdin
        ConsoleStdout.lineBuffered(msg => console.log(`[WASI stdout] ${msg}`)),
        ConsoleStdout.lineBuffered(msg => console.warn(`[WASI stderr] ${msg}`)),
        new PreopenDirectory("assets", new Map([
                                 [ "48.rom", new File(rom48) ],
                                 [ "128.rom", new File(rom128) ],
                             ])),
        new PreopenDirectory("snapshots", snapshots)
    ]);

    const result = await WebAssembly.instantiateStreaming(fetch(wasmUrl), {"wasi_snapshot_preview1" : wasi.wasiImport});
    wasi.start(result.instance);
    return {instance : result.instance, snapshots};
}

const SampleRate = 48000;

class Spectrum {
    constructor(exports, snapshots) {
        this._exports = exports;
        this._instance = exports.create(48, SampleRate);
        this.width = exports.video_width();
        this.height = exports.video_height();
        this.snapshots = snapshots;
    }

    _alloc_string(name) {
        const length = name.length + 1;
        const offset = this._exports.alloc_bytes(length);
        const buffer = new Uint8Array(this._exports.memory.buffer, offset, length);
        for (let index = 0; index < name.length; ++index) {
            buffer[index] = name.charCodeAt(index);
        }
        buffer[length - 1] = 0;
        return offset;
    }

    _free_string(offset) { this._exports.free_bytes(offset); }

    load_snapshot(name, data) {
        this.snapshots.set(name, new File(data));
        const offset = this._alloc_string(`snapshots/${name}`);
        this._exports.load_snapshot(this._instance, offset);
        this._free_string(offset);
    }

    run_frame() { return this._exports.run_frame(this._instance); }

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

    key_state(code, pressed) { this._exports.key_state(this._instance, code, pressed); }
}

// fart about to get SDL keycodes from browser keycodes.
function keyCode(evt) {
    // Drop any ctrl keypresses.
    if (evt.ctrlKey)
        return undefined;
    const code = evt.which || evt.charCode || evt.keyCode;
    // Shift to spectrum shift
    if (code === 16) {     // shift
        return 0x400000e1; // Spectrum Shift
    }
    // either alt to symbolshift
    if (code === 18 || code === 230) {
        return 0x400000e0;
    }
    // Map uppercase to lower.
    if (code >= 65 && code <= 90) {
        return code + 32;
    }
    if (code >= 48 && code <= 57 || code === 13) {
        // Numbers and enter remain as-is
        return code;
    }
    // Else not mapped.
    return undefined;
}

let spectrum;

async function load_snapshot(game_url) {
    const game_data = await (await fetch(game_url)).arrayBuffer();
    const file_name = game_url.pathname.replace(/.*\//, '');
    spectrum.load_snapshot(file_name, game_data);
}

async function initialise() {
    const {instance, snapshots} = await initialiseWasm();
    spectrum = new Spectrum(instance.exports, snapshots);

    $canvas.setAttribute('width', spectrum.width);
    $canvas.setAttribute('height', spectrum.height);

    const $warning = document.querySelector('#warning');
    const audioHandler = new AudioHandler($warning, SampleRate);
    await audioHandler.initialise();

    let next_update = undefined;
    let running = true;

    let effectiveMhz = 3.5;
    let currentFps = 30;

    let prevFrameTime = performance.now();
    let pendingFrame;
    function drawFrame(ts) {
        pendingFrame = undefined;
        currentFps = 1000 / (ts - prevFrameTime);
        prevFrameTime = ts;
        const frame = spectrum.render_video();
        const ctx = $canvas.getContext("2d");
        ctx.putImageData(frame, 0, 0);
    }

    function emulate() {
        const ts = performance.now();
        if (next_update === undefined)
            next_update = ts;
        if (ts > next_update) {
            const numCycles = spectrum.run_frame();
            const timeTakenMs = performance.now() - ts;
            effectiveMhz = numCycles / timeTakenMs / 1000;
            const audio = spectrum.render_audio();
            audioHandler.postAudio(audio);
            if (!pendingFrame) {
                // Schedule drawing the results at next vsync.
                pendingFrame = requestAnimationFrame(drawFrame);
            }
            // Schedule the next tick, but don't let us get stupidly behind.
            const msPerUpdate = 1000 / 50;
            const maxMsBehind = 1000;
            next_update = Math.max(ts - maxMsBehind, next_update + msPerUpdate);
        }
        if (running)
            setTimeout(emulate, 0);
    }

    function start() {
        running = true;
        next_update = undefined;
        setTimeout(emulate, 0);
    }

    function stop() { running = false; }

    function updateStats() {
        $effectiveMhz.innerHTML = `${effectiveMhz.toFixed(2)} MHz`;
        $currentFps.innerHTML = `${currentFps.toFixed(2)} fps`;
    }
    updateStats();
    setInterval(updateStats, 1000);

    start();

    document.onkeydown = (e) => {
        const code = keyCode(e);
        if (code !== undefined) {
            spectrum.key_state(keyCode(e), true);
            e.preventDefault();
        } else if (e.key === "F10") {
            if (running)
                stop();
            else
                start();
            e.preventDefault();
        }
    };

    document.onkeyup =
        (e) => {
            const code = keyCode(e);
            if (code !== undefined) {
                spectrum.key_state(keyCode(e), false);
                e.preventDefault();
            }
        }

    const parsedQuery = new URLSearchParams(window.location.search);
    if (parsedQuery.get("load")) {
        const game_url = new URL(parsedQuery.get("load"), window.location);
        await load_snapshot(game_url);
    }
}

initialise().then(() => {});
