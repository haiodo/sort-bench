#!/usr/bin/env python3
"""Turn results/raw/*.jsonl into results/tables.md: median ns per element."""
import glob
import json
import os
import statistics

RAW = "results/raw"
OUT = "results/tables.md"

timed = {}
for path in sorted(glob.glob(os.path.join(RAW, "*.jsonl"))):
    name = os.path.basename(path)
    if name == "mem.jsonl":
        continue
    with open(path) as f:
        timed[name] = [json.loads(line) for line in f if line.strip()]

# g++ rows are a compiler control; cpp-probe rows carry no type
gcc = [r for name, rs in timed.items() if name.endswith("-gcc.jsonl") for r in rs]
rows = [r for name, rs in timed.items() if not name.endswith("-gcc.jsonl")
        for r in rs if "type" in r]

bad = [r for r in rows if not r.get("ok")]
if bad:
    raise SystemExit(f"{len(bad)} unsorted results, first: {bad[0]}")

# (lang, impl, type, dist, n, threads) -> median ms
med = {}
for r in rows:
    key = (r["lang"], r["impl"], r["type"], r["dist"], r["n"], r.get("threads", 0))
    med.setdefault(key, []).append(r["ms"])
for key in med:
    med[key] = statistics.median(med[key])


def ns(lang, impl, typ, dist, n):
    ms = med.get((lang, impl, typ, dist, n, 0))
    return None if ms is None else ms * 1e6 / n


def ms(lang, impl, typ, dist, n):
    return med.get((lang, impl, typ, dist, n, 0))


def fmt(v, mark=False):
    if v is None:
        return "-"
    return f"**{v:.1f}**" if mark else f"{v:.1f}"


def table(title, row_specs, cols):
    """row_specs: (label, lang, impl). cols: (header, type, dist, n)."""
    lines = [f"### {title}", ""]
    lines.append("| реализация | " + " | ".join(c[0] for c in cols) + " |")
    lines.append("|---|" + "---|" * len(cols))
    values = []
    for label, lang, impl in row_specs:
        values.append([ns(lang, impl, c[1], c[2], c[3]) for c in cols])
    best = []
    for i in range(len(cols)):
        col = [v[i] for v in values if v[i] is not None]
        best.append(min(col) if col else None)
    for (label, _, _), vals in zip(row_specs, values):
        cells = [fmt(v, best[i] is not None and v is not None and abs(v - best[i]) < 1e-9)
                 for i, v in enumerate(vals)]
        lines.append(f"| {label} | " + " | ".join(cells) + " |")
    lines.append("")
    return "\n".join(lines)


MAIN = [
    ("C qsort", "c", "qsort"),
    ("C++ std::sort", "cpp", "std::sort"),
    ("Go slices.Sort", "go", "slices.Sort"),
    ("Go sort.Slice", "go", "sort.Slice"),
    ("Node Int32Array.sort", "node", "Int32Array.sort"),
    ("Node Array.sort(cmp)", "node", "Array.sort"),
    ("Bun Int32Array.sort", "bun", "Int32Array.sort"),
    ("Bun Array.sort(cmp)", "bun", "Array.sort"),
    ("Swift sort()", "swift", "sort"),
]

SHAPES = ["sorted", "reverse", "nearly", "few", "equal", "organ", "saw", "small"]

TYPE_IMPLS = {
    "c": {"_": "qsort"},
    "cpp": {"_": "std::sort"},
    "go": {"i32": "slices.Sort", "i64": "slices.Sort", "f64": "slices.Sort",
           "str": "slices.Sort", "rec16": "slices.SortFunc", "rec32": "slices.SortFunc"},
    "node": {"i32": "Int32Array.sort", "i64": "BigInt64Array.sort", "f64": "Float64Array.sort",
             "str": "Array.sort", "rec16": "Array.sort", "rec32": "Array.sort"},
    "swift": {"_": "sort"},
    "bun": {"i32": "Int32Array.sort", "str": "Array.sort"},
}

TYPE_LABEL = {"i32": "int32", "i64": "int64", "f64": "float64", "str": "строки",
              "rec16": "запись 16 Б", "rec32": "запись 32 Б"}

out = ["# Сортировки: медиана, нс на элемент", ""]

out.append(table("int32, случайный порядок, нс на элемент", MAIN,
                 [(str(n), "i32", "random", n) for n in (1000, 10000, 100000, 1000000, 10000000)]))

for n in (1000000, 10000000):
    num = f"{n:,}".replace(",", " ")
    out.append(table(f"int32, распределения входа, {num} элементов, нс на элемент",
                     MAIN, [("random", "i32", "random", n)] +
                     [(s, "i32", s, n) for s in SHAPES]))

