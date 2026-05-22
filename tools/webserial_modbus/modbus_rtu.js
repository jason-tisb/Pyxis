function u16(value) {
  return value & 0xffff;
}

export function crc16Modbus(bytes) {
  let crc = 0xffff;
  for (let i = 0; i < bytes.length; i++) {
    crc ^= bytes[i];
    for (let bit = 0; bit < 8; bit++) {
      crc = crc & 1 ? (crc >> 1) ^ 0xa001 : crc >> 1;
    }
  }
  return crc & 0xffff;
}

function appendCrc(frame) {
  const crc = crc16Modbus(frame);
  frame.push(crc & 0xff, (crc >> 8) & 0xff);
  return frame;
}

function toHex(bytes) {
  return Array.from(bytes)
    .map((byte) => byte.toString(16).padStart(2, "0"))
    .join(" ");
}

function toHexShort(bytes, max = 96) {
  const arr = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);
  const shown = arr.length > max ? arr.slice(0, max) : arr;
  const suffix = arr.length > max ? ` ...(+${arr.length - max}b)` : "";
  return `${toHex(shown)}${suffix}`;
}

function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

function computeTimings({ baudRate, dataBits, parity, stopBits }) {
  const baud = Math.max(300, Number(baudRate) || 9600);
  const bitsPerChar =
    1 + (Number(dataBits) === 7 ? 7 : 8) + (parity && parity !== "none" ? 1 : 0) + (Number(stopBits) === 2 ? 2 : 1);
  const charTimeMs = (bitsPerChar * 1000) / baud;
  if (baud > 19200) return { bitsPerChar, interCharMs: 0.75, frameGapMs: 1.75 };
  return { bitsPerChar, interCharMs: charTimeMs * 1.5, frameGapMs: charTimeMs * 3.5 };
}

export class ModbusRtuMaster {
  constructor({ port, slaveId, timeoutMs = 10000, retries = 2, log = () => {}, logRaw = false }) {
    this.port = port;
    this.slaveId = Number(slaveId);
    this.timeoutMs = Math.max(50, Number(timeoutMs) || 10000);
    this.retries = Math.max(0, Number(retries) || 0);
    this.log = log;
    this.logRaw = Boolean(logRaw);
    this._reader = null;
    this._writer = null;
    this._keepReading = false;
    this._buffer = [];
    this._pending = null;
    this._queue = Promise.resolve();
    this._lastTxRxAt = 0;
    this._timings = computeTimings({ baudRate: 19200, dataBits: 8, parity: "none", stopBits: 2 });
  }

  setSlaveId(id) {
    this.slaveId = Number(id);
  }

  setTimeoutMs(ms) {
    this.timeoutMs = Math.max(50, Number(ms) || 10000);
  }

  setRetries(retries) {
    this.retries = Math.max(0, Number(retries) || 0);
  }

  setLogRaw(enabled) {
    this.logRaw = Boolean(enabled);
  }

  setSerialParams(params) {
    this._timings = computeTimings(params);
    this.log(
      `TIMING baud=${Number(params.baudRate)} bits/char=${this._timings.bitsPerChar} ` +
        `wireInter<=${this._timings.interCharMs.toFixed(2)}ms frame>=${this._timings.frameGapMs.toFixed(2)}ms ` +
        `timeout=${this.timeoutMs}ms`
    );
  }

  async open() {
    if (!this.port?.readable || !this.port?.writable) throw new Error("port not open");
    this._writer = this.port.writable.getWriter();
    this._keepReading = true;
    this._readLoop();
  }

  async close() {
    this._keepReading = false;
    this._rejectPending(new Error("serial closed"));
    try {
      if (this._reader) await this._reader.cancel();
    } catch {}
    try {
      if (this._writer) await this._writer.close();
    } catch {}
    try {
      this._reader?.releaseLock();
    } catch {}
    try {
      this._writer?.releaseLock();
    } catch {}
    this._reader = null;
    this._writer = null;
    this._buffer = [];
  }

  _enqueue(task) {
    this._queue = this._queue.then(task, task);
    return this._queue;
  }

  async _readLoop() {
    while (this.port.readable && this._keepReading) {
      this._reader = this.port.readable.getReader();
      try {
        while (this._keepReading) {
          const { value, done } = await this._reader.read();
          if (done) break;
          if (!value || value.length === 0) continue;
          this._lastTxRxAt = performance.now();
          this._buffer.push(...value);
          if (this.logRaw) this.log(`RAW RX ${toHexShort(value)}`);
          this._processBuffer();
        }
      } catch (error) {
        if (this._keepReading) this.log(`Read error: ${error.message || error}`);
      } finally {
        try {
          this._reader.releaseLock();
        } catch {}
        this._reader = null;
      }
    }
  }

  _rejectPending(error) {
    if (!this._pending) return;
    clearTimeout(this._pending.timer);
    const reject = this._pending.reject;
    this._pending = null;
    reject(error);
  }

