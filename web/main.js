import {Spectrum} from "./spectrum";

const $canvas = document.querySelector('canvas');
const $effectiveMhz = document.querySelector('#effective-mhz');
const $currentFps = document.querySelector('#current-fps');

const spectrum = new Spectrum($canvas, document.querySelector('#warning'));

async function initialise() {
    await spectrum.initialise();

    function updateStats() {
        $effectiveMhz.innerHTML = `${spectrum.effectiveMhz.toFixed(2)} MHz`;
        $currentFps.innerHTML = `${spectrum.currentFps.toFixed(2)} fps`;
    }

    updateStats();
    setInterval(updateStats, 1000);

    document.onkeydown = (e) => { spectrum.onKeyDown(e); };

    document.onkeyup = (e) => { spectrum.onKeyUp(e); }

    const parsedQuery = new URLSearchParams(window.location.search);
    if (parsedQuery.get("load")) {
        const game_url = new URL(parsedQuery.get("load"), window.location);
        await spectrum.load_snapshot(game_url);
    }
    spectrum.start();
}

initialise().then(() => {});
