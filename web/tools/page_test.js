const { chromium } = require('playwright');
(async () => {
  const b = await chromium.launch();
  const pg = await b.newPage({ viewport: { width: 1200, height: 1000 } });
  const errs = [];
  pg.on('console', m => { if (m.type() === 'error') errs.push(m.text()); });
  pg.on('pageerror', e => errs.push('pageerror: ' + e.message));
  await pg.goto('file://' + require('path').resolve('dist/smart2raw-live.html'));
  await pg.waitForFunction(() => document.getElementById('ver').textContent !== 'v—', {timeout: 15000});
  console.log('versao no cabecalho:', await pg.textContent('#ver'));

  const cases = [
    ['ts', 300000], ['telemetry', 300000], ['cat', 300000],
    ['rand', 200000], ['hash', 200000], ['const', 200000],
    ['sensor', 200000], ['bool', 300000], ['money', 200000], ['ids', 300000]
  ];
  for (const [k, n] of cases) {
    await pg.selectOption('#preset', k);
    await pg.fill('#npreset', String(n));
    await pg.click('#gen');
    await pg.click('#run');
    await pg.waitForFunction(() => document.getElementById('status').textContent.includes('análise completa'), {timeout: 60000});
    const r = await pg.evaluate(() => ({
      cls: document.getElementById('k_cls').textContent,
      ratio: document.getElementById('k_ratio').textContent,
      form: document.getElementById('k_form').textContent,
      ver: document.getElementById('b_verified').textContent,
      exp: document.getElementById('b_expand').textContent,
      rt: document.getElementById('b_round').textContent,
      file: document.getElementById('fileinfo').textContent,
      planRows: document.querySelectorAll('#t_plan tbody tr').length
    }));
    await pg.click('#bench');
    await pg.waitForFunction(() => document.getElementById('benchnote').textContent.length > 0, {timeout: 30000});
    const bn = await pg.evaluate(() => ({
      note: document.getElementById('benchnote').textContent.slice(0, 60),
      rows: [...document.querySelectorAll('#t_bench tbody tr')].map(tr => tr.innerText.replace(/\s+/g, ' '))
    }));
    const bad = !r.ver.startsWith('✓') || !r.exp.startsWith('✓') || !bn.note.startsWith('✓');
    console.log((bad ? 'FALHA ' : 'ok    ') + k.padEnd(10), r.cls.padEnd(7), r.ratio.padEnd(8),
                r.form.padEnd(22), '| plano:' + r.planRows, '|', r.file);
    console.log('        ', bn.rows.join(' || '));
    if (bad) { console.log('        ', r.ver, r.exp, bn.note); process.exitCode = 1; }
  }

  // o download: os bytes precisam ser um .s2r de verdade
  await pg.selectOption('#preset', 'ts'); await pg.fill('#npreset', '100000');
  await pg.click('#gen'); await pg.click('#run');
  await pg.waitForFunction(() => document.getElementById('status').textContent.includes('análise completa'));
  const [dl] = await Promise.all([pg.waitForEvent('download'), pg.click('#dl')]);
  const path = 'out/baixado.s2r';
  await dl.saveAs(path);
  const fs = require('fs');
  const buf = fs.readFileSync(path);
  console.log('arquivo baixado:', dl.suggestedFilename(), buf.length, 'bytes, magic =',
              buf.readUInt32LE(0).toString(16), 'fmt =', buf[6]);

  await pg.screenshot({ path: 'out/page.png', fullPage: true });
  console.log(errs.length ? 'ERROS NO CONSOLE: ' + errs.join(' | ') : 'console limpo');
  await b.close();
})();
