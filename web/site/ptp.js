// SPDX-License-Identifier: GPL-2.0-or-later
// Talks to a camera running CHDK over WebUSB: plain PTP framing plus CHDK's
// PTP extension (operation 0x9999, see core/ptp.h in the CHDK source). The
// same commands chdkptp uses: run Lua, read its messages, download and
// upload files.

const PTP = { OpenSession: 0x1002, CloseSession: 0x1003, CHDK: 0x9999 };
const RC_OK = 0x2001, RC_SESSION_OPEN = 0x201e;
const CHDK = { Version: 0, TempData: 4, UploadFile: 5, DownloadFile: 6, ExecuteScript: 7, ScriptStatus: 8, ReadScriptMsg: 10 };
const SCRIPT_NOKILL = 0x100, SCRIPT_FLUSH = 0x200 | 0x400;
const STATUS_RUN = 1, STATUS_MSG = 2;
const MSG_NONE = 0, MSG_ERR = 1, MSG_RET = 2, MSG_USER = 3;
const TYPE_NIL = 1, TYPE_BOOLEAN = 2, TYPE_INTEGER = 3;
const CONTAINER_DATA = 2, CONTAINER_RESPONSE = 3;
const CHUNK = 1 << 20;

export class PtpError extends Error {
  constructor(message, code) { super(message); this.code = code; }
}

const text = new TextEncoder(), untext = new TextDecoder('latin1');
const hex = (n) => '0x' + n.toString(16).padStart(4, '0');

export class ChdkCamera {
  // Asks the user to pick the camera. Canon's vendor id, or any PTP device.
  static async request() {
    if (!navigator.usb) throw new PtpError('This browser has no WebUSB. Use Chrome or Edge.');
    const device = await navigator.usb.requestDevice({ filters: [{ vendorId: 0x04a9 }, { classCode: 6 }] });
    return ChdkCamera.open(device);
  }

  // A camera picked before, without asking again.
  static async known() {
    if (!navigator.usb) return null;
    const devices = await navigator.usb.getDevices();
    return devices.length ? ChdkCamera.open(devices[0]) : null;
  }

  static async open(device) {
    const cam = new ChdkCamera(device);
    await cam.connect();
    return cam;
  }

  constructor(device) {
    this.device = device;
    this.tid = 0;
    this.queue = Promise.resolve();
    this.ops = Promise.resolve();
  }

