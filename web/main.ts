import {Spectrum} from "./spectrum";
import {Typist} from "./typist";
import {setupLoadGame} from "./ui";

const $canvas = document.querySelector("canvas");
const spectrum = new Spectrum($canvas, document.querySelector("#warning"));

document.querySelectorAll("dialog").forEach((dialog) => {
    dialog.addEventListener("toggle", (event: ToggleEvent) => {
        if (event.newState === "open") {
            pause();
        } else {
            unpause();
        }
    });
});

const $loadGameButton = document.querySelector(".load-game-button");
if ($loadGameButton) {
    const loadGameDialog: HTMLDialogElement = document.querySelector("dialog.game-finder");
    $loadGameButton.addEventListener("click", () => {
        loadGameDialog.showModal();
    });
    setupLoadGame(spectrum, loadGameDialog);
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

async function initialise() {
    const parsedQuery = new URLSearchParams(window.location.search);
    const $effectiveMhz = document.querySelector("#effective-mhz");
    const $currentFps = document.querySelector("#current-fps");

    await spectrum.initialise(parseInt(parsedQuery.get("model")) || 48);

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
        const game_url = new URL(parsedQuery.get("load"), window.location.toString());
        await spectrum.loadSnapshot(game_url);
    }
    if (parsedQuery.get("tape")) {
        const game_url = new URL(parsedQuery.get("tape"), window.location.toString());
        await spectrum.loadTape(game_url);
    }

    for (let i = 0; i < 80; ++i)
        spectrum.emulateFrame();

    if (!parsedQuery.get("autostart"))
        spectrum.start();
    else
        startOnKeyPress = true;

    if (parsedQuery.get("type")) {
        const typist = new Typist(spectrum, parsedQuery.get("type"));

        $canvas.addEventListener("emulated-frame", () => {
            typist.type();
        });
    }
}

initialise().then(() => {
});

window.addEventListener("message", (event) => {
    if (event.data === "specbolt:start") {
        spectrum.start();
    } else if (event.data === "slide:start") {
        // Embedded inside reveal.js start slide: Does nothing...
    } else if (event.data === "slide:stop" || event.data === "specbolt:stop") {
        // Embedded inside reveal.js stop slide: stop the emulator.
        spectrum.stop();
    }
});
