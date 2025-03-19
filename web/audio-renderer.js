/*global sampleRate, currentTime */

class SoundChipProcessor extends AudioWorkletProcessor {
    constructor(options) {
        super(options);
        console.log("Audio processor: Sample rate", sampleRate);

        this._lastSample = 0;
        this.queue = [];
        this._queueSizeBytes = 0;
        this.dropped = 0;
        this.underruns = 0;
        this.targetLatencyMs = 1000 * (1 / 50);
        this.maxQueueSizeBytes = sampleRate * 0.25;
        this.port.onmessage = (event) => { this.onBuffer(event.data.time, event.data.buffer); };
        this.nextLog = 0;
    }

    _queueAge() {
        if (this.queue.length === 0)
            return 0;
        const timeInBufferMs = 1000 * (this.queue[0].offset / sampleRate) + this.queue[0].time;
        return Date.now() - timeInBufferMs;
    }

    onBuffer(time, buffer) {
        this.queue.push({offset : 0, time, buffer});
        this._queueSizeBytes += buffer.length;
        this.cleanQueue();
    }

    _shift() {
        const dropped = this.queue.shift();
        this._queueSizeBytes -= dropped.buffer.length;
    }

    cleanQueue() {
        const maxLatency = this.targetLatencyMs * 2;
        while (this._queueSizeBytes > this.maxQueueSizeBytes || this._queueAge() > maxLatency) {
            this._shift();
            this.dropped++;
        }
    }

    nextSample() {
        if (this.queue.length) {
            const queueElement = this.queue[0];
            this._lastSample = queueElement.buffer[queueElement.offset] / 32768;
            if (++queueElement.offset === queueElement.buffer.length)
                this._shift();
        } else {
            this.underruns++;
        }
        return this._lastSample;
    }

    process(inputs, outputs) {
        this.cleanQueue();
        if (currentTime > this.nextLog) {
            console.log("Dropped", this.dropped, "Underruns", this.underruns);
            this.nextLog = currentTime + 30;
        }

        const channel = outputs[0][0];
        for (let i = 0; i < channel.length; i++) {
            channel[i] = this.nextSample();
        }
        return true;
    }
}

registerProcessor("sound-chip-processor", SoundChipProcessor);
