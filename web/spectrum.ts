import {ConsoleStdout, File, Inode, OpenFile, PreopenDirectory, WASI} from "@bjorn3/browser_wasi_shim";
// @ts-ignore
import wasmUrl from '@spectrum.wasm?url';

// @ts-ignore
import rom128Url from '../assets/128.rom?url';
// @ts-ignore
import rom48Url from '../assets/48.rom?url';

import {AudioHandler} from "./audio-handler";

async function initialiseWasm() {
    const rom48 = await (await fetch(rom48Url)).arrayBuffer();
    const rom128 = await (await fetch(rom128Url)).arrayBuffer();
    const data = new Map();
    const wasi = new WASI([], [], [
        new OpenFile(new File([])), // stdin
        ConsoleStdout.lineBuffered(msg => console.log(`[WASI stdout] ${msg}`)),
        ConsoleStdout.lineBuffered(msg => console.warn(`[WASI stderr] ${msg}`)),
        new PreopenDirectory("assets", new Map([
            ["48.rom", new File(rom48)],
            ["128.rom", new File(rom128)],
        ])),
        new PreopenDirectory("data", data)
    ]);

    const result = await WebAssembly.instantiateStreaming(fetch(wasmUrl), {"wasi_snapshot_preview1": wasi.wasiImport});
    wasi.start(result.instance as any);
    return {instance: result.instance, data};
}

const SampleRate = 48000;

class WasmSpectrum {
    private readonly _instance: number;
    readonly width: any;
    readonly height: any;
    private readonly _exports: any;
    private readonly data_map: Map<string, Inode>;

    constructor(exports: Record<string, any>, model: number, data_map: Map<string, Inode>) {
        this._exports = exports;
        this._instance = exports.create(model, SampleRate);
        this.width = exports.video_width();
        this.height = exports.video_height();
        this.data_map = data_map;
    }

    _alloc_string(name: string): number {
        const length = name.length + 1;
        const offset = this._exports.alloc_bytes(length);
        const buffer = new Uint8Array(this._exports.memory.buffer, offset, length);
        for (let index = 0; index < name.length; ++index) {
            buffer[index] = name.charCodeAt(index);
        }
        buffer[length - 1] = 0;
        return offset;
    }

    _free_string(offset: number) {
        this._exports.free_bytes(offset);
    }

    load_snapshot(name: string, data: ArrayBuffer) {
        this.data_map.set(name, new File(data));
        const offset = this._alloc_string(`data/${name}`);
        this._exports.load_snapshot(this._instance, offset);
        this._free_string(offset);
    }

    load_tape(name: string, data: ArrayBuffer) {
        this.data_map.set(name, new File(data));
        const offset = this._alloc_string(`data/${name}`);
        this._exports.load_tape(this._instance, offset);
        this._free_string(offset);
    }

    run_frame() {
        return this._exports.run_frame(this._instance);
    }

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

    key_state(code: number, pressed: boolean) {
        this._exports.key_state(this._instance, code, pressed);
    }
}

export const SpectrumShift = 0x400000e1;
export const SymbolShift = 0x400000e0;

// fart about to get SDL keycodes from browser keycodes.
function keyCode(evt: KeyboardEvent) {
    // Drop any ctrl keypresses.
    if (evt.ctrlKey)
        return undefined;
    const code = evt.which || evt.charCode || evt.keyCode;
    // Shift to spectrum shift
    if (code === 16) { // shift
        return SpectrumShift;
    }
    // either alt to symbolshift
    if (code === 18 || code === 230) {
        return SymbolShift;
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

const emulatedFrameEvent = new Event("emulated-frame");

export class Spectrum {
    private readonly canvas: HTMLCanvasElement;
    private readonly canvas2d: CanvasRenderingContext2D;
    private readonly audioWarningNode: HTMLElement;
    currentFps: number;
    effectiveMhz: number;
    prevFrameTime: number;
    running: boolean;
    nextUpdate: any;
    private wasm: WasmSpectrum;
    private audioHandler: AudioHandler;

    constructor(canvas: HTMLCanvasElement, audioWarningNode: HTMLElement) {
        this.canvas = canvas;
        this.canvas2d = canvas.getContext("2d");
        this.audioWarningNode = audioWarningNode;
        this.currentFps = 60;
        this.effectiveMhz = 0;
        this.prevFrameTime = performance.now();
        this.running = false;
        this.nextUpdate = undefined;
    }

    async initialise(model: number) {
        const {instance, data} = await initialiseWasm();
        this.wasm = new WasmSpectrum(instance.exports, model, data);
        this.canvas.setAttribute('width', this.wasm.width);
        this.canvas.setAttribute('height', this.wasm.height);

        this.audioHandler = new AudioHandler(this.audioWarningNode, SampleRate);
        await this.audioHandler.initialise();
    }

    blitSpectrumFrame() {
        this.canvas2d.putImageData(this.wasm.render_video(), 0, 0);
    }

    drawFrame(ts: number) {
        this.currentFps = 1000 / (ts - this.prevFrameTime);
        this.prevFrameTime = ts;
        this.blitSpectrumFrame();
        if (this.running)
            requestAnimationFrame((ts) => this.drawFrame(ts));
    }

    emulateFrame() {
        this.canvas.dispatchEvent(emulatedFrameEvent);
        return this.wasm.run_frame();
    }

    emulate() {
        const ts = performance.now();
        if (this.nextUpdate === undefined)
            this.nextUpdate = ts;
        if (ts >= this.nextUpdate) {
            const numCycles = this.emulateFrame();
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
        if (this.running)
            return;
        this.running = true;
        this.nextUpdate = undefined;
        setTimeout(() => this.emulate(), 0);
        requestAnimationFrame((ts) => this.drawFrame(ts));
    }

    stop() {
        this.running = false;
    }

    setKeyState(code: number, pressed: boolean) {
        this.wasm.key_state(code, pressed);
    }

    onKeyDown(evt: KeyboardEvent) {
        const code = keyCode(evt);
        if (code !== undefined) {
            this.setKeyState(code, true);
            evt.preventDefault();
        } else if (evt.key === "F10") {
            if (this.running)
                this.stop();
            else
                this.start();
            evt.preventDefault();
        }
        this.audioHandler.tryResume().then(() => {
        });
    }

    onKeyUp(evt: KeyboardEvent) {
        const code = keyCode(evt);
        if (code !== undefined) {
            this.setKeyState(code, false);
            evt.preventDefault();
        }
    }

    static async loadUrl(url: URL) {
        const filename = url.pathname.replace(/.*\//, '');
        return {filename, data: await (await fetch(url)).arrayBuffer()};
    }

    async loadTape(tape_url: URL) {
        const {filename, data} = await Spectrum.loadUrl(tape_url);
        this.wasm.load_tape(filename, data);
    }

    async loadSnapshot(game_url: URL) {
        const {filename, data} = await Spectrum.loadUrl(game_url);
        this.wasm.load_snapshot(filename, data);
        this.wasm.run_frame();
        this.blitSpectrumFrame();
    }
}