  // One CHDK operation at a time: a download is two transactions and a
  // script several, which must not interleave with another's.
  #op(fn) {
    const p = this.ops.then(fn, fn);
    this.ops = p.catch(() => {});
    return p;
  }

  get name() { return this.device.productName || 'camera'; }

  async connect() {
    const d = this.device;
    await d.open();
    if (!d.configuration) await d.selectConfiguration(1);
    const iface = d.configuration.interfaces.find((i) => i.alternates.some((a) => a.interfaceClass === 6));
    if (!iface) throw new PtpError('The device has no PTP interface.');
    const alt = iface.alternates.find((a) => a.interfaceClass === 6);
    try {
      await d.claimInterface(iface.interfaceNumber);
    } catch (err) {
      throw new PtpError('The browser could not take the camera. On a Mac, macOS\'s ptpcamerad usually has it: '
        + 'run "killall ptpcamerad" in Terminal and connect again. Also close chdkptp, Image Capture and Photos. '
        + `(${err.message})`);
    }
    this.iface = iface.interfaceNumber;
    for (const ep of alt.endpoints) {
      if (ep.type !== 'bulk') continue;
      if (ep.direction === 'in') { this.epIn = ep.endpointNumber; this.packet = ep.packetSize; }
      else this.epOut = ep.endpointNumber;
    }
    if (!this.epIn || !this.epOut) throw new PtpError('The PTP interface has no bulk endpoints.');
    const r = await this.transaction(PTP.OpenSession, [1]);
    if (r.code !== RC_OK && r.code !== RC_SESSION_OPEN) throw new PtpError(`OpenSession failed (${hex(r.code)})`, r.code);
  }

  async close() {
    try { await this.transaction(PTP.CloseSession, []); } catch { /* the camera may be gone */ }
    try { await this.device.releaseInterface(this.iface); } catch { /* ditto */ }
    try { await this.device.close(); } catch { /* ditto */ }
  }

  // One PTP transaction at a time, in order.
  transaction(code, params, dataOut = null, onProgress = null) {
    const run = () => this.#transaction(code, params, dataOut, onProgress);
    const p = this.queue.then(run, run);
    this.queue = p.catch(() => {});
    return p;
  }

  async #transaction(code, params, dataOut, onProgress) {
    const tid = ++this.tid;
    const cmd = new DataView(new ArrayBuffer(12 + 4 * params.length));
    cmd.setUint32(0, cmd.byteLength, true);
    cmd.setUint16(4, 1, true);
    cmd.setUint16(6, code, true);
    cmd.setUint32(8, tid, true);
    params.forEach((p, i) => cmd.setUint32(12 + 4 * i, p >>> 0, true));
    await this.#out(cmd.buffer);

    if (dataOut) await this.#sendData(code, tid, dataOut, onProgress);

    let data = null;
    for (;;) {
      const c = await this.#readContainer(onProgress);
      if (c.type === CONTAINER_DATA) { data = c.payload; continue; }
      if (c.type === CONTAINER_RESPONSE) {
        if (c.tid !== tid) throw new PtpError(`Response for transaction ${c.tid}, expected ${tid}`);
        const v = new DataView(c.payload.buffer, c.payload.byteOffset, c.payload.byteLength);
        const out = [];
        for (let i = 0; i + 4 <= v.byteLength; i += 4) out.push(v.getUint32(i, true));
        return { code: c.code, params: out, data };
      }
      // Events (type 4) on the bulk pipe are not expected; skip them.
    }
  }

  async #out(buffer) {
    const r = await this.device.transferOut(this.epOut, buffer);
    if (r.status !== 'ok') throw new PtpError(`USB write: ${r.status}`);
  }

  async #sendData(code, tid, data, onProgress) {
    const total = 12 + data.byteLength;
    const head = new DataView(new ArrayBuffer(12));
    head.setUint32(0, total, true);
    head.setUint16(4, CONTAINER_DATA, true);
    head.setUint16(6, code, true);
    head.setUint32(8, tid, true);
    // The header and the first chunk go together: the camera reads the header
    // from the first packet.
    const first = Math.min(data.byteLength, CHUNK - 12);
    const start = new Uint8Array(12 + first);
    start.set(new Uint8Array(head.buffer), 0);
    start.set(data.subarray(0, first), 12);
    await this.#out(start);
    for (let off = first; off < data.byteLength; off += CHUNK) {
      const end = Math.min(off + CHUNK, data.byteLength);
      await this.#out(data.subarray(off, end));
      onProgress?.(end, data.byteLength);
    }
    if (total % this.packet === 0) await this.#out(new ArrayBuffer(0));
  }

  async #in(length) {
    const r = await this.device.transferIn(this.epIn, length);
    if (r.status !== 'ok') throw new PtpError(`USB read: ${r.status}`);
    return new Uint8Array(r.data.buffer, r.data.byteOffset, r.data.byteLength);
  }

  // Reads one container. The first read is one packet, so it ends whatever
  // the container's length; the rest is read in chunks no longer than what
  // is left, so no read waits for bytes that never come.
  async #readContainer(onProgress) {
    let first = await this.#in(this.packet);
    // A zero-length packet after a data phase that filled its last packet.
    for (let i = 0; first.byteLength === 0 && i < 3; i++) first = await this.#in(this.packet);
    if (first.byteLength < 12) throw new PtpError(`Short PTP container (${first.byteLength} bytes)`);
    const v = new DataView(first.buffer, first.byteOffset, first.byteLength);
    const length = v.getUint32(0, true), type = v.getUint16(4, true), code = v.getUint16(6, true), tid = v.getUint32(8, true);
    const payload = new Uint8Array(length - 12);
    const firstPart = Math.min(first.byteLength, length) - 12;
    payload.set(first.subarray(12, 12 + firstPart), 0);
    let got = firstPart;
    while (got < payload.byteLength) {
      const part = await this.#in(Math.min(CHUNK, payload.byteLength - got));
      if (!part.byteLength) throw new PtpError('The camera stopped sending mid-file.');
      payload.set(part, got);
      got += part.byteLength;
      if (type === CONTAINER_DATA) onProgress?.(got, payload.byteLength);
    }
    return { type, code, tid, payload };
  }

  async #chdk(params, dataOut = null, onProgress = null) {
    const r = await this.transaction(PTP.CHDK, params, dataOut, onProgress);
    if (r.code !== RC_OK) throw new PtpError(`CHDK command ${params[0]} failed (${hex(r.code)})`, r.code);
    return r;
  }

  // CHDK's PTP protocol version, e.g. { major: 2, minor: 9 }.
  version() {
    return this.#op(async () => {
      const r = await this.#chdk([CHDK.Version]);
      return { major: r.params[0], minor: r.params[1] };
    });
  }

  // Runs Lua on the camera and waits for it to end. Returns what the script
  // returned (one value, or an array for several) and anything it sent with
  // write_usb_msg. Throws with the camera's message on a Lua error. With
  // bytes, strings come back as Uint8Array.
  lua(code, opts = {}) { return this.#op(() => this.#lua(code, opts)); }

  async #lua(code, { wait = true, timeout = 10000, bytes = false } = {}) {
    const r = await this.#chdk([CHDK.ExecuteScript, SCRIPT_NOKILL | SCRIPT_FLUSH], text.encode(code));
    const id = r.params[0], status = r.params[1];
    if (status === 0x1000) throw new PtpError('A script is already running on the camera.');
    const result = { values: [], messages: [] };
    if (!wait && status === 0) return result;
    const deadline = performance.now() + timeout;
    for (;;) {
      const st = (await this.#chdk([CHDK.ScriptStatus])).params[0];
      if (st & STATUS_MSG) await this.#drain(id, result, bytes);
      else if (!(st & STATUS_RUN)) break;
      else if (performance.now() > deadline) throw new PtpError('The script is still running on the camera.');
      else await new Promise((res) => setTimeout(res, 20));
    }
    await this.#drain(id, result, bytes);
    if (result.error) throw new PtpError(result.error);
    return result.values.length > 1 ? result.values : result.values[0];
  }

  async #drain(id, result, raw) {
    for (;;) {
      const r = await this.#chdk([CHDK.ReadScriptMsg]);
      const [type, subtype, sid, size] = r.params;
      if (type === MSG_NONE) return;
      if (sid !== id) continue;
      const bytes = r.data ? r.data.subarray(0, size) : new Uint8Array(0);
      if (type === MSG_ERR) result.error = untext.decode(bytes);
      else if (type === MSG_RET) result.values.push(decodeValue(subtype, bytes, raw));
      else if (type === MSG_USER) result.messages.push(decodeValue(subtype, bytes, raw));
    }
  }

  // Reads a whole file from the card, e.g. 'A/FGRAW/R0000001.RAW'.
  download(path, onProgress = null) {
    return this.#op(async () => {
      await this.#chdk([CHDK.TempData, 0], text.encode(path));
      const r = await this.#chdk([CHDK.DownloadFile], null, onProgress);
      return r.data || new Uint8Array(0);
    });
  }

  // A file's first `length` bytes, read by Lua on the camera, e.g. a JPEG's
  // Exif block without the whole file.
  async head(path, length) {
    const r = await this.lua(`local f,err=io.open(${luaString(path)},'rb')
if not f then error(err) end
local s=f:read(${length | 0}) f:close() return s or ''`, { bytes: true });
    return r instanceof Uint8Array ? r : new Uint8Array(0);
  }

  // Writes a whole file to the card, replacing it.
  upload(path, bytes, onProgress = null) { return this.#op(() => this.#upload(path, bytes, onProgress)); }

  async #upload(path, bytes, onProgress) {
    const name = text.encode(path);
    const data = new Uint8Array(4 + name.byteLength + bytes.byteLength);
    new DataView(data.buffer).setUint32(0, name.byteLength, true);
    data.set(name, 4);
    data.set(bytes, 4 + name.byteLength);
    await this.#chdk([CHDK.UploadFile], data, onProgress);
  }

  // A card folder's entries: { name, size, dir, mtime }.
  async list(path) {
    const out = await this.lua(`
local p=${luaString(path)}
local t,err=os.listdir(p)
if not t then error(err or ('cannot list '..p)) end
local o={}
for _,n in ipairs(t) do
  local s=os.stat(p..'/'..n)
  o[#o+1]=n..'\\t'..(s and s.size or 0)..'\\t'..((s and s.is_dir) and 1 or 0)..'\\t'..(s and s.mtime or 0)
end
return table.concat(o,'\\n')`);
    if (!out) return [];
    return out.split('\n').map((line) => {
      const [name, size, dir, mtime] = line.split('\t');
      return { name, size: +size, dir: dir === '1', mtime: +mtime };
    });
  }
}

export function luaString(s) {
  if (!/^[\x20-\x7e]*$/.test(s)) throw new PtpError(`Unsupported characters in ${JSON.stringify(s)}`);
  return "'" + s.replace(/\\/g, '\\\\').replace(/'/g, "\\'") + "'";
}

function decodeValue(subtype, bytes, raw = false) {
  const v = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  if (subtype === TYPE_NIL) return null;
  if (subtype === TYPE_BOOLEAN) return bytes.byteLength >= 4 ? v.getInt32(0, true) !== 0 : false;
  if (subtype === TYPE_INTEGER) return bytes.byteLength >= 4 ? v.getInt32(0, true) : 0;
  return raw ? bytes.slice() : untext.decode(bytes);
}
