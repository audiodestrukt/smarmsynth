// Page half: boot the worklet, draw the swarm, wire the controls.

const P = {
  volume: 0, pan: 1, resonance: 2, noise: 3,
  vol_var: 4, pitch_var: 5, pan_var: 6, res_var: 7, noise_var: 8,
  speed: 9, speed_lfo: 10, std_dev: 11, reflection: 12,
  attract: 13, repel: 14, proximity: 15,
  lowpass: 16, q: 17, overdrive: 18, oscillators: 19,
};

// laid out like the original's five columns, then its motion row
const COLUMNS = [
  { name: 'VOL',   hue: 232, home: P.volume,    range: P.vol_var },
  { name: 'PITCH', hue: 145, home: null,        range: P.pitch_var },
  { name: 'PAN',   hue: 0,   home: P.pan,       range: P.pan_var },
  { name: 'RES',   hue: 55,  home: P.resonance, range: P.res_var },
  { name: 'NOISE', hue: 280, home: P.noise,     range: P.noise_var },
];
const MOTION = [
  ['Speed', P.speed], ['LFO', P.speed_lfo], ['Std Dev', P.std_dev],
  ['Reflect', P.reflection], ['Attract', P.attract], ['Repel', P.repel],
  ['Proximity', P.proximity], ['Osc', P.oscillators],
  ['LoPass', P.lowpass], ['Q', P.q], ['Overdrive', P.overdrive],
];

let ctx = null, node = null, params = null, display = null;
let particles = { count: 0, flat: new Float32Array(0) };
let held = new Set();

const $ = (s) => document.querySelector(s);
const statusEl = $('#status');

function setStatus(t, busy) {
  statusEl.textContent = t;
  statusEl.dataset.busy = busy ? '1' : '';
}

// ---------------------------------------------------------------- audio boot

async function start() {
  if (ctx) return;
  setStatus('starting audio…', true);
  ctx = new AudioContext({ latencyHint: 'interactive' });
  await ctx.resume();

  // A worklet can neither import nor fetch, so the Emscripten glue and the
  // processor are concatenated into one classic script and passed as a blob.
  // The wasm is compiled out here and handed over already built, because a
  // worklet constructor cannot await and Chrome refuses synchronous
  // compilation above 4 KB.
  const [glue, proc, wasmBytes] = await Promise.all([
    fetch('dist/swarm.js').then((r) => r.text()),
    fetch('src/processor.js').then((r) => r.text()),
    fetch('dist/swarm.wasm').then((r) => r.arrayBuffer()),
  ]);
  const wasmModule = await WebAssembly.compile(wasmBytes);

  const blob = new Blob([glue, '\n', proc], { type: 'application/javascript' });
  const url = URL.createObjectURL(blob);
  await ctx.audioWorklet.addModule(url);
  URL.revokeObjectURL(url);

  node = new AudioWorkletNode(ctx, 'swarm-processor', {
    numberOfInputs: 0, numberOfOutputs: 1, outputChannelCount: [2],
    processorOptions: { wasmModule },
  });
  node.onprocessorerror = () => setStatus('the audio engine failed to start', false);
  node.port.onmessage = (e) => {
    const m = e.data;
    if (m.type === 'params') { params = m.values; display = m.display; refreshControls(); }
    else if (m.type === 'particles') particles = { count: m.count, flat: m.flat };
  };
  node.connect(ctx.destination);

  await loadPresetList();
  setStatus('ready — play the keyboard, or press a key A–L', false);
  document.body.dataset.running = '1';
}

const send = (msg) => node && node.port.postMessage(msg);

// ------------------------------------------------------------------ presets

let patchIndex = [];
let patchBlob = null;

async function loadPresetList() {
  const sel = $('#preset');
  try {
    const [index, blob] = await Promise.all([
      fetch('dist/patches.json').then((r) => { if (!r.ok) throw new Error(r.status); return r.json(); }),
      fetch('dist/patches.bin').then((r) => { if (!r.ok) throw new Error(r.status); return r.arrayBuffer(); }),
    ]);
    patchIndex = index;
    patchBlob = blob;
  } catch (e) {
    // say so rather than leaving a dropdown that silently does nothing
    setStatus('could not load the factory patches (' + e.message + ')', false);
    patchIndex = []; patchBlob = null;
  }
  sel.innerHTML = '<option value="">— factory patches —</option>';
  patchIndex.forEach((p, i) => {
    const o = document.createElement('option');
    o.value = String(i);
    o.textContent = p.name;
    sel.appendChild(o);
  });
}

function loadPreset(which) {
  if (which === '' || patchBlob === null) return;
  const entry = patchIndex[Number(which)];
  if (!entry) return;
  const bytes = new Uint8Array(patchBlob, entry.offset, entry.size);
  send({ type: 'patch', bytes: new Uint8Array(bytes) });   // copy, the blob is reused
  $('#patchname').textContent = entry.name;
}

// ----------------------------------------------------------------- controls