  _processBuffer() {
    if (!this._pending || this._buffer.length < 5) return;

    while (this._pending && this._buffer.length >= 5) {
      const expected = this._pending;
      const slaveIndex = this._buffer.indexOf(expected.slaveId);
      if (slaveIndex < 0) {
        if (this.logRaw) this.log(`DROP ${toHexShort(this._buffer)}`);
        this._buffer = [];
        return;
      }
      if (slaveIndex > 0) {
        if (this.logRaw) this.log(`DROP ${toHexShort(this._buffer.slice(0, slaveIndex))}`);
        this._buffer.splice(0, slaveIndex);
      }

      const fc = this._buffer[1];
      if (fc !== expected.func && fc !== (expected.func | 0x80)) {
        this._buffer.shift();
        continue;
      }

      const frameLength = fc & 0x80 ? 5 : expected.responseLength;
      if (this._buffer.length < frameLength) return;

      const frame = new Uint8Array(this._buffer.slice(0, frameLength));
      const crcGot = frame[frame.length - 2] | (frame[frame.length - 1] << 8);
      const crcCalc = crc16Modbus(frame.slice(0, frame.length - 2));
      if (crcGot !== crcCalc) {
        this.log(`CRC BAD ${toHexShort(frame)} calc=${crcCalc.toString(16).padStart(4, "0")}`);
        this._buffer.shift();
        continue;
      }

      this._buffer.splice(0, frameLength);
      clearTimeout(expected.timer);
      this._pending = null;
      this.log(`RX ${toHex(frame)}`);
      expected.resolve(frame);
    }
  }

  async _waitFrameGap() {
    const elapsed = performance.now() - this._lastTxRxAt;
    const waitMs = Math.max(0, this._timings.frameGapMs - elapsed);
    if (waitMs > 0) await sleep(waitMs);
  }

  async _transactionOnce(frameBytes, { func, responseLength }) {
    await this._waitFrameGap();
    this._buffer = [];

    return await new Promise(async (resolve, reject) => {
      const timer = setTimeout(() => {
        const raw = this._buffer.length ? ` raw=${toHexShort(this._buffer)}` : "";
        this._pending = null;
        reject(new Error(`Modbus timeout${raw}`));
      }, this.timeoutMs);

      this._pending = { slaveId: this.slaveId, func, responseLength, resolve, reject, timer };

      try {
        const bytes = new Uint8Array(frameBytes);
        this.log(`TX ${toHex(bytes)}`);
        await this._writer.write(bytes);
        this._lastTxRxAt = performance.now();
        this._processBuffer();
      } catch (error) {
        clearTimeout(timer);
        this._pending = null;
        reject(error);
      }
    });
  }

  async _transaction(frameBytes, expected) {
    let lastError = null;
    for (let attempt = 0; attempt <= this.retries; attempt++) {
      try {
        if (attempt > 0) this.log(`RETRY ${attempt}`);
        return await this._transactionOnce(frameBytes, expected);
      } catch (error) {
        lastError = error;
      }
    }
    throw lastError;
  }

  _parseReadRegisters(frame, func, count) {
    if (frame[0] !== this.slaveId) throw new Error("slave id mismatch");
    if (frame[1] & 0x80) throw new Error(`modbus exception 0x${frame[2].toString(16)}`);
    if (frame[1] !== func) throw new Error("function mismatch");
    if (frame[2] !== count * 2) throw new Error("bytecount mismatch");

    const regs = [];
    for (let i = 0; i < count; i++) {
      regs.push((frame[3 + i * 2] << 8) | frame[4 + i * 2]);
    }
    return regs;
  }

  async readHolding(startAddr, count) {
    return this._enqueue(async () => {
      const req = appendCrc([this.slaveId, 0x03, (startAddr >> 8) & 0xff, startAddr & 0xff, (count >> 8) & 0xff, count & 0xff]);
      const frame = await this._transaction(req, { func: 0x03, responseLength: 3 + count * 2 + 2 });
      return this._parseReadRegisters(frame, 0x03, count);
    });
  }

  async readInput(startAddr, count) {
    return this._enqueue(async () => {
      const req = appendCrc([this.slaveId, 0x04, (startAddr >> 8) & 0xff, startAddr & 0xff, (count >> 8) & 0xff, count & 0xff]);
      const frame = await this._transaction(req, { func: 0x04, responseLength: 3 + count * 2 + 2 });
      return this._parseReadRegisters(frame, 0x04, count);
    });
  }

  async writeSingleHolding(addr, value) {
    return this._enqueue(async () => {
      const v = u16(value);
      const req = appendCrc([this.slaveId, 0x06, (addr >> 8) & 0xff, addr & 0xff, (v >> 8) & 0xff, v & 0xff]);
      const frame = await this._transaction(req, { func: 0x06, responseLength: 8 });
      if (frame[1] & 0x80) throw new Error(`modbus exception 0x${frame[2].toString(16)}`);
      if (frame[1] !== 0x06) throw new Error("function mismatch");
      if (((frame[2] << 8) | frame[3]) !== addr || ((frame[4] << 8) | frame[5]) !== v) throw new Error("write echo mismatch");
      return true;
    });
  }

  async writeMultipleHolding(startAddr, values) {
    return this._enqueue(async () => {
      const count = values.length;
      const req = [this.slaveId, 0x10, (startAddr >> 8) & 0xff, startAddr & 0xff, (count >> 8) & 0xff, count & 0xff, count * 2];
      for (const value of values) {
        const v = u16(value);
        req.push((v >> 8) & 0xff, v & 0xff);
      }
      const frame = await this._transaction(appendCrc(req), { func: 0x10, responseLength: 8 });
      if (frame[1] & 0x80) throw new Error(`modbus exception 0x${frame[2].toString(16)}`);
      if (frame[1] !== 0x10) throw new Error("function mismatch");
      if (((frame[2] << 8) | frame[3]) !== startAddr || ((frame[4] << 8) | frame[5]) !== count) throw new Error("write echo mismatch");
      return true;
    });
  }
}