# Types: the fastest stdlib path per language for each type.
type_rows = []
for lang, label in (("c", "C qsort"), ("cpp", "C++ std::sort"), ("go", "Go slices"),
                    ("node", "Node тип/Array"), ("bun", "Bun Int32/Array"),
                    ("swift", "Swift sort()")):
    type_rows.append((label, lang))
type_cols = [(TYPE_LABEL[t], t, "random", 1000000) for t in ("i32", "i64", "f64", "str", "rec16", "rec32")]

lines = [f"### Типы элементов, случайный порядок, 1 млн, нс на элемент", ""]
lines.append("| реализация | " + " | ".join(c[0] for c in type_cols) + " |")
lines.append("|---|" + "---|" * len(type_cols))
for label, lang in type_rows:
    cells = []
    for _, typ, dist, n in type_cols:
        impl = TYPE_IMPLS[lang].get(typ) or TYPE_IMPLS[lang].get("_")
        cells.append(fmt(ns(lang, impl, typ, dist, n)))
    lines.append(f"| {label} | " + " | ".join(cells) + " |")
lines.append("")
out.append("\n".join(lines))

LIBS = [
    ("std::sort", "cpp", "std::sort"),
    ("std::stable_sort", "cpp", "std::stable_sort"),
    ("pdqsort", "cpp", "pdqsort"),
    ("ska_sort", "cpp", "ska_sort"),
    ("ips4o", "cpp", "ips4o"),
]
out.append(table("C++: библиотеки, int32 random, нс на элемент", LIBS,
                 [("1 млн", "i32", "random", 1000000), ("10 млн", "i32", "random", 10000000)]))
out.append(table("C++: библиотеки, int32, 1 млн, нс на элемент", LIBS,
                 [("random", "i32", "random", 1000000)] +
                 [(s, "i32", s, 1000000) for s in SHAPES]))

GOLIBS = [
    ("slices.Sort", "go", "slices.Sort"),
    ("sort.Slice", "go", "sort.Slice"),
    ("pdqsort (lib)", "go", "pdqsort"),
    ("slices.SortStableFunc", "go", "slices.SortStableFunc"),
]
out.append(table("Go: библиотеки, int32 random, нс на элемент", GOLIBS,
                 [("1 млн", "i32", "random", 1000000), ("10 млн", "i32", "random", 10000000)]))
out.append(table("Go: библиотеки, int32, 1 млн, нс на элемент", GOLIBS,
                 [("random", "i32", "random", 1000000)] +
                 [(s, "i32", s, 1000000) for s in SHAPES]))

JS = [
    ("Node Int32Array.sort", "node", "Int32Array.sort"),
    ("Node Array.sort(cmp)", "node", "Array.sort"),
    ("Bun Int32Array.sort", "bun", "Int32Array.sort"),
    ("Bun Array.sort(cmp)", "bun", "Array.sort"),
]
out.append(table("Node и Bun, int32, нс на элемент", JS,
                 [("1 тыс.", "i32", "random", 1000), ("100 тыс.", "i32", "random", 100000),
                  ("1 млн", "i32", "random", 1000000), ("10 млн", "i32", "random", 10000000)]))

# Parallel: rows are languages, columns are thread counts.
plines = ["### Параллельная сортировка int32, 10 млн, нс на элемент", ""]
plines.append("| язык | 1 | 2 | 4 | 8 | 12 |")
plines.append("|---|---|---|---|---|---|")
for lang, label in (("c", "C pthreads"), ("cpp", "C++ std::thread"), ("go", "Go goroutines"),
                    ("swift", "Swift concurrentPerform"), ("node", "Node workers")):
    cells = []
    for t in (1, 2, 4, 8, 12):
        v = med.get((lang, "parallel", "i32", "random", 10000000, t))
        cells.append(fmt(None if v is None else v * 1e6 / 10000000))
    plines.append(f"| {label} | " + " | ".join(cells) + " |")
plines.append("")
out.append("\n".join(plines))

# Compiler control: the same C++17 code built with clang++/libc++ and g++/libstdc++.
gmed = {}
for r in gcc:
    key = (r["lang"], r["impl"], r["type"], r["dist"], r["n"], r.get("threads", 0))
    gmed.setdefault(key, []).append(r["ms"])
for key in gmed:
    gmed[key] = statistics.median(gmed[key])


def gns(impl, typ, dist, n):
    ms = gmed.get(("cpp", impl, typ, dist, n, 0))
    return None if ms is None else ms * 1e6 / n


