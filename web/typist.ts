import {SpectrumShift, SymbolShift, Spectrum} from "./spectrum";

export class Typist {
    private readonly spectrum: Spectrum;
    private readonly onCycle: Map<number, { code: number, pressed: boolean }>;
    private counter: number;

    constructor(spectrum: Spectrum, text: string) {
        this.spectrum = spectrum;
        this.counter = 0;
        let index = 0;
        this.onCycle = new Map();
        let counter = 10;
        while (index < text.length) {
            let code = text.charCodeAt(index++);
            if (code >= "A".charCodeAt(0) && code <= "Z".charCodeAt(0)) {
                this.onCycle.set(counter, {code: SpectrumShift, pressed: true});
                this.onCycle.set(counter + 7, {code: SpectrumShift, pressed: false});
                counter += 2;
                code = code + 32;
            }
            if (code === "^".charCodeAt(0)) {
                this.onCycle.set(counter, {code: SymbolShift, pressed: true});
                this.onCycle.set(counter + 5, {code: SymbolShift, pressed: false});
                counter += 2;
                continue;
            }
            if (code === "$".charCodeAt(0))
                code = 13;
            this.onCycle.set(counter, {code: code, pressed: true});
            this.onCycle.set(counter + 3, {code: code, pressed: false});
            counter += 15;
        }
    }

    type() {
        this.counter++;
        const todo = this.onCycle.get(this.counter);
        if (todo)
            this.spectrum.setKeyState(todo.code, todo.pressed);
    }
}
