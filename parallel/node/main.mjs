// Parallel int32 sort in Node: T workers sort chunks of a SharedArrayBuffer,
// then merge adjacent runs pairwise. Usage: node main.mjs FILE N THREADS REPS
import fs from 'node:fs';
import { Worker } from 'node:worker_threads';

const [file, nStr, threadsStr, repsStr] = process.argv.slice(2);
const n = Number(nStr);
const threads = Number(threadsStr);
const reps = Number(repsStr);

const buf = fs.readFileSync(file);
const src = new Int32Array(buf.buffer.slice(buf.byteOffset, buf.byteOffset + n * 4));

const workSab = new SharedArrayBuffer(n * 4);
const bufSab = new SharedArrayBuffer(n * 4);
const work = new Int32Array(workSab);
const scratch = new Int32Array(bufSab);

let workers = [];
if (threads > 1) {
  workers = Array.from({ length: threads }, () => new Worker(new URL('./worker.mjs', import.meta.url), {
    workerData: { workSab, bufSab, n },
  }));
}

let seq = 0;
function ask(worker, msg) {
  msg.id = ++seq;
  return new Promise((resolve) => {
    worker.once('message', resolve);
    worker.postMessage(msg);
  });
}

function runsFor(parts) {
  const chunk = Math.ceil(n / parts);
  const runs = [];
  for (let s = 0; s < n; s += chunk) runs.push([s, Math.min(n, s + chunk)]);
  return runs;
}

async function parallelSort() {
  if (threads === 1) {
    work.sort();
    return;
  }
  let runs = runsFor(threads);
  await Promise.all(runs.map(([lo, hi], i) => ask(workers[i], { cmd: 'sort', lo, hi })));

  let curSab = workSab, otherSab = bufSab;
  while (runs.length > 1) {
    const next = [];
    const tasks = [];
    for (let i = 0; i < runs.length; i += 2) {
      if (i + 1 < runs.length) {
        next.push([runs[i][0], runs[i + 1][1]]);
        tasks.push({ cmd: 'merge', srcSab: curSab, dstSab: otherSab, n,
          lo: runs[i][0], mid: runs[i][1], hi: runs[i + 1][1], odd: false });
      } else {
        next.push(runs[i]);
        tasks.push({ cmd: 'merge', srcSab: curSab, dstSab: otherSab, n,
          lo: runs[i][0], mid: runs[i][1], hi: runs[i][1], odd: true });
      }
    }
    await Promise.all(tasks.map((t, i) => ask(workers[i % workers.length], t)));
    [curSab, otherSab] = [otherSab, curSab];
    runs = next;
  }
  if (curSab !== workSab) work.set(new Int32Array(curSab));
}

function sorted() {
  for (let i = 1; i < n; i++) if (work[i - 1] > work[i]) return false;
  return true;
}

const warm = performance.now();
do {
  work.set(src);
  await parallelSort();
} while (performance.now() - warm < 300);

for (let rep = 1; rep <= reps; rep++) {
  work.set(src);
  const t0 = performance.now();
  await parallelSort();
  const ms = performance.now() - t0;
  console.log(JSON.stringify({
    lang: 'node', impl: 'parallel', type: 'i32', dist: 'random', n, threads, rep,
    ms: Number(ms.toFixed(3)), ns_el: Number((ms * 1e6 / n).toFixed(3)), ok: sorted(),
  }));
}

if (workers.length) await Promise.all(workers.map((w) => w.terminate()));
