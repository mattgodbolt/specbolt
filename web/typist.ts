import {SpectrumShift, SymbolShift, Spectrum} from "./spectrum";

export class Typist {
    private readonly spectrum: Spectrum;
    // Several events can share a frame: a SYMBOL SHIFT release and the
    // release of the key it shifted land on the same one.
    private readonly onFrame: Map<number, { code: number, pressed: boolean }[]>;
    private frameCounter: number;

    constructor(spectrum: Spectrum, text: string) {
        this.spectrum = spectrum;
        this.frameCounter = 0;
        let index = 0;
        this.onFrame = new Map();
        const at = (frame: number, code: number, pressed: boolean) => {
            const events = this.onFrame.get(frame) ?? [];
            events.push({code, pressed});
            this.onFrame.set(frame, events);
        };
        let frameToSchedule = 10;
        while (index < text.length) {
            let code = text.charCodeAt(index++);
            if (code >= "A".charCodeAt(0) && code <= "Z".charCodeAt(0)) {
                at(frameToSchedule, SpectrumShift, true);
                at(frameToSchedule + 7, SpectrumShift, false);
                frameToSchedule += 2;
                code = code + 32;
            }
            if (code === "^".charCodeAt(0)) {
                at(frameToSchedule, SymbolShift, true);
                at(frameToSchedule + 5, SymbolShift, false);
                frameToSchedule += 2;
                continue;
            }
            if (code === "$".charCodeAt(0))
                code = 13;
            at(frameToSchedule, code, true);
            at(frameToSchedule + 3, code, false);
            frameToSchedule += 15;
        }
    }

    type() {
        this.frameCounter++;
        for (const {code, pressed} of this.onFrame.get(this.frameCounter) ?? [])
            this.spectrum.setKeyState(code, pressed);
    }
}