const knobs = [];

function buildControls() {
  const cols = $('#columns');
  for (const c of COLUMNS) {
    const el = document.createElement('div');
    el.className = 'column';
    el.style.setProperty('--h', c.hue);
    el.innerHTML = `<div class="colname">${c.name}</div>`;
    for (const [label, idx] of [['home', c.home], ['range', c.range]]) {
      if (idx === null) {
        el.insertAdjacentHTML('beforeend',
          `<div class="knob-slot inert"><span class="klabel">coarse</span>
           <div class="knob"></div><span class="kval">0 s/t</span></div>`);
        continue;
      }
      el.appendChild(makeKnob(label, idx, c.hue));
    }
    cols.appendChild(el);
  }

  const row = $('#motion');
  for (const [label, idx] of MOTION) row.appendChild(makeKnob(label, idx, 205));
}

function makeKnob(label, index, hue) {
  const slot = document.createElement('div');
  slot.className = 'knob-slot';
  slot.style.setProperty('--h', hue);
  slot.innerHTML = `<span class="klabel">${label}</span>
    <div class="knob" tabindex="0" role="slider" aria-label="${label}"
         aria-valuemin="0" aria-valuemax="100"><i></i></div>
    <span class="kval">—</span>`;

  const dial = slot.querySelector('.knob');
  const val = slot.querySelector('.kval');
  let dragging = false, startY = 0, startV = 0;

  const set = (v) => {
    v = Math.max(0, Math.min(1, v));
    send({ type: 'param', index, value: v });
    if (params) params[index] = v;
    paint(v);
  };
  const paint = (v) => {
    dial.style.setProperty('--v', v);
    dial.setAttribute('aria-valuenow', Math.round(v * 100));
    val.textContent = display ? formatValue(index, display[index]) : Math.round(v * 100);
  };

  dial.addEventListener('pointerdown', (e) => {
    dragging = true; startY = e.clientY;
    startV = params ? params[index] : 0.5;
    dial.setPointerCapture(e.pointerId);
  });
  dial.addEventListener('pointermove', (e) => {
    if (!dragging) return;
    set(startV + (startY - e.clientY) / 180);
  });
  dial.addEventListener('pointerup', () => { dragging = false; send({ type: 'sync' }); });
  dial.addEventListener('keydown', (e) => {
    const step = e.shiftKey ? 0.01 : 0.05;
    if (e.key === 'ArrowUp' || e.key === 'ArrowRight') { set((params?.[index] ?? 0) + step); e.preventDefault(); }
    if (e.key === 'ArrowDown' || e.key === 'ArrowLeft') { set((params?.[index] ?? 0) - step); e.preventDefault(); }
  });

  knobs.push({ index, paint });
  return slot;
}

function formatValue(index, d) {
  if (index === P.volume) return (d <= -999 ? '-inf' : d.toFixed(1)) + ' dB';
  if (index === P.oscillators) return String(Math.round(d));
  return String(Math.round(d));
}

function refreshControls() {
  if (!params) return;
  for (const k of knobs) k.paint(params[k.index]);
}

// --------------------------------------------------------------------- view

const cv = $('#view');
const g = cv.getContext('2d');

function plane(z, w, h) {
  const pad = 0.055, d = 0.62;
  const fx = w * pad, fy = h * pad, fw = w * (1 - pad * 2), fh = h * (1 - pad * 2);
  const bw = fw * d, bh = fh * d;
  const bx = fx + (fw - bw) / 2, by = fy + (fh - bh) / 2 - h * 0.02;
  const t = 1 - z;
  return { x: fx + (bx - fx) * t, y: fy + (by - fy) * t,
           w: fw + (bw - fw) * t, h: fh + (bh - fh) * t };
}

