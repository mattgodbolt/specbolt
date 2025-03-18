const textEncoder = new TextEncoder();

// based off https://github.com/ndesmic/wasm-cross/blob/v0/browser/wasi.js
export class Wasi {
  #encodedStdin;
  #envEncodedStrings;
  #argEncodedStrings;
  #instance;

  constructor({env, stdin, args}) {
    this.#encodedStdin = textEncoder.encode(stdin);
    const envStrings = Object.entries(env).map(([ k, v ]) => `${k}=${v}`);
    this.#envEncodedStrings =
        envStrings.map(s => textEncoder.encode(s +
                                               "\0")) this.#argEncodedStrings =
            args.map(s => textEncoder.encode(s + "\0"));
    this.bind();
  }

  // really annoying the interface works this way but we MUST set the instance
  // after creating it with the WASI class as an import in order to access it's
  // memory
  set instance(val) { this.#instance = val; }

  bind() {
    this.args_get = this.args_get.bind(this);
    this.args_sizes_get = this.args_sizes_get.bind(this);
    this.environ_get = this.environ_get.bind(this);
    this.environ_sizes_get = this.environ_sizes_get.bind(this);
    this.fd_read = this.fd_read.bind(this);
    this.fd_write = this.fd_write.bind(this);
    this.fd_close = this.fd_close.bind(this);
    const no_impl = () => { throw new Error("Not implemented") };
    this.fd_prestat_get = no_impl;
    this.fd_prestat_dir_name = no_impl;
    this.fd_pwrite = no_impl;
    this.fd_seek = no_impl;
  }

  args_sizes_get(argCountPtr, argBufferSizePtr) {
    const argByteLength =
        this.#argEncodedStrings.reduce((sum, val) => sum + val.byteLength, 0);
    const countPointerBuffer =
        new Uint32Array(this.#instance.exports.memory.buffer, argCountPtr, 1);
    const sizePointerBuffer = new Uint32Array(
        this.#instance.exports.memory.buffer, argBufferSizePtr, 1);
    countPointerBuffer[0] = this.#argEncodedStrings.length;
    sizePointerBuffer[0] = argByteLength;
    return 0;
  }

  args_get(argsPtr, argBufferPtr) {
    const argsByteLength =
        this.#argEncodedStrings.reduce((sum, val) => sum + val.byteLength, 0);
    const argsPointerBuffer =
        new Uint32Array(this.#instance.exports.memory.buffer, argsPtr,
                        this.#argEncodedStrings.length);
    const argsBuffer = new Uint8Array(this.#instance.exports.memory.buffer,
                                      argBufferPtr, argsByteLength)

    let pointerOffset = 0;
    for (let i = 0; i < this.#argEncodedStrings.length; i++) {
      const currentPointer = argBufferPtr + pointerOffset;
      argsPointerBuffer[i] = currentPointer;
      argsBuffer.set(this.#argEncodedStrings[i], pointerOffset)
      pointerOffset += this.#argEncodedStrings[i].byteLength;
    }

    return 0;
  }

  fd_write(fd, iovsPtr, iovsLength, bytesWrittenPtr) {
    const iovs = new Uint32Array(this.#instance.exports.memory.buffer, iovsPtr,
                                 iovsLength * 2);
    if (fd === 1) { // stdout
      let text = "";
      let totalBytesWritten = 0;

      const decoder = new TextDecoder();
      for (let i = 0; i < iovsLength * 2; i += 2) {
        const offset = iovs[i];
        const length = iovs[i + 1];
        const textChunk = decoder.decode(new Int8Array(
            this.#instance.exports.memory.buffer, offset, length));
        text += textChunk;
        totalBytesWritten += length;
      }

      const dataView = new DataView(this.#instance.exports.memory.buffer);
      dataView.setInt32(bytesWrittenPtr, totalBytesWritten, true);
      console.log(text);
    }
    return 0;
  }

  fd_read(fd, iovsPtr, iovsLength, bytesReadPtr) {
    const memory = new Uint8Array(this.#instance.exports.memory.buffer);
    const iovs = new Uint32Array(this.#instance.exports.memory.buffer, iovsPtr,
                                 iovsLength * 2);
    let totalBytesRead = 0;
    if (fd === 0) { // stdin
      for (let i = 0; i < iovsLength * 2; i += 2) {
        const offset = iovs[i];
        const length = iovs[i + 1];
        const chunk = this.#encodedStdin.slice(0, length);
        this.#encodedStdin = this.#encodedStdin.slice(length);

        memory.set(chunk, offset);
        totalBytesRead += chunk.byteLength;

        if (this.#encodedStdin.length === 0)
          break;
      }

      const dataView = new DataView(this.#instance.exports.memory.buffer);
      dataView.setInt32(bytesReadPtr, totalBytesRead, true);
    }
    return 0;
  }

  fd_close(fd) {
    // TODO
  }

  environ_get(environPtr, environBufferPtr) {
    const envByteLength = this.#envEncodedStrings.map(s => s.byteLength)
                              .reduce((sum, val) => sum + val, 0);
    const environsPointerBuffer =
        new Uint32Array(this.#instance.exports.memory.buffer, environPtr,
                        this.#envEncodedStrings.length);
    const environsBuffer = new Uint8Array(this.#instance.exports.memory.buffer,
                                          environBufferPtr, envByteLength)

    let pointerOffset = 0;
    for (let i = 0; i < this.#envEncodedStrings.length; i++) {
      const currentPointer = environBufferPtr + pointerOffset;
      environsPointerBuffer[i] = currentPointer;
      environsBuffer.set(this.#envEncodedStrings[i], pointerOffset)
      pointerOffset += this.#envEncodedStrings[i].byteLength;
    }

    return 0;
  }

  environ_sizes_get(environCountPtr, environBufferSizePtr) {
    const envByteLength = this.#envEncodedStrings.map(s => s.byteLength)
                              .reduce((sum, val) => sum + val, 0);
    const countPointerBuffer = new Uint32Array(
        this.#instance.exports.memory.buffer, environCountPtr, 1);
    const sizePointerBuffer = new Uint32Array(
        this.#instance.exports.memory.buffer, environBufferSizePtr, 1);
    countPointerBuffer[0] = this.#envEncodedStrings.length;
    sizePointerBuffer[0] = envByteLength;
    return 0;
  }

  proc_exit() { console.log("EXIT"); }
}
