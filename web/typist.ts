import {SpectrumShift, SymbolShift, Spectrum} from "./spectrum";

export class Typist {
    private readonly spectrum: Spectrum;
    private readonly onFrame: Map<number, { code: number, pressed: boolean }>;
    private frameCounter: number;

    constructor(spectrum: Spectrum, text: string) {
        this.spectrum = spectrum;
        this.frameCounter = 0;
        let index = 0;
        this.onFrame = new Map();
        let frameToSchedule = 10;
        while (index < text.length) {
            let code = text.charCodeAt(index++);
            if (code >= "A".charCodeAt(0) && code <= "Z".charCodeAt(0)) {
                this.onFrame.set(frameToSchedule, {code: SpectrumShift, pressed: true});
                this.onFrame.set(frameToSchedule + 7, {code: SpectrumShift, pressed: false});
                frameToSchedule += 2;
                code = code + 32;
            }
            if (code === "^".charCodeAt(0)) {
                this.onFrame.set(frameToSchedule, {code: SymbolShift, pressed: true});
                this.onFrame.set(frameToSchedule + 5, {code: SymbolShift, pressed: false});
                frameToSchedule += 2;
                continue;
            }
            if (code === "$".charCodeAt(0))
                code = 13;
            this.onFrame.set(frameToSchedule, {code: code, pressed: true});
            this.onFrame.set(frameToSchedule + 3, {code: code, pressed: false});
            frameToSchedule += 15;
        }
    }

    type() {
        this.frameCounter++;
        const todo = this.onFrame.get(this.frameCounter);
        if (todo)
            this.spectrum.setKeyState(todo.code, todo.pressed);
    }
}
