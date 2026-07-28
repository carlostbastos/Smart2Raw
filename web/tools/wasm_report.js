// Runs the wasm build over the same shapes.bin the native build reads, and
// prints the same lines, so `diff` is the whole verification.
const fs = require('fs');

const wasmPath = process.argv[2] || 'out/s2r.wasm';
const binPath  = process.argv[3] || 'out/shapes.bin';

(async () => {
  const mod = await WebAssembly.instantiate(fs.readFileSync(wasmPath), {});
  const x = mod.instance.exports;
  const mem = () => new DataView(x.memory.buffer);
  const u8  = () => new Uint8Array(x.memory.buffer);

  const buf = fs.readFileSync(binPath);
  let off = 0;
  const nsh = buf.readUInt32LE(off); off += 4;
  const slots = x.s2r_probe_slots();
  const out = [];
  for (let s = 0; s < nsh; s++) {
    const n  = buf.readUInt32LE(off); off += 4;
    const sg = buf.readUInt32LE(off); off += 4;
    const ptr = x.s2r_probe_input(n);
    if (!ptr) throw new Error('oom');
    u8().set(buf.subarray(off, off + n * 8), ptr);
    off += n * 8;
    x.s2r_probe_run(n, sg);
    const rp = x.s2r_probe_report();
    const dv = mem();
    let line = 'shape ' + s;
    for (let i = 0; i < slots; i++) line += ' ' + dv.getBigUint64(rp + i * 8, true).toString();
    line += ' q ' + (x.s2r_probe_count_gt_s2r(1000n) >>> 0) +
            ' '   + (x.s2r_probe_count_gt_naive(1000n) >>> 0);
    line += ' f ' + (x.s2r_probe_file_len() >>> 0);
    out.push(line);
  }
  process.stdout.write(out.join('\n') + '\n');
})().catch(e => { console.error(e); process.exit(1); });
