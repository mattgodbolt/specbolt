import {Spectrum} from "./spectrum";

const $canvas = document.querySelector('canvas');
const $effectiveMhz = document.querySelector('#effective-mhz');
const $currentFps = document.querySelector('#current-fps');

const spectrum = new Spectrum($canvas, document.querySelector('#warning'));

async function initialise() {
    await spectrum.initialise();

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

    const parsedQuery = new URLSearchParams(window.location.search);
    if (parsedQuery.get("load")) {
        const game_url = new URL(parsedQuery.get("load"), window.location);
        await spectrum.loadSnapshot(game_url);
    }
    if (!parsedQuery.get("autostart"))
        spectrum.start();
    else
        startOnKeyPress = true;
}

initialise().then(() => {});

// JavaScript inside of an iframe embedded within reveal.js
window.addEventListener('message', (event) => {
    if (event.data === 'slide:start') {
        // Does nothihng...
    } else if (event.data === 'slide:stop') {
        spectrum.stop();
    }
});
