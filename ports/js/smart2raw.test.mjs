/*
 * Smart2Raw JavaScript port tests
 * Copyright (C) 2026 Carlos Alberto Terêncio de Bastos
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, resolve } from 'node:path';
import { Smart2RawPool, classifyUnsigned, classifySigned, classifyRange, crc32 } from './smart2raw.mjs';

const __dirname = dirname(fileURLToPath(import.meta.url));

test('classifies unsigned and signed values', () => {
  assert.equal(classifyUnsigned(255), 8);
  assert.equal(classifyUnsigned(256), 16);
  assert.equal(classifyUnsigned(0xffffffffn + 1n), 64);
  assert.equal(classifySigned(-128), -8);
  assert.equal(classifySigned(128), -16);
  assert.equal(classifyRange(-10, 35, true), -8);
});

test('unsigned pool promotes and sums exactly', () => {
  const p = new Smart2RawPool();
  p.pushMany([1, 2, 255]);
  assert.equal(p.size, 8);
  p.push(256);
  assert.equal(p.size, 16);
  assert.equal(p.count, 4);
  assert.equal(p.get(3), 256n);
  assert.equal(p.sum(), 514n);
});

test('signed pool promotes and demotes after outlier removal', () => {
  const p = new Smart2RawPool({ signed: true });
  p.pushMany([-10, -3, 20, 35]);
  assert.equal(p.size, -8);
  p.push(100000);
  assert.equal(p.size, -32);
  p.removeSwap(4).fitClass();
  assert.equal(p.size, -8);
  assert.equal(p.sum(), 42n);
});

test('serializes and loads portable s2r bytes', () => {
  const p = new Smart2RawPool({ signed: true });
  p.pushMany([-10, -3, 20, 35]);
  const bytes = p.toS2RBytes();
  const q = Smart2RawPool.fromS2RBytes(bytes);
  assert.equal(q.signed, true);
  assert.equal(q.size, -8);
  assert.deepEqual(q.toArray(), [-10n, -3n, 20n, 35n]);
});

test('detects crc corruption', () => {
  const p = new Smart2RawPool();
  p.pushMany([1, 2, 3]);
  const bytes = p.toS2RBytes();
  bytes[16] ^= 0xff;
  assert.throws(() => Smart2RawPool.fromS2RBytes(bytes), /crc mismatch/);
});

test('crc32 known vector', () => {
  const bytes = new TextEncoder().encode('123456789');
  assert.equal(crc32(bytes), 0xcbf43926);
});


test('loads canonical conformance fixtures', () => {
  const root = resolve(__dirname, '..', '..', 'conformance', 'fixtures');
  const manifest = JSON.parse(readFileSync(resolve(root, 'manifest.json'), 'utf8'));
  for (const fx of manifest) {
    const bytes = readFileSync(resolve(root, fx.file));
    const p = Smart2RawPool.fromS2RBytes(bytes);
    assert.equal(p.size, fx.class, fx.file);
    assert.equal(p.count, fx.count, fx.file);
    assert.equal(p.sum(), BigInt(fx.sum), fx.file);
    assert.deepEqual(p.toArray(), fx.values.map(BigInt), fx.file);
  }
  const corrupted = readFileSync(resolve(root, 'corrupted_crc.s2r'));
  assert.throws(() => Smart2RawPool.fromS2RBytes(corrupted), /crc mismatch/);
});


test('analytics v2: sort, unique and value counts', () => {
  const p = new Smart2RawPool({ signed: true });
  p.pushMany([10, -1, -128, 10, 0, -1]);
  assert.equal(p.isSorted(), false);
  p.sort();
  assert.equal(p.isSorted(), true);
  assert.deepEqual(p.toArray(), [-128n, -1n, -1n, 0n, 10n, 10n]);
  assert.equal(p.nUnique(), 4);
  const counts = p.valueCounts();
  assert.equal(counts.get('-1'), 2n);
  assert.equal(counts.get('10'), 2n);
  p.uniqueSorted();
  assert.deepEqual(p.toArray(), [-128n, -1n, 0n, 10n]);
  assert.equal(p.size, -8);
});
