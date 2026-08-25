// The AudioWorkletProcessor half.
//
// This file is never loaded on its own: the page concatenates the Emscripten
// glue in front of it and hands the pair to addModule as a blob, because a
// worklet can neither import modules nor fetch. `createSwarm` therefore already
// exists in scope by the time this runs.

class SwarmProcessor extends AudioWorkletProcessor {
  constructor(options) {
    super();
    this.ready = false;
    this.blocks = 0;

    // The page compiled the WebAssembly.Module for us and passed it in, because
    // Chrome refuses synchronous compilation above 4 KB and a worklet
    // constructor cannot await. Instantiating an already-compiled module
    // synchronously is allowed at any size.
    const compiled = options.processorOptions.wasmModule;
    this.mod = createSwarm({
      instantiateWasm: (imports, done) => {
        const instance = new WebAssembly.Instance(compiled, imports);
        done(instance);
        return instance.exports;
      },
    });
    this.mod._swarm_init(sampleRate, 512);

    // scratch buffers inside the wasm heap, reused every block
    this.cap = 512;
    this.pL = this.mod._malloc(this.cap * 4);
    this.pR = this.mod._malloc(this.cap * 4);
    this.ready = true;

    const initial = options?.processorOptions?.patch;
    if (initial) this.loadPatch(initial);

    this.port.onmessage = (e) => {
      const m = e.data;
      if (m.type === 'param')    this.mod._swarm_set_param(m.index, m.value);
      else if (m.type === 'noteOn')  this.mod._swarm_note_on(m.note, m.velocity ?? 0.8);
      else if (m.type === 'noteOff') this.mod._swarm_note_off();
      else if (m.type === 'patch')   this.loadPatch(m.bytes);
      else if (m.type === 'anarchy') { this.mod._swarm_anarchy(m.seed >>> 0); this.sendParams(); }
      else if (m.type === 'sync')    this.sendParams();
    };

    this.sendParams();
  }

  loadPatch(bytes) {
    const n = bytes.length;
    const p = this.mod._malloc(n);
    this.mod.HEAPU8.set(bytes, p);
    const ok = this.mod._swarm_load_patch(p, n);
    this.mod._free(p);
    if (ok) this.sendParams();
  }

  /** Mirrors the engine's parameter values back to the page. */
  sendParams() {
    const n = this.mod._swarm_num_params();
    const values = new Float32Array(n);
    const display = new Float32Array(n);
    for (let i = 0; i < n; i++) {
      values[i] = this.mod._swarm_get_param(i);
      display[i] = this.mod._swarm_param_display(i);
    }
    this.port.postMessage({ type: 'params', values, display }, [values.buffer, display.buffer]);
  }

  process(inputs, outputs) {
    if (!this.ready) return true;
    const out = outputs[0];
    const frames = out[0].length;

    if (frames > this.cap) {
      this.mod._free(this.pL); this.mod._free(this.pR);
      this.cap = frames;
      this.pL = this.mod._malloc(this.cap * 4);
      this.pR = this.mod._malloc(this.cap * 4);
    }

    this.mod._swarm_process(this.pL, this.pR, frames);

    const L = this.mod.HEAPF32.subarray(this.pL >> 2, (this.pL >> 2) + frames);
    const R = this.mod.HEAPF32.subarray(this.pR >> 2, (this.pR >> 2) + frames);
    out[0].set(L);
    if (out.length > 1) out[1].set(R);

    // particle positions for the view, a few times a second rather than every block
    if ((this.blocks++ & 7) === 0) {
      const count = this.mod._swarm_particles();
      const ptr = this.mod._swarm_particle_buffer();
      const flat = this.mod.HEAPF32.slice(ptr >> 2, (ptr >> 2) + count * 7);
      this.port.postMessage({ type: 'particles', count, flat }, [flat.buffer]);
    }
    return true;
  }
}

registerProcessor('swarm-processor', SwarmProcessor);
