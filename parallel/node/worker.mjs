import { parentPort, workerData } from 'node:worker_threads';

function mergeInto(dst, src, lo, mid, hi) {
  let i = lo, j = mid, k = lo;
  while (i < mid && j < hi) {
    if (src[i] <= src[j]) dst[k++] = src[i++];
    else dst[k++] = src[j++];
  }
  while (i < mid) dst[k++] = src[i++];
  while (j < hi) dst[k++] = src[j++];
}

parentPort.on('message', (m) => {
  if (m.cmd === 'sort') {
    new Int32Array(workerData.workSab, m.lo * 4, m.hi - m.lo).sort();
  } else if (m.cmd === 'merge') {
    const src = new Int32Array(m.srcSab, 0, m.n);
    const dst = new Int32Array(m.dstSab, 0, m.n);
    if (m.odd) {
      dst.set(src.subarray(m.lo, m.hi), m.lo);
    } else {
      mergeInto(dst, src, m.lo, m.mid, m.hi);
    }
  }
  parentPort.postMessage(m.id);
});
