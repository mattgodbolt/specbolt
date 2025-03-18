/*global sampleRate, currentTime */

const lowPassFilterFreq = sampleRate / 2;
const RC = 1 / (2 * Math.PI * lowPassFilterFreq);

let moose = 0;
let nextLog = 0;
class SoundChipProcessor extends AudioWorkletProcessor {
    constructor(options) {
        super(options);
        console.log("Sample rate", sampleRate);

        this.inputSampleRate = options.processorOptions.sampleRate;
        this._lastSample = 0;
        this._lastFilteredOutput = 0;
        this.queue = [];
        this._queueSizeBytes = 0;
        this.dropped = 0;
        this.underruns = 0;
        this.targetLatencyMs = 1000 * (10 / 50);
        this.startQueueSizeBytes = this.inputSampleRate * (this.targetLatencyMs / 1000) / 2;
        console.log("start queue size", this.startQueueSizeBytes);
        this.running = false;
        this.maxQueueSizeBytes = this.inputSampleRate * 0.25;
        this.port.onmessage = (event) => { this.onBuffer(event.data.time, event.data.buffer); };
    }

    _queueAge() {
        if (this.queue.length === 0)
            return 0;
        const timeInBufferMs = 1000 * (this.queue[0].offset / this.inputSampleRate) + this.queue[0].time;
        return Date.now() - timeInBufferMs;
    }

    onBuffer(time, buffer) {
        this.queue.push({offset : 0, time, buffer});
        this._queueSizeBytes += buffer.length;
        this.cleanQueue();
        if (!this.running && this._queueSizeBytes >= this.startQueueSizeBytes)
            this.running = true;
    }

    _shift() {
        const dropped = this.queue.shift();
        this._queueSizeBytes -= dropped.buffer.length;
    }

    cleanQueue() {
        const maxLatency = this.targetLatencyMs * 10;
        while (this._queueSizeBytes > this.maxQueueSizeBytes || this._queueAge() > maxLatency) {
            this._shift();
            this.dropped++;
        }
    }

    nextSample() {
        if (this.running && this.queue.length) {
            const queueElement = this.queue[0];
            this._lastSample = queueElement.buffer[queueElement.offset];
            if (++queueElement.offset === queueElement.buffer.length)
                this._shift();
        } else {
            this.underruns++;
            this.running = false;
        }
        return this._lastSample;
    }

    process(inputs, outputs) {
        this.cleanQueue();
        const channel = outputs[0][0];
        let nonzero = false;
        for (let i = 0; i < channel.length; i += 2) {
            channel[i] = this.nextSample();
            channel[i + 1] = channel[i]; // this.nextSample();
            if (channel[i] !== 0)
                nonzero = true;
        }
        // if (++moose < 20) {
        //     console.log(channel.length, currentTime);
        // }
        // if (nonzero) {
        //     console.log(channel);
        // }
        if (currentTime > nextLog) {
            console.log("Dropped", this.dropped, "Underruns", this.underruns, "Queue size", this._queueSizeBytes);
            nextLog = currentTime + 2;
        }
        return true;
        if (this.queue.length === 0) {
            return true;
        }

        // I looked into using https://www.npmjs.com/package/@alexanderolsen/libsamplerate-js or similar (the full API),
        // but we fiddle the sample rate here to catch up with the target latency, which is harder to do with that API.
        const outByMs = this._queueAge() - this.targetLatencyMs;
        const maxAdjust = this.inputSampleRate * 0.01;
        const adjustment = Math.min(maxAdjust, Math.max(-maxAdjust, outByMs * 100));
        const effectiveSampleRate = this.inputSampleRate + adjustment;
        const sampleRatio = effectiveSampleRate / sampleRate;

        const dt = 1 / effectiveSampleRate;
        const filterAlpha = dt / (RC + dt);

        const numInputSamples = Math.round(sampleRatio * channel.length);
        const source = new Float32Array(numInputSamples);
        let prevSample = this._lastFilteredOutput;
        for (let i = 0; i < numInputSamples; ++i) {
            prevSample += filterAlpha * (this.nextSample() - prevSample);
            source[i] = prevSample;
        }
        this._lastFilteredOutput = prevSample;
        for (let i = 0; i < channel.length; i++) {
            const pos = (i + 0.5) * sampleRatio;
            const loc = Math.floor(pos);
            const alpha = pos - loc;
            channel[i] = source[loc] * (1 - alpha) + source[loc + 1] * alpha;
        }
        return true;
    }
}

registerProcessor("sound-chip-processor", SoundChipProcessor);
