// SPDX-License-Identifier: GPL-2.0-or-later
// Zrno render worker: the camera's effect kernels and a developer for
// Zrno's sensor backups, compiled to WebAssembly (zrno.wasm, built from web/).
let x = null;
let lutName = '';
const lutBytes = new Map();
let pending = null;
let busy = false;

const mem = () => new Uint8Array(x.memory.buffer);
function put(bytes) {
  const p = x.fl_alloc(bytes.length);
  mem().set(bytes, p);
  return p;
}

async function init() {
  const res = await fetch('zrno.wasm');
  if (!res.ok) throw new Error('zrno.wasm: HTTP ' + res.status);
  const { instance } = await WebAssembly.instantiate(await res.arrayBuffer());
  x = instance.exports;
  // The engines' menu labels and grade ranges, from the camera's kernels.
  const p = x.fl_names(), m = mem();
  let end = p;
  while (m[end]) end++;
  postMessage({ type: 'ready', names: JSON.parse(new TextDecoder().decode(m.slice(p, end))) });
}

function loaded(rc, ms, kind, seq) {
  postMessage({
    type: 'loaded', rc, ms, kind, seq,
    width: x.fl_width(), height: x.fl_height(),
    wb: [0, 1, 2, 3].map((i) => x.fl_wb(i)),
  });
}

async function setLut(name) {
  if (name === lutName) return;
  if (!lutBytes.size) {
    // luts.json: { "PORTRA.LUT": "<base64>", ... }, the card's 16 looks.
    const res = await fetch('luts.json');
    if (!res.ok) throw new Error('luts.json: HTTP ' + res.status);
    for (const [file, b64] of Object.entries(await res.json())) {
      lutBytes.set(file, Uint8Array.from(atob(b64), (c) => c.charCodeAt(0)));
    }
  }
  const bytes = lutBytes.get(name);
  if (!bytes) throw new Error(name + ': not in luts.json');
  const p = put(bytes);
  const rc = x.fl_set_lut(p, bytes.length);
  x.fl_free(p, bytes.length);
  if (rc) throw new Error(name + ': not a G12 LUT (' + rc + ')');
  lutName = name;
}

// PRINT.BIN (645,760 bytes), fetched when Print first renders.
let printLoaded = false;
async function loadPrint() {
  if (printLoaded) return;
  const res = await fetch('print.bin');
  if (!res.ok) throw new Error('print.bin: HTTP ' + res.status);
  const bytes = new Uint8Array(await res.arrayBuffer());
  const p = put(bytes);
  const rc = x.fl_set_print_data(p, bytes.length);
  x.fl_free(p, bytes.length);
  if (rc) throw new Error('print.bin is not a Zrno PRINT.BIN');
  printLoaded = true;
}

// ZrnoBoy (1), Print (2) or Grade (3) over the developed frame.
async function renderEngine(m) {
  if (m.engine === 2) await loadPrint();
  const bytes = m.engine === 3 ? new Uint8Array(Int8Array.from(m.values).buffer) : Uint8Array.from(m.values);
  const p = put(bytes);
  const e = m.exposure || {};
  const rc = x.fl_render_engine(m.engine, p, m.settings.seed >>> 0, e.tv96 ?? 0, e.av96 ?? 0, e.iso ?? 0, e.ev96 ?? 0);
  x.fl_free(p, bytes.length);
  return rc;
}

async function drain() {
  busy = true;
  while (pending) {
    const m = pending;
    pending = null;
    try {
      let rc, ms;
      if (m.engine && m.mode !== 2) {
        const t0 = performance.now();
        rc = await renderEngine(m);
        ms = performance.now() - t0;
      } else {
        await setLut(m.look);
        const t = new Uint8Array(25);
        t.set(new TextEncoder().encode(m.text).slice(0, 24));
        const tp = put(t);
        const t0 = performance.now();
        const s = m.settings;
        rc = x.fl_render(m.mode, s.halation, s.bloom, s.grain, s.vignette, s.leaks, s.toy,
          s.dust, s.dustDark, s.stamp, s.seed >>> 0, tp, s.soft);
        ms = performance.now() - t0;
        x.fl_free(tp, 25);
      }
      if (rc) {
        postMessage({ type: 'error', message: 'Render failed (code ' + rc + ')' });
        continue;
      }
      const w = x.fl_width(), h = x.fl_height(), o = x.fl_output();
      const pixels = mem().slice(o, o + w * h * 4);
      postMessage({ type: 'rendered', id: m.id, seq: m.seq, ms, width: w, height: h, pixels: pixels.buffer }, [pixels.buffer]);
    } catch (err) {
      postMessage({ type: 'error', message: String(err.message || err) });
    }
  }
  busy = false;
}

onmessage = (e) => {
  const m = e.data;
  try {
    if (m.type === 'load-raw') {
      const b = new Uint8Array(m.buffer);
      const p = put(b);
      const t0 = performance.now();
      const [r, g, bl] = m.levels || [0, 0, 0];
      const rc = x.fl_load_raw(p, b.length, r, g, bl);
      const ms = performance.now() - t0;
      x.fl_free(p, b.length);
      loaded(rc, ms, 'raw', m.seq);
    } else if (m.type === 'set-wb') {
      const [r, g, bl] = m.levels || [0, 0, 0];
      const t0 = performance.now();
      const rc = x.fl_set_wb(r, g, bl);
      loaded(rc, performance.now() - t0, 'raw', m.seq);
    } else if (m.type === 'load-rgba') {
      const b = new Uint8Array(m.buffer);
      const p = put(b);
      const rc = x.fl_load_rgba(p, m.width, m.height);
      x.fl_free(p, b.length);
      loaded(rc, 0, m.kind, m.seq);
    } else if (m.type === 'render') {
      pending = m;
      if (!busy) drain();
    }
  } catch (err) {
    postMessage({ type: 'error', message: String(err.message || err) });
  }
};

init().catch((err) => postMessage({ type: 'fatal', message: String(err.message || err) }));