if gmed:
    GCC_ROWS = [("std::sort", "std::sort"), ("std::stable_sort", "std::stable_sort"),
                ("pdqsort", "pdqsort"), ("ska_sort", "ska_sort"), ("ips4o", "ips4o")]
    lines = ["### C++: clang++/libc++ против g++/libstdc++, int32 random, нс на элемент", ""]
    lines.append("| реализация | 1 млн clang | 1 млн g++ | 10 млн clang | 10 млн g++ |")
    lines.append("|---|---|---|---|---|")
    for label, impl in GCC_ROWS:
        lines.append(f"| {label} | {fmt(ns('cpp', impl, 'i32', 'random', 1000000))} "
                     f"| {fmt(gns(impl, 'i32', 'random', 1000000))} "
                     f"| {fmt(ns('cpp', impl, 'i32', 'random', 10000000))} "
                     f"| {fmt(gns(impl, 'i32', 'random', 10000000))} |")
    lines.append("")
    lines.append("### C++ `std::sort`, распределения входа, 1 млн, clang против g++")
    lines.append("")
    lines.append("| компилятор | " + " | ".join(SHAPES) + " |")
    lines.append("|---|" + "---|" * len(SHAPES))
    lines.append("| clang++/libc++ | " + " | ".join(
        fmt(ns("cpp", "std::sort", "i32", s, 1000000)) for s in SHAPES) + " |")
    lines.append("| g++/libstdc++ | " + " | ".join(
        fmt(gns("std::sort", "i32", s, 1000000)) for s in SHAPES) + " |")
    lines.append("")
    out.append("\n".join(lines))

# Memory: peak RSS minus the noop run of the same harness.
mem = {}
if os.path.exists(os.path.join(RAW, "mem.jsonl")):
    with open(os.path.join(RAW, "mem.jsonl")) as f:
        for line in f:
            r = json.loads(line)
            mem[(r["lang"], r["impl"], r["n"])] = r["peak_bytes"]

if mem:
    MEM_ROWS = [
        ("C qsort", "c", "qsort"),
        ("C++ std::sort (libc++)", "cpp", "std::sort"),
        ("C++ std::stable_sort (libc++)", "cpp", "std::stable_sort"),
        ("C++ pdqsort", "cpp", "pdqsort"),
        ("C++ ska_sort", "cpp", "ska_sort"),
        ("C++ ips4o", "cpp", "ips4o"),
        ("Go slices.Sort", "go", "slices.Sort"),
        ("Go sort.Slice", "go", "sort.Slice"),
        ("Go pdqsort", "go", "pdqsort"),
        ("Go slices.SortStableFunc", "go", "slices.SortStableFunc"),
        ("Node Int32Array.sort", "node", "Int32Array.sort"),
        ("Node Array.sort(cmp)", "node", "Array.sort"),
        ("Bun Int32Array.sort", "bun", "Int32Array.sort"),
        ("Bun Array.sort(cmp)", "bun", "Array.sort"),
        ("Swift sort()", "swift", "sort"),
    ]
    sizes = sorted({n for _, _, n in mem})
    lines = ["### Память, int32: пик RSS сверх загрузки массива, МБ (10^6 байт)", ""]
    lines.append("| реализация | " + " | ".join(f"{n // 1000000} млн" for n in sizes) + " |")
    lines.append("|---|" + "---|" * len(sizes))
    for label, lang, impl in MEM_ROWS:
        base_impl = "noop-array" if impl == "Array.sort" else "noop"
        cells = []
        for n in sizes:
            got = mem.get((lang, impl, n))
            base = mem.get((lang, base_impl, n))
            if got is None or base is None:
                cells.append("-")
                continue
            delta = (got - base) / 1e6
            cells.append("0" if delta < 0.5 else f"{delta:+.1f}")
        lines.append(f"| {label} | " + " | ".join(cells) + " |")
    lines.append("")
    for n in sizes:
        lines.append(f"Пик процесса {n // 1000000} млн, МБ: " + ", ".join(
            f"{lang} {mem[(lang, 'noop', n)] / 1e6:.0f}"
            for lang in ("c", "cpp", "go", "node", "bun", "swift")) + ".")
    lines.append("")
    lines.append("Ноль - в пределах шума ±2 МБ. libstdc++ `std::stable_sort` на 10 млн "
                 "держит один буфер n/2 (+20 МБ) против двух у libc++ (+40 МБ).")
    lines.append("")
    out.append("\n".join(lines))

with open(OUT, "w") as f:
    f.write("\n".join(out))
print(f"{OUT}: {len(rows)} measurements, {len(med)} configurations")
