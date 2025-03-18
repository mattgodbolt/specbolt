import rendererUrl from "./audio-renderer.js?url";

function toggle(elem, visible) {
    if (!visible)
        elem.setAttribute("hidden", true);
    else
        elem.removeAttribute("hidden");
}

export class AudioHandler {
    constructor(warningNode, sampleRate) {
        this.warningNode = warningNode;
        this.sampleRate = sampleRate;
        toggle(this.warningNode, false);
        this.audioContext = typeof AudioContext !== "undefined"         ? new AudioContext()
                            : typeof webkitAudioContext !== "undefined" ? new webkitAudioContext()
                                                                        : null;
        this._jsAudioNode = null;
        if (this.audioContext && this.audioContext.audioWorklet) {
            this.audioContext.onstatechange = () => this.checkStatus();
            this._setup().then();
        } else {
            if (this.audioContext && !this.audioContext.audioWorklet) {
                this.audioContext = null;
                console.log("Unable to initialise audio: no audio worklet API");
                toggle(warningNode, true);
                const localhost = new URL(window.location);
                localhost.hostname = "localhost";
                this.warningNode.html(
                    `No audio worklet API was found - there will be no audio.
                    If you are running a local jsbeeb, you must either use a host of
                    <a href="${localhost}">localhost</a>,
                    or serve the content over <em>https</em>.`,
                );
            }
        }

        this.warningNode.addEventListener("mousedown", () => this.tryResume());
        toggle(this.warningNode, false);
    }

    async _setup() {
        await this.audioContext.audioWorklet.addModule(rendererUrl);
        this._audioDestination = this.audioContext.destination;

        this._jsAudioNode = new AudioWorkletNode(this.audioContext, "sound-chip-processor",
                                                 {processorOptions : {sampleRate : this.sampleRate}});
        this._jsAudioNode.connect(this._audioDestination);
    }

    postAudio(buffer) {
        if (this._jsAudioNode)
            this._jsAudioNode.port.postMessage({time : Date.now(), buffer});
    }

    // Recent browsers, particularly Safari and Chrome, require a user interaction in order to enable sound playback.
    async tryResume() {
        if (this.audioContext)
            await this.audioContext.resume();
    }

    checkStatus() {
        if (!this.audioContext)
            return;
        if (this.audioContext.state === "suspended")
            toggle(this.warningNode, true);
        if (this.audioContext.state === "running")
            toggle(this.warningNode, false);
    }
}
