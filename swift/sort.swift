// Swift: Array.sort() (stable Timsort) on the shared int32/int64/float64/string
// and record inputs. Usage: swiftsort IMPL TYPE SHAPE FILE N REPS
import Dispatch
import Foundation

let warmupNs: UInt64 = 300_000_000
let targetMs = 200.0
let maxSorts = 20000

struct Rec16: Comparable {
    let key: Int32
    let p0, p1, p2: UInt32
    static func < (a: Rec16, b: Rec16) -> Bool { a.key < b.key }
}

struct Rec32: Comparable {
    let key: Int32
    let p0, p1, p2, p3, p4, p5, p6: UInt32
    static func < (a: Rec32, b: Rec32) -> Bool { a.key < b.key }
}

let args = CommandLine.arguments
guard args.count == 7 else {
    FileHandle.standardError.write("usage: swiftsort IMPL TYPE SHAPE FILE N REPS\n".data(using: .utf8)!)
    exit(2)
}
let impl = args[1], type = args[2], shape = args[3], file = args[4]
let n = Int(args[5])!, reps = Int(args[6])!

func now() -> UInt64 { DispatchTime.now().uptimeNanoseconds }

func batchCount(_ pilotMs: Double) -> Int {
    if !(pilotMs > 0) { return 1 }
    return min(maxSorts, max(1, Int((targetMs / pilotMs).rounded(.up))))
}

func emit(_ rep: Int, _ ms: Double, _ ok: Bool) {
    let ns = ms * 1e6 / Double(n)
    print("{\"lang\":\"swift\",\"impl\":\"\(impl)\",\"type\":\"\(type)\",\"dist\":\"\(shape)\",\"n\":\(n),"
        + "\"rep\":\(rep),\"ms\":\(String(format: "%.4f", ms)),"
        + "\"ns_el\":\(String(format: "%.3f", ns)),\"ok\":\(ok)}")
}

func recWord(_ key: Int32, _ w: Int) -> UInt32 {
    switch w {
    case 0: return UInt32(bitPattern: key) &* 2654435761
    case 1: return ~UInt32(bitPattern: key)
    case 2: return UInt32(bitPattern: key) &+ 0x9e3779b9
    default: return UInt32(bitPattern: key) ^ (0x85ebca6b &* UInt32(w))
    }
}

let data = try! Data(contentsOf: URL(fileURLWithPath: file))

func run<T: Comparable>(_ src: [T], _ check: ([T]) -> Bool, _ doSort: (inout [T]) -> Void = { $0.sort() }) {
    var work = [T]()
    func copyIn() {
        work = Array(src)
        work.withUnsafeMutableBufferPointer { $0[0] = $0[0] } // force uniqueness outside the timer
    }

    var pilot = 0.0
    let warm = now()
    repeat {
        copyIn()
        let t0 = now()
        doSort(&work)
        pilot = Double(now() - t0) / 1e6
    } while now() - warm < warmupNs

    let k = batchCount(pilot)
    for rep in 1...reps {
        copyIn()
        var total = 0.0
        for j in 0..<k {
            let t0 = now()
            doSort(&work)
            total += Double(now() - t0) / 1e6
            if j + 1 < k { copyIn() }
        }
        emit(rep, total / Double(k), check(work))
    }
}

func isAscending<T: Comparable>(_ a: [T]) -> Bool {
    for i in 1..<a.count where a[i - 1] > a[i] { return false }
    return true
}

if type == "i32" {
    var src = [Int32](repeating: 0, count: n)
    data.withUnsafeBytes { (raw: UnsafeRawBufferPointer) in
        for i in 0..<n { src[i] = raw.loadUnaligned(fromByteOffset: i * 4, as: Int32.self) }
    }
    run(src, { impl == "noop" || isAscending($0) }, { if impl != "noop" { $0.sort() } })
} else if type == "i64" {
    var src = [Int64](repeating: 0, count: n)
    data.withUnsafeBytes { (raw: UnsafeRawBufferPointer) in
        for i in 0..<n { src[i] = raw.loadUnaligned(fromByteOffset: i * 8, as: Int64.self) }
    }
    run(src) { isAscending($0) }
} else if type == "f64" {
    var src = [Double](repeating: 0, count: n)
    data.withUnsafeBytes { (raw: UnsafeRawBufferPointer) in
        for i in 0..<n { src[i] = raw.loadUnaligned(fromByteOffset: i * 8, as: Double.self) }
    }
    run(src) { isAscending($0) }
} else if type == "str" {
    let src = String(decoding: data, as: UTF8.self).split(separator: "\n").map(String.init)
    guard src.count == n else {
        FileHandle.standardError.write("want \(n) strings, got \(src.count)\n".data(using: .utf8)!)
        exit(1)
    }
    run(src) { isAscending($0) }
} else if type == "rec16" || type == "rec32" {
    let wide = type == "rec32"
    let esz = wide ? 32 : 16
    let words = wide ? 7 : 3
    var recs: [Rec32] = []
    var recs16: [Rec16] = []
    data.withUnsafeBytes { (raw: UnsafeRawBufferPointer) in
        for i in 0..<n {
            let off = i * esz
            let key = raw.loadUnaligned(fromByteOffset: off, as: Int32.self)
            var w = [UInt32](repeating: 0, count: words)
            for j in 0..<words {
                w[j] = raw.loadUnaligned(fromByteOffset: off + 4 + 4 * j, as: UInt32.self)
            }
            if wide {
                recs.append(Rec32(key: key, p0: w[0], p1: w[1], p2: w[2], p3: w[3], p4: w[4], p5: w[5], p6: w[6]))
            } else {
                recs16.append(Rec16(key: key, p0: w[0], p1: w[1], p2: w[2]))
            }
        }
    }

    func check16(_ a: [Rec16]) -> Bool {
        for i in 1..<a.count where a[i - 1].key > a[i].key { return false }
        for r in a {
            let p = [r.p0, r.p1, r.p2]
            for w in 0..<3 where p[w] != recWord(r.key, w) { return false }
        }
        return true
    }

    if wide {
        var work: [Rec32] = []
        func copyIn() {
            work = Array(recs)
            work.withUnsafeMutableBufferPointer { $0[0] = $0[0] }
        }
        var pilot = 0.0
        let warm = now()
        repeat {
            copyIn()
            let t0 = now(); work.sort(); pilot = Double(now() - t0) / 1e6
        } while now() - warm < warmupNs
        let k = batchCount(pilot)
        for rep in 1...reps {
            copyIn()
            var total = 0.0
            for j in 0..<k {
                let t0 = now(); work.sort()
                total += Double(now() - t0) / 1e6
                if j + 1 < k { copyIn() }
            }
            var ok = true
            for i in 1..<n where work[i - 1].key > work[i].key { ok = false; break }
            if ok {
                outer: for r in work {
                    let p = [r.p0, r.p1, r.p2, r.p3, r.p4, r.p5, r.p6]
                    for w in 0..<7 where p[w] != recWord(r.key, w) { ok = false; break outer }
                }
            }
            emit(rep, total / Double(k), ok)
        }
    } else {
        run(recs16) { check16($0) }
    }
} else {
    FileHandle.standardError.write("unknown type \(type)\n".data(using: .utf8)!)
    exit(2)
}
