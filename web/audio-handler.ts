// @ts-ignore
import rendererUrl from "./fifo-audio.js?url";

function warn(elem: HTMLElement, message?: string) {
    if (message) {
        elem.innerHTML = message;
        elem.removeAttribute("hidden");
    } else {
        elem.setAttribute("hidden", "");
    }
}

export class AudioHandler {
    private readonly warningNode: HTMLElement;
    private readonly audioContext: AudioContext | null;
    private _jsAudioNode: AudioWorkletNode | null;
    private _audioDestination: AudioDestinationNode;

    constructor(warningNode: HTMLElement, sampleRate: number) {
        this.warningNode = warningNode;
        this.warningNode.addEventListener("mousedown", () => this.tryResume());

        this.audioContext = new AudioContext({sampleRate});
        this._jsAudioNode = null;
        if (this.audioContext && this.audioContext.audioWorklet) {
            console.log("Audio constructed ok");
            this.audioContext.onstatechange = () => this.checkStatus();
            this.checkStatus();
        } else if (this.audioContext && !this.audioContext.audioWorklet) {
            this.audioContext = null;
            console.log("Unable to initialise audio: no audio worklet API");
            const localhost = new URL(window.location.toString());
            localhost.hostname = "localhost";
            warn(
                warningNode,
                `No audio worklet API was found - there will be no audio.
                    If you are running a local specbolt, you must either use a host of
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

        this._jsAudioNode = new AudioWorkletNode(this.audioContext, "fifo-audio");
        this._jsAudioNode.connect(this._audioDestination);
        console.log("Audio initialised")
    }

    postAudio(buffer: Int16Array) {
        if (this._jsAudioNode)
            this._jsAudioNode.port.postMessage({time: Date.now(), buffer});
    }

    async tryResume() {
        if (this.audioContext && this.audioContext.state !== "running") {
            console.log("Trying to resume audio");
            await this.audioContext.resume();
        }
    }

    checkStatus() {
        if (!this.audioContext)
            return;
        console.log("Audio status = ", this.audioContext.state);
        if (this.audioContext.state === "suspended") {
            warn(this.warningNode, "Your browser has suspended audio. Click here to resume.");
        }
        if (this.audioContext.state === "running") {
            warn(this.warningNode, undefined);
        }
    }
}
