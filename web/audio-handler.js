import rendererUrl from "./audio-renderer.js?url";

function warn(elem, message) {
    if (message) {
        elem.innerHTML = message;
        elem.removeAttribute("hidden");
    } else {
        elem.setAttribute("hidden", true);
    }
}

export class AudioHandler {
    constructor(warningNode, sampleRate) {
        this.warningNode = warningNode;
        this.warningNode.addEventListener("mousedown", () => this.tryResume());

        this.audioContext = typeof AudioContext !== "undefined"         ? new AudioContext({sampleRate})
                            : typeof webkitAudioContext !== "undefined" ? new webkitAudioContext()
                                                                        : null;
        this._jsAudioNode = null;
        if (this.audioContext && this.audioContext.audioWorklet) {
            console.log("Audio constructed ok") this.audioContext.onstatechange = () => this.checkStatus();
            this.checkStatus();
        } else if (this.audioContext && !this.audioContext.audioWorklet) {
            this.audioContext = null;
            console.log("Unable to initialise audio: no audio worklet API");
            const localhost = new URL(window.location);
            localhost.hostname = "localhost";
            warn(
                warningNode,
                `No audio worklet API was found - there will be no audio.
                    If you are running a local jsbeeb, you must either use a host of
                    <a href="${localhost}">localhost</a>,
                    or serve the content over <em>https</em>.`,
            );
        } else {
            console.log("Unable to initialise audio: no audio at all");
            warn(warningNode, "Unable to start audio");
        }
    }

    async initialise() {
        if (!this.audioContext || !this.audioContext.audioWorklet)
            return;
        await this.audioContext.audioWorklet.addModule(rendererUrl);
        this._audioDestination = this.audioContext.destination;

        this._jsAudioNode = new AudioWorkletNode(this.audioContext, "sound-chip-processor");
        this._jsAudioNode.connect(this._audioDestination);
        console.log("Audio initialised")
    }

    postAudio(buffer) {
        if (this._jsAudioNode)
            this._jsAudioNode.port.postMessage({time : Date.now(), buffer});
    }

    async tryResume() {
        console.log("Trying to resume audio")
        if (this.audioContext) await this.audioContext.resume();
    }

    checkStatus() {
        if (!this.audioContext)
            return;
        console.log("Audio status = ", this.audioContext.state);
        if (this.audioContext.state === "suspended")
            warn(this.warningNode, "Your browser has suspended audio. Click here to resume.");
        if (this.audioContext.state === "running")
            warn(this.warningNode, false);
    }
}
