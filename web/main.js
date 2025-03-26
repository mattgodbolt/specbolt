import {Spectrum, SpectrumShift, SymbolShift} from "./spectrum";

const $canvas = document.querySelector('canvas');
const $effectiveMhz = document.querySelector('#effective-mhz');
const $currentFps = document.querySelector('#current-fps');

const spectrum = new Spectrum($canvas, document.querySelector('#warning'));

class Typist {
    constructor(text) {
        this.counter = 0;
        let index = 0;
        this.onCycle = new Map();
        let counter = 10;
        while (index < text.length) {
            let code = text.charCodeAt(index++);
            if (code >= 'A'.charCodeAt(0) && code <= 'Z'.charCodeAt(0)) {
                this.onCycle.set(counter, {code : SpectrumShift, pressed : true});
                this.onCycle.set(counter + 7, {code : SpectrumShift, pressed : false});
                counter += 2;
                code = code + 32;
            }
            if (code === '^'.charCodeAt(0)) {
                this.onCycle.set(counter, {code : SymbolShift, pressed : true});
                this.onCycle.set(counter + 5, {code : SymbolShift, pressed : false});
                counter += 2;
                continue;
            }
            if (code === '$'.charCodeAt(0))
                code = 13;
            this.onCycle.set(counter, {code : code, pressed : true});
            this.onCycle.set(counter + 3, {code : code, pressed : false});
            counter += 15;
        }
    }

    type() {
        this.counter++;
        const todo = this.onCycle.get(this.counter);
        if (todo)
            spectrum.setKeyState(todo.code, todo.pressed);
    }
}

async function initialise() {
    const parsedQuery = new URLSearchParams(window.location.search);

    await spectrum.initialise(parsedQuery.get("model") || 48);

    function updateStats() {
        if ($effectiveMhz)
            $effectiveMhz.innerHTML = `${spectrum.effectiveMhz.toFixed(2)} MHz`;
        if ($currentFps)
            $currentFps.innerHTML = `${spectrum.currentFps.toFixed(2)} fps`;
    }

    updateStats();
    setInterval(updateStats, 1000);

    let startOnKeyPress = false;

    document.onkeydown = (e) => {
        if (startOnKeyPress) {
            spectrum.start();
        }
        spectrum.onKeyDown(e);
    };
    document.onkeyup = (e) => { spectrum.onKeyUp(e); }

    if (parsedQuery.get("load")) {
        const game_url = new URL(parsedQuery.get("load"), window.location);
        await spectrum.loadSnapshot(game_url);
    }
    if (parsedQuery.get("tape")) {
        const game_url = new URL(parsedQuery.get("tape"), window.location);
        await spectrum.loadTape(game_url);
    }

    for (let i = 0; i < 80; ++i)
        spectrum.emulateFrame();

    if (!parsedQuery.get("autostart"))
        spectrum.start();
    else
        startOnKeyPress = true;

    if (parsedQuery.get("type")) {
        const typist = new Typist(parsedQuery.get("type"));

        $canvas.addEventListener('emulated-frame', () => { typist.type(); });
    }
}

initialise().then(() => {});

// JavaScript inside of an iframe embedded within reveal.js
window.addEventListener('message', (event) => {
    if (event.data === 'specbolt:start') {
        spectrum.start();
    } else if (event.data === 'slide:start') {
        // Does nothing...
    } else if (event.data === 'slide:stop' || event.data === 'specbolt:stop') {
        spectrum.stop();
    }
});
