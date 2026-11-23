// Node: typed-array fast paths (Int32Array/BigInt64Array/Float64Array call
// libc++ std::sort in V8), Array.sort with a JS comparator, strings and records.
// Usage: node --expose-gc sort.mjs IMPL TYPE SHAPE FILE N REPS
import fs from 'node:fs';

const WARMUP_MS = 300;
const TARGET_MS = 200;
const MAX_SORTS = 20000;

const LANG = typeof Bun === 'undefined' ? 'node' : 'bun';
const [impl, type, shape, file, nStr, repsStr] = process.argv.slice(2);
const n = Number(nStr);
const reps = Number(repsStr);

function emit(rep, ms, ok) {
  console.log(JSON.stringify({
    lang: LANG, impl, type, dist: shape, n, rep,
    ms: Number(ms.toFixed(4)), ns_el: Number((ms * 1e6 / n).toFixed(3)), ok,
  }));
}

function batchCount(pilotMs) {
  if (!(pilotMs > 0)) return 1;
  return Math.min(MAX_SORTS, Math.max(1, Math.ceil(TARGET_MS / pilotMs)));
}

function repeat(doSort, copy, check) {
  let pilot = 0;
  const warm = performance.now();
  do {
    copy();
    const t0 = performance.now();
    doSort();
    pilot = performance.now() - t0;
  } while (performance.now() - warm < WARMUP_MS);

  const K = batchCount(pilot);
  for (let rep = 1; rep <= reps; rep++) {
    copy();
    global.gc?.();
    let total = 0;
    for (let k = 0; k < K; k++) {
      const t0 = performance.now();
      doSort();
      total += performance.now() - t0;
      if (k + 1 < K) copy();
    }
    emit(rep, total / K, check());
  }
}

const buf = fs.readFileSync(file);
let src, work;

function aligned(cls, bytes) {
  return new cls(buf.buffer.slice(buf.byteOffset, buf.byteOffset + bytes));
}

function recWord(key, w) {
  switch (w) {
    case 0: return Math.imul(key, 2654435761) >>> 0;
    case 1: return (~key) >>> 0;
    case 2: return (key + 0x9e3779b9) >>> 0;
    default: return (key ^ Math.imul(0x85ebca6b, w)) >>> 0;
  }
}

function loadRecs(esz, words) {
  const view = new DataView(buf.buffer, buf.byteOffset, buf.byteLength);
  const out = new Array(n);
  for (let i = 0; i < n; i++) {
    const off = i * esz;
    const r = { key: view.getInt32(off, true), p: new Array(words) };
    for (let w = 0; w < words; w++) r.p[w] = view.getUint32(off + 4 + 4 * w, true);
    out[i] = r;
  }
  return out;
}

function checkRecs(words) {
  return () => {
    for (let i = 1; i < n; i++) if (work[i - 1].key > work[i].key) return false;
    for (let i = 0; i < n; i++) {
      for (let w = 0; w < words; w++) if (work[i].p[w] !== recWord(work[i].key, w)) return false;
    }
    return true;
  };
}

function checkAsc() {
  return () => {
    for (let i = 1; i < n; i++) if (work[i - 1] > work[i]) return false;
    return true;
  };
}

if (type === 'i32') {
  // noop / noop-array: the same loading without a sort, baseline for the memory phase.
  const noop = impl.startsWith('noop');
  const typed = impl === 'Int32Array.sort' || impl === 'noop';
  src = typed ? aligned(Int32Array, n * 4) : Array.from(aligned(Int32Array, n * 4));
  work = typed ? new Int32Array(n) : new Array(n);
  const copy = typed
    ? () => { work.set(src); }
    : () => { for (let i = 0; i < n; i++) work[i] = src[i]; };
  const doSort = noop
    ? () => {}
    : typed
      ? () => work.sort()
      : () => work.sort((a, b) => a - b);
  repeat(doSort, copy, noop ? () => true : checkAsc());
} else if (type === 'i64') {
  if (impl === 'BigInt64Array.sort') {
    src = aligned(BigInt64Array, n * 8);
    work = new BigInt64Array(n);
    repeat(() => work.sort(), () => { work.set(src); }, checkAsc());
  } else if (impl === 'Array.sort') {
    const raw = aligned(BigInt64Array, n * 8);
    src = new Array(n);
    for (let i = 0; i < n; i++) src[i] = Number(raw[i]);
    work = new Array(n);
    repeat(() => work.sort((a, b) => a - b),
      () => { for (let i = 0; i < n; i++) work[i] = src[i]; }, checkAsc());
  } else throw new Error(`unknown impl ${impl} for i64`);
} else if (type === 'f64') {
  src = impl === 'Float64Array.sort' ? aligned(Float64Array, n * 8) : Array.from(aligned(Float64Array, n * 8));
  work = impl === 'Float64Array.sort' ? new Float64Array(n) : new Array(n);
  const copy = impl === 'Float64Array.sort'
    ? () => { work.set(src); }
    : () => { for (let i = 0; i < n; i++) work[i] = src[i]; };
  const doSort = impl === 'Float64Array.sort'
    ? () => work.sort()
    : () => work.sort((a, b) => a - b);
  repeat(doSort, copy, checkAsc());
} else if (type === 'str') {
  src = buf.toString('utf8').split('\n');
  if (src[src.length - 1] === '') src.pop();
  if (src.length !== n) throw new Error(`want ${n} strings, got ${src.length}`);
  work = new Array(n);
  repeat(() => work.sort(),
    () => { for (let i = 0; i < n; i++) work[i] = src[i]; },
    () => {
      for (let i = 1; i < n; i++) if (work[i - 1] > work[i]) return false;
      return true;
    });
} else if (type === 'rec16' || type === 'rec32') {
  const words = type === 'rec16' ? 3 : 7;
  src = loadRecs(type === 'rec16' ? 16 : 32, words);
  work = new Array(n);
  repeat(() => work.sort((a, b) => a.key - b.key),
    () => { for (let i = 0; i < n; i++) work[i] = src[i]; },
    checkRecs(words));
} else {
  throw new Error(`unknown type ${type}`);
}