function drawView() {
  const rect = cv.getBoundingClientRect();
  const dpr = Math.min(window.devicePixelRatio || 1, 2);
  if (cv.width !== Math.round(rect.width * dpr)) {
    cv.width = Math.round(rect.width * dpr);
    cv.height = Math.round(rect.height * dpr);
  }
  g.setTransform(dpr, 0, 0, dpr, 0, 0);
  const w = rect.width, h = rect.height;

  g.clearRect(0, 0, w, h);
  const front = plane(1, w, h), back = plane(0, w, h);

  const grad = g.createLinearGradient(0, 0, 0, h);
  grad.addColorStop(0, 'rgba(255,255,255,.05)');
  grad.addColorStop(1, 'rgba(0,0,0,.18)');
  g.fillStyle = '#8b8b8b';
  g.fillRect(front.x, front.y, front.w, front.h);
  g.fillStyle = grad;
  g.fillRect(front.x, front.y, front.w, front.h);
  g.fillStyle = 'rgba(255,255,255,.07)';
  g.fillRect(back.x, back.y, back.w, back.h);

  g.strokeStyle = 'rgba(255,255,255,.32)';
  g.lineWidth = 1;
  g.strokeRect(front.x + .5, front.y + .5, front.w, front.h);
  g.strokeRect(back.x + .5, back.y + .5, back.w, back.h);
  g.beginPath();
  for (const [cx, cy] of [[0,0],[1,0],[1,1],[0,1]]) {
    g.moveTo(front.x + cx * front.w, front.y + cy * front.h);
    g.lineTo(back.x + cx * back.w, back.y + cy * back.h);
  }
  g.stroke();

  // particles, far ones first
  const n = particles.count, f = particles.flat;
  const order = [];
  for (let i = 0; i < n; i++) order.push(i);
  order.sort((a, b) => f[a * 7 + 2] - f[b * 7 + 2]);

  for (const i of order) {
    const pan = f[i * 7], pitch = f[i * 7 + 1], vol = f[i * 7 + 2];
    const res = f[i * 7 + 3], noise = f[i * 7 + 4];
    const vpan = f[i * 7 + 5], vpitch = f[i * 7 + 6];

    // the measured colour law: hue from Res, HSL lightness from Noise
    const col = `hsl(${(0.79 * res * 360).toFixed(0)} 100% ${((0.21 + 0.60 * noise) * 100).toFixed(0)}%)`;

    const pl = plane(vol, w, h);
    const x = pl.x + pl.w * pan;
    const y = pl.y + pl.h * (1 - pitch);

    // streaks are NOT clamped short here -- in the original a fast, pale
    // particle draws a line right across the box
    const sx = vpan * pl.w * 0.06;
    const sy = -vpitch * pl.h * 0.06;
    if (Math.abs(sx) + Math.abs(sy) > 0.6) {
      g.strokeStyle = col;
      g.globalAlpha = 0.6;
      g.lineWidth = Math.max(1.2, 2.6 * vol);
      g.beginPath(); g.moveTo(x - sx, y - sy); g.lineTo(x, y); g.stroke();
      g.globalAlpha = 1;
    }
    g.fillStyle = col;
    const sz = 1.6 + vol * 2.2;
    g.beginPath(); g.arc(x, y, sz, 0, Math.PI * 2); g.fill();
  }

  requestAnimationFrame(drawView);
}

// ----------------------------------------------------------------- keyboard

const KEYMAP = { a:0, w:1, s:2, e:3, d:4, f:5, t:6, g:7, y:8, h:9, u:10, j:11, k:12, o:13, l:14 };
let octave = 4;

function buildKeyboard() {
  const kb = $('#keys');
  const pattern = [0,1,0,1,0,0,1,0,1,0,1,0];
  for (let n = 36; n <= 84; n++) {
    const black = pattern[n % 12] === 1;
    const el = document.createElement('button');
    el.className = 'key' + (black ? ' black' : '');
    el.dataset.note = n;
    el.setAttribute('aria-label', 'note ' + n);
    kb.appendChild(el);
  }
  kb.addEventListener('pointerdown', (e) => {
    const k = e.target.closest('.key'); if (!k) return;
    noteOn(+k.dataset.note); k.setPointerCapture(e.pointerId);
  });
  kb.addEventListener('pointerup', () => noteOff());
  kb.addEventListener('pointercancel', () => noteOff());
}

function noteOn(n) {
  if (!ctx) { start().then(() => noteOn(n)); return; }
  held.add(n);
  send({ type: 'noteOn', note: n, velocity: 0.85 });
  document.querySelectorAll('.key').forEach((k) => {
    k.classList.toggle('on', held.has(+k.dataset.note));
  });
}
function noteOff() {
  held.clear();
  send({ type: 'noteOff' });
  document.querySelectorAll('.key.on').forEach((k) => k.classList.remove('on'));
}

window.addEventListener('keydown', (e) => {
  if (e.repeat || e.metaKey || e.ctrlKey) return;
  if (e.target.matches('input, select, button, .knob')) return;
  const k = KEYMAP[e.key.toLowerCase()];
  if (k === undefined) {
    if (e.key === 'z') { octave = Math.max(1, octave - 1); e.preventDefault(); }
    if (e.key === 'x') { octave = Math.min(7, octave + 1); e.preventDefault(); }
    return;
  }
  e.preventDefault();
  noteOn(octave * 12 + k);
});
window.addEventListener('keyup', (e) => {
  if (KEYMAP[e.key.toLowerCase()] !== undefined) noteOff();
});

// --------------------------------------------------------------------- wire

buildControls();
buildKeyboard();
drawView();

$('#start').addEventListener('click', start);
$('#preset').addEventListener('change', (e) => {
  const v = e.target.value;
  start().then(() => loadPreset(v));
});
$('#anarchy').addEventListener('click', () => {
  start().then(() => {
    const seed = (Math.random() * 0xffffffff) >>> 0;
    send({ type: 'anarchy', seed });
    $('#patchname').textContent = 'random patch #' + seed.toString(16).slice(0, 4).toUpperCase();
    $('#preset').value = '';
  });
});
