import _ from "lodash";

import {downloadSnapshotUrlFor, searchForSpectrum} from "./ia-search";
import {Spectrum, SpectrumShift, SymbolShift} from "./spectrum";

const $canvas = document.querySelector("canvas");
const $effectiveMhz = document.querySelector("#effective-mhz");
const $currentFps = document.querySelector("#current-fps");
const $loadGameButton = document.querySelector(".load-game-button");
if ($loadGameButton) {
    const $loadGameDialog = document.querySelector("dialog.game-finder");
    $loadGameButton.addEventListener("click", () => { $loadGameDialog.showModal(); });
    const $searchResults = $loadGameDialog.querySelector(".search-results");
    const $template = $loadGameDialog.querySelector("template").content;

    function updateSearchResults(results) {
        $searchResults.innerHTML = "";
        if (results.length === 0) {
            $searchResults.innerHTML = "<h3>No results</h3>";
        }
        for (const result of results) {
            const newItem = $template.cloneNode(true);
            newItem.querySelector(".description").innerHTML = result.title;
            newItem.querySelector(".description").addEventListener("click", async (evt) => {
                evt.preventDefault();
                $loadGameDialog.requestClose();
                const z80FileUrl = new URL(await downloadSnapshotUrlFor(result.identifier));
                await spectrum.loadSnapshot(z80FileUrl);
                updateUrl({load : z80FileUrl});
            });
            $searchResults.appendChild(newItem);
        }
    }

    const $searchButton = $loadGameDialog.querySelector(".search-input");

    async function doSearch() {
        $searchResults.innerHTML = "<h5>Loading...</h5>";
        const results = await searchForSpectrum($searchButton.value);
        updateSearchResults(results);
    }

    const debouncedSearch = _.debounce(doSearch, 500);
    $searchButton.addEventListener("change", debouncedSearch);
    $searchButton.addEventListener("keyup", debouncedSearch);
}

let _wasPaused = false;

function pause() {
    if (spectrum.running) {
        _wasPaused = true;
        spectrum.stop();
    }
}

function unpause() {
    if (_wasPaused) {
        _wasPaused = false;
        spectrum.start();
    }
}

document.querySelectorAll("dialog").forEach((dialog) => {
    console.log(dialog);
    dialog.addEventListener("toggle", (event) => {
        if (event.newState === "open") {
            pause();
        } else {
            unpause()
        }
    });
});

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

function updateUrl(updateMap) {
    const parsedQuery = new URLSearchParams(window.location.search);
    for (const item of Object.keys(updateMap)) {
        parsedQuery.set(item, updateMap[item]);
    }
    const url = new URL(window.location);
    url.search = parsedQuery.toString();
    window.history.pushState({}, undefined, url.toString());
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
        if (!spectrum.running)
            return;
        spectrum.onKeyDown(e);
    };
    document.onkeyup = (e) => {
        if (!spectrum.running)
            return;
        spectrum.onKeyUp(e);
    };

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
