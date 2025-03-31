import {Spectrum, SpectrumShift, SymbolShift} from "./spectrum";

const $canvas = document.querySelector("canvas");
const $effectiveMhz = document.querySelector("#effective-mhz");
const $currentFps = document.querySelector("#current-fps");
const $loadGameButton = document.querySelector(".load-game-button");
const $loadGamePopup = document.querySelector(".game-popup");
if ($loadGameButton) {
    $loadGameButton.addEventListener("click", () => { console.log("poo"); });
}

const spectrum = new Spectrum($canvas, document.querySelector("#warning"));

class Typist {
    constructor(text) {
        this.counter = 0;
        let index = 0;
        this.onCycle = new Map();
        let counter = 10;
        while (index < text.length) {
            let code = text.charCodeAt(index++);
            if (code >= "A".charCodeAt(0) && code <= "Z".charCodeAt(0)) {
                this.onCycle.set(counter, {code : SpectrumShift, pressed : true});
                this.onCycle.set(counter + 7, {code : SpectrumShift, pressed : false});
                counter += 2;
                code = code + 32;
            }
            if (code === "^".charCodeAt(0)) {
                this.onCycle.set(counter, {code : SymbolShift, pressed : true});
                this.onCycle.set(counter + 5, {code : SymbolShift, pressed : false});
                counter += 2;
                continue;
            }
            if (code === "$".charCodeAt(0))
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
    document.onkeyup = (e) => { spectrum.onKeyUp(e); };

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

        $canvas.addEventListener("emulated-frame", () => { typist.type(); });
    }
}

initialise().then(() => {});

// JavaScript inside of an iframe embedded within reveal.js
window.addEventListener("message", (event) => {
    if (event.data === "specbolt:start") {
        spectrum.start();
    } else if (event.data === "slide:start") {
        // Does nothing...
    } else if (event.data === "slide:stop" || event.data === "specbolt:stop") {
        spectrum.stop();
    }
});

const ArchiveOrgCors = "https://cors.archive.org";
async function findSpectrumZ80Files(itemId) {
    const metadataUrl = `${ArchiveOrgCors}/metadata/${itemId}`;
    const response = await fetch(metadataUrl);
    const data = await response.json();

    const z80Files =
        data.files.filter((file) => file.name.toLowerCase().endsWith(".z80") || file.format?.toLowerCase() === "z80");

    return z80Files;
}

function getCorsFileDownloadUrl(itemId, fileName) { return `${ArchiveOrgCors}/cors/${itemId}/${fileName}`; }
async function do_the_thing(itemId) {
    const files = await findSpectrumZ80Files(itemId);
    if (files.length === 1) {
        return getCorsFileDownloadUrl(itemId, files[0].name);
    }
}

async function searchForSpectrumEmulator(query) {
    const searchQuery = encodeURI(`title:(${query}) AND collection:(softwarelibrary_zx_spectrum)`);
    const searchUrl = `${ArchiveOrgCors}/advancedsearch.php?q=${searchQuery}&fl[]=identifier,title&output=json`;
    const response = await fetch(searchUrl);
    const data = await response.json();
    // docuemnt: 'identifier', title:
    return data.response.docs;
    //   return data.response.docs.map((doc) => doc.identifier);
}

// console.log(await do_the_thing("zx_Scuba_Dive_1983_Durell_Software_a"));
// const mongoose = await searchForSpectrumEmulator("jet set");
// console.log(mongoose);
