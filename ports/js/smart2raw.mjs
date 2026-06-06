/*
 * Smart2Raw JavaScript port
 * Copyright (C) 2026 Carlos Alberto Terencio Bastos
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

const MAGIC = 0x33335253; // portable v3.3 magic; bytes 53 52 33 33 ("SR33")
const FORMAT_VERSION = 1;
const FLAG_SIGNED = 1;

const U_LIMITS = [
  [8, 0n, 0xffn],
  [16, 0n, 0xffffn],
  [32, 0n, 0xffffffffn],
  [64, 0n, 0xffffffffffffffffn]
];

const I_LIMITS = [
  [-8, -0x80n, 0x7fn],
  [-16, -0x8000n, 0x7fffn],
  [-32, -0x80000000n, 0x7fffffffn],
  [-64, -0x8000000000000000n, 0x7fffffffffffffffn]
];

function toBig(value) {
  if (typeof value === 'bigint') return value;
  if (typeof value === 'number' && Number.isInteger(value)) return BigInt(value);
  throw new TypeError('Smart2Raw values must be integers or BigInt');
}

function byteWidth(size) {
  const w = Math.abs(size);
  if (w === 8) return 1;
  if (w === 16) return 2;
  if (w === 32) return 4;
  if (w === 64) return 8;
  throw new RangeError(`invalid Smart2Raw class: ${size}`);
}

function limitsFor(size) {
  const table = size < 0 ? I_LIMITS : U_LIMITS;
  const hit = table.find(([s]) => s === size);
  if (!hit) throw new RangeError(`invalid Smart2Raw class: ${size}`);
  return hit;
}

export function classifyUnsigned(value) {
  const v = toBig(value);
  for (const [size, min, max] of U_LIMITS) {
    if (v >= min && v <= max) return size;
  }
  throw new RangeError('unsigned value does not fit in 64 bits');
}

export function classifySigned(value) {
  const v = toBig(value);
  for (const [size, min, max] of I_LIMITS) {
    if (v >= min && v <= max) return size;
  }
  throw new RangeError('signed value does not fit in 64 bits');
}

export function classifyRange(minValue, maxValue, signed = false) {
  const min = toBig(minValue);
  const max = toBig(maxValue);
  if (min > max) throw new RangeError('min must be <= max');
  const table = signed ? I_LIMITS : U_LIMITS;
  for (const [size, lo, hi] of table) {
    if (min >= lo && max <= hi) return size;
  }
  throw new RangeError('range does not fit in 64 bits');
}

function crcTable() {
  if (crcTable.cache) return crcTable.cache;
  const table = new Uint32Array(256);
  for (let i = 0; i < 256; i++) {
    let c = i;
    for (let k = 0; k < 8; k++) {
      c = (c & 1) ? (0xedb88320 ^ (c >>> 1)) : (c >>> 1);
    }
    table[i] = c >>> 0;
  }
  crcTable.cache = table;
  return table;
}

export function crc32(bytes) {
  const table = crcTable();
  let c = 0xffffffff;
  for (const b of bytes) c = table[(c ^ b) & 0xff] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}

function ensureUint8Array(input) {
  if (input instanceof Uint8Array) return input;
  if (input instanceof ArrayBuffer) return new Uint8Array(input);
  if (ArrayBuffer.isView(input)) return new Uint8Array(input.buffer, input.byteOffset, input.byteLength);
  throw new TypeError('expected Uint8Array, ArrayBuffer, or typed array view');
}

function writeBigLE(out, offset, value, bytes) {
  let v = BigInt.asUintN(bytes * 8, toBig(value));
  for (let i = 0; i < bytes; i++) {
    out[offset + i] = Number(v & 0xffn);
    v >>= 8n;
  }
}

function readBigLE(bytes, offset, width, signed) {
  let v = 0n;
  for (let i = width - 1; i >= 0; i--) v = (v << 8n) | BigInt(bytes[offset + i]);
  return signed ? BigInt.asIntN(width * 8, v) : v;
}

export class Smart2RawPool {
  constructor({ signed = false, size = signed ? -8 : 8 } = {}) {
    if ((signed && size > 0) || (!signed && size < 0)) {
      throw new RangeError('size sign must match signed option');
    }
    limitsFor(size);
    this.signed = signed;
    this.size = size;
    this.values = [];
  }

  get count() {
    return this.values.length;
  }

  get byteLength() {
    return this.count * byteWidth(this.size);
  }

  _classFor(value) {
    return this.signed ? classifySigned(value) : classifyUnsigned(value);
  }

  _ensureFits(value) {
    const v = toBig(value);
    const [, min, max] = limitsFor(this.size);
    if (v >= min && v <= max) return;
    const next = this._classFor(v);
    if (byteWidth(next) > byteWidth(this.size)) this.size = next;
  }

  push(value) {
    const v = toBig(value);
    this._ensureFits(v);
    this.values.push(v);
    return this;
  }

  pushMany(values) {
    for (const value of values) this.push(value);
    return this;
  }

  get(index) {
    if (!Number.isInteger(index) || index < 0 || index >= this.values.length) {
      throw new RangeError('index out of range');
    }
    return this.values[index];
  }

  set(index, value) {
    if (!Number.isInteger(index) || index < 0 || index >= this.values.length) {
      throw new RangeError('index out of range');
    }
    const v = toBig(value);
    this._ensureFits(v);
    this.values[index] = v;
    return this;
  }

  removeSwap(index) {
    if (!Number.isInteger(index) || index < 0 || index >= this.values.length) {
      throw new RangeError('index out of range');
    }
    const last = this.values.pop();
    if (index < this.values.length) this.values[index] = last;
    return this;
  }

  sum() {
    let total = 0n;
    for (const value of this.values) total += value;
    return total;
  }

  min() {
    if (this.values.length === 0) return 0n;
    return this.values.reduce((a, b) => a < b ? a : b);
  }

  max() {
    if (this.values.length === 0) return 0n;
    return this.values.reduce((a, b) => a > b ? a : b);
  }

  fitClass() {
    if (this.values.length === 0) {
      this.size = this.signed ? -8 : 8;
      return this;
    }
    this.size = classifyRange(this.min(), this.max(), this.signed);
    return this;
  }


  sort() {
    this.values.sort((a, b) => a < b ? -1 : (a > b ? 1 : 0));
    return this;
  }

  isSorted() {
    for (let i = 1; i < this.values.length; i++) {
      if (this.values[i - 1] > this.values[i]) return false;
    }
    return true;
  }

  uniqueSorted() {
    if (this.values.length < 2) return this;
    const out = [this.values[0]];
    for (let i = 1; i < this.values.length; i++) {
      if (this.values[i] !== out[out.length - 1]) out.push(this.values[i]);
    }
    this.values = out;
    return this.fitClass();
  }

  nUnique() {
    return new Set(this.values.map(v => v.toString())).size;
  }

  valueCounts() {
    const counts = new Map();
    for (const value of this.values) {
      const key = value.toString();
      counts.set(key, (counts.get(key) ?? 0n) + 1n);
    }
    return counts;
  }

  toArray() {
    return [...this.values];
  }

  toS2RBytes() {
    const width = byteWidth(this.size);
    const payloadLen = this.values.length * width;
    const out = new Uint8Array(16 + payloadLen + 4);
    const view = new DataView(out.buffer, out.byteOffset, out.byteLength);
    view.setUint32(0, MAGIC, true);
    view.setInt8(4, this.size);
    view.setUint8(5, this.signed ? FLAG_SIGNED : 0);
    view.setUint8(6, FORMAT_VERSION);
    view.setUint8(7, 0);
    view.setBigUint64(8, BigInt(this.values.length), true);
    let p = 16;
    for (const value of this.values) {
      writeBigLE(out, p, value, width);
      p += width;
    }
    const check = crc32(out.subarray(16, 16 + payloadLen));
    view.setUint32(16 + payloadLen, check, true);
    return out;
  }

  static fromS2RBytes(input, { verifyCrc = true } = {}) {
    const bytes = ensureUint8Array(input);
    if (bytes.length < 20) throw new Error('corrupt .s2r: file too small');
    const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    if (view.getUint32(0, true) !== MAGIC) throw new Error('corrupt .s2r: bad magic');
    const size = view.getInt8(4);
    const flags = view.getUint8(5);
    const fmt = view.getUint8(6);
    if (fmt !== FORMAT_VERSION) throw new Error(`unsupported .s2r format version: ${fmt}`);
    const signed = (flags & FLAG_SIGNED) !== 0 || size < 0;
    const width = byteWidth(size);
    const count = Number(view.getBigUint64(8, true));
    if (!Number.isSafeInteger(count)) throw new Error('too many elements for JavaScript runtime');
    const payloadLen = count * width;
    if (bytes.length !== 16 + payloadLen + 4) throw new Error('corrupt .s2r: length mismatch');
    if (verifyCrc) {
      const want = view.getUint32(16 + payloadLen, true);
      const got = crc32(bytes.subarray(16, 16 + payloadLen));
      if (want !== got) throw new Error('corrupt .s2r: crc mismatch');
    }
    const pool = new Smart2RawPool({ signed, size });
    for (let i = 0, p = 16; i < count; i++, p += width) {
      pool.values.push(readBigLE(bytes, p, width, signed));
    }
    return pool;
  }
}
