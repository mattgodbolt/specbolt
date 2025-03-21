import {ConsoleStdout, File, OpenFile, PreopenDirectory, WASI} from "@bjorn3/browser_wasi_shim";
import wasmUrl from '@spectrum.wasm?url';

import rom128Url from '../assets/128.rom?url';
import rom48Url from '../assets/48.rom?url';

import {AudioHandler} from "./audio-handler";

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

class WasmSpectrum {
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
    if (code >= 48 && code <= 57 || code === 13 || code === 32) {
        // Numbers, space and enter remain as-is
        return code;
    }
    // Else not mapped.
    return undefined;
}

export class Spectrum {
    constructor(canvas, audioWarningNode) {
        this.canvas = canvas;
        this.canvas2d = canvas.getContext("2d");
        this.audioWarningNode = audioWarningNode;
        this.currentFps = 60;
        this.effectiveMhz = 0;
        this.prevFrameTime = performance.now();
        this.running = false;
        this.nextUpdate = undefined;
    }

    async initialise() {
        const {instance, snapshots} = await initialiseWasm();
        this.wasm = new WasmSpectrum(instance.exports, snapshots);
        this.canvas.setAttribute('width', this.wasm.width);
        this.canvas.setAttribute('height', this.wasm.height);

        this.audioHandler = new AudioHandler(this.audioWarningNode, SampleRate);
        await this.audioHandler.initialise();
    }

    drawFrame(ts) {
        this.currentFps = 1000 / (ts - this.prevFrameTime);
        this.prevFrameTime = ts;
        const frame = this.wasm.render_video();
        this.canvas2d.putImageData(frame, 0, 0);
        if (this.running)
            requestAnimationFrame((ts) => this.drawFrame(ts));
    }

    emulate() {
        const ts = performance.now();
        if (this.nextUpdate === undefined)
            this.nextUpdate = ts;
        if (ts >= this.nextUpdate) {
            const numCycles = this.wasm.run_frame();
            const timeTakenMs = performance.now() - ts;
            this.effectiveMhz = numCycles / timeTakenMs / 1000;
            const audio = this.wasm.render_audio();
            this.audioHandler.postAudio(audio);
            // Schedule the next tick, but don't let us get stupidly behind.
            const msPerUpdate = 1000 / 50;
            const maxMsBehind = 1000;
            this.nextUpdate = Math.max(ts - maxMsBehind, this.nextUpdate + msPerUpdate);
        }
        if (this.running)
            setTimeout(() => this.emulate(), 0);
    }

    start() {
        this.running = true;
        this.nextUpdate = undefined;
        setTimeout(() => this.emulate(), 0);
        requestAnimationFrame((ts) => this.drawFrame(ts));
    }

    stop() { this.running = false; }

    onKeyDown(evt) {
        const code = keyCode(evt);
        if (code !== undefined) {
            this.wasm.key_state(keyCode(evt), true);
            evt.preventDefault();
        } else if (evt.key === "F10") {
            if (this.running)
                this.stop();
            else
                this.start();
            evt.preventDefault();
        }
        this.audioHandler.tryResume().then(() => {});
    }

    onKeyUp(evt) {
        const code = keyCode(evt);
        if (code !== undefined) {
            this.wasm.key_state(keyCode(evt), false);
            evt.preventDefault();
        }
    }

    async load_snapshot(game_url) {
        const game_data = await (await fetch(game_url)).arrayBuffer();
        const file_name = game_url.pathname.replace(/.*\//, '');
        this.wasm.load_snapshot(file_name, game_data);
    }
}
