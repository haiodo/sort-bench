// Parallel int32 sort: sort T chunks with DispatchQueue.concurrentPerform, then
// merge runs pairwise in parallel. Usage: swiftparallel FILE N THREADS REPS
import Dispatch
import Foundation

func mergeInto(_ dst: UnsafeMutablePointer<Int32>, _ src: UnsafeMutablePointer<Int32>,
               _ lo: Int, _ mid: Int, _ hi: Int) {
    var i = lo, j = mid, k = lo
    while i < mid && j < hi {
        if src[i] <= src[j] { dst[k] = src[i]; i += 1 } else { dst[k] = src[j]; j += 1 }
        k += 1
    }
    while i < mid { dst[k] = src[i]; i += 1; k += 1 }
    while j < hi { dst[k] = src[j]; j += 1; k += 1 }
}

func parallelSort(_ work: UnsafeMutablePointer<Int32>, _ buf: UnsafeMutablePointer<Int32>,
                  _ n: Int, _ threads: Int) {
    var runs: [(Int, Int)] = []
    let chunk = (n + threads - 1) / threads
    var s = 0
    while s < n { runs.append((s, min(n, s + chunk))); s += chunk }

    let chunks = runs
    DispatchQueue.concurrentPerform(iterations: chunks.count) { idx in
        let (lo, hi) = chunks[idx]
        var bp = UnsafeMutableBufferPointer(start: work + lo, count: hi - lo)
        bp.sort()
    }

    var cur = work, other = buf
    var level = runs
    while level.count > 1 {
        var next: [(Int, Int)] = []
        for i in stride(from: 0, to: level.count, by: 2) {
            next.append((level[i].0, i + 1 < level.count ? level[i + 1].1 : level[i].1))
        }
        let pairs = level
        DispatchQueue.concurrentPerform(iterations: (pairs.count + 1) / 2) { k in
            let i = k * 2
            if i + 1 < pairs.count {
                mergeInto(other, cur, pairs[i].0, pairs[i].1, pairs[i + 1].1)
            } else {
                memcpy(other + pairs[i].0, cur + pairs[i].0,
                       (pairs[i].1 - pairs[i].0) * MemoryLayout<Int32>.size)
            }
        }
        swap(&cur, &other)
        level = next
    }
    if cur != work {
        memcpy(work, cur, n * MemoryLayout<Int32>.size)
    }
}

let args = CommandLine.arguments
guard args.count == 5 else {
    FileHandle.standardError.write("usage: swiftparallel FILE N THREADS REPS\n".data(using: .utf8)!)
    exit(2)
}
let n = Int(args[2])!, threads = Int(args[3])!, reps = Int(args[4])!
let data = try! Data(contentsOf: URL(fileURLWithPath: args[1]))
var src = [Int32](repeating: 0, count: n)
data.withUnsafeBytes { (raw: UnsafeRawBufferPointer) in
    for i in 0..<n { src[i] = raw.loadUnaligned(fromByteOffset: i * 4, as: Int32.self) }
}

let work = UnsafeMutablePointer<Int32>.allocate(capacity: n)
let buf = UnsafeMutablePointer<Int32>.allocate(capacity: n)
defer { work.deallocate(); buf.deallocate() }

func reset() { _ = src.withUnsafeBufferPointer { memcpy(work, $0.baseAddress!, n * 4) } }
func sorted() -> Bool {
    for i in 1..<n where work[i - 1] > work[i] { return false }
    return true
}
func now() -> UInt64 { DispatchTime.now().uptimeNanoseconds }

let warm = now()
repeat {
    reset()
    parallelSort(work, buf, n, threads)
} while now() - warm < 300_000_000

for rep in 1...reps {
    reset()
    let t0 = now()
    parallelSort(work, buf, n, threads)
    let ms = Double(now() - t0) / 1e6
    print("{\"lang\":\"swift\",\"impl\":\"parallel\",\"type\":\"i32\",\"dist\":\"random\",\"n\":\(n),"
        + "\"threads\":\(threads),\"rep\":\(rep),\"ms\":\(String(format: "%.3f", ms)),"
        + "\"ns_el\":\(String(format: "%.3f", ms * 1e6 / Double(n))),\"ok\":\(sorted())}")
}
