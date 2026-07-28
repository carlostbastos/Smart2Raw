// O mesmo lote, muitas vezes, no MESMO módulo: o relatório não pode mudar e a
// memória não pode crescer sem parar. É o teste do alocador que o s2r_rt.c traz.
const fs = require('fs');
(async () => {
  const mod = await WebAssembly.instantiate(fs.readFileSync('out/s2r.wasm'), {});
  const x = mod.instance.exports;
  const buf = fs.readFileSync('out/shapes.bin');
  const slots = x.s2r_probe_slots();
  let first = null, pages = [];
  for (let round = 0; round < 8; round++) {
    let off = 4, lines = [];
    const nsh = buf.readUInt32LE(0);
    for (let s = 0; s < nsh; s++) {
      const n = buf.readUInt32LE(off); off += 4;
      const sg = buf.readUInt32LE(off); off += 4;
      const ptr = x.s2r_probe_input(n);
      new Uint8Array(x.memory.buffer).set(buf.subarray(off, off + n*8), ptr);
      off += n*8;
      x.s2r_probe_run(n, sg);
      const dv = new DataView(x.memory.buffer), rp = x.s2r_probe_report();
      let l = '';
      for (let i = 0; i < slots; i++) l += dv.getBigUint64(rp + i*8, true) + ' ';
      lines.push(l);
    }
    const j = lines.join('\n');
    if (first === null) first = j;
    else if (j !== first) { console.error('DIVERGIU na rodada ' + round); process.exit(1); }
    pages.push(x.memory.buffer.byteLength >> 16);
  }
  // uma coluna grande, depois pequena de novo: o heap tem de ser reutilizado
  const big = 2000000;
  const p = x.s2r_probe_input(big);
  const dv2 = new DataView(x.memory.buffer);
  for (let i = 0; i < big; i++) dv2.setBigUint64(p + i*8, BigInt(1700000000 + i*60), true);
  const t0 = process.hrtime.bigint();
  const ok = x.s2r_probe_run(big, 0);
  const t1 = process.hrtime.bigint();
  const rp = x.s2r_probe_report(), dv3 = new DataView(x.memory.buffer);
  const get = i => dv3.getBigUint64(rp + i*8, true);
  console.log('8 rodadas identicas: sim');
  console.log('paginas de 64 KB por rodada:', pages.join(','));
  console.log('coluna de 2.000.000: verificado=' + ok +
              ' bruto=' + get(9) + ' melhor=' + get(21) +
              ' nunca_expande=' + get(36) +
              ' em ' + Number(t1-t0)/1e6 + ' ms');
  console.log('memoria final: ' + (x.memory.buffer.byteLength >> 20) + ' MB');
})().catch(e => { console.error(e); process.exit(1); });
