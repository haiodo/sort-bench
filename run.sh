#!/usr/bin/env bash
# Phases:
#   A - stdlib: int32 random sizes, int32 shapes, int64/float64/string/records
#   B - C++ libraries: std::stable_sort, pdqsort, ska_sort, ips4o
#   C - Go libraries: pdqsort, stable sorts
#   P - parallel sorts (parallel/)
#   N - Bun: Array and Int32Array
#   M - memory: peak RSS against a noop run, see mem.sh
#   ./run.sh A B C N   selected phases; default A.
set -euo pipefail
cd "$(dirname "$0")"

export PATH="/opt/homebrew/bin:$PATH"
CXX="${CXX:-clang++}"
SUFFIX="${SUFFIX:-}"
NODE="${NODE:-$(command -v node || echo "$HOME/.nvm/versions/node/v24.19.0/bin/node")}"
BUN="${BUN:-$(command -v bun || echo /opt/homebrew/bin/bun)}"
PHASES="${*:-A}"
LANGS="${LANGS:-c cpp go node swift}"
REPS="${REPS:-7}"
SIZES="${SIZES:-1000 10000 100000 1000000 10000000}"
PATTERN_SIZES="${PATTERN_SIZES:-1000000 10000000}"
TYPE_SIZES="${TYPE_SIZES:-1000000 10000000}"
SMALL_TYPE_SIZES="${SMALL_TYPE_SIZES:-1000000}"
SHAPES="${SHAPES:-sorted reverse nearly few equal organ saw small}"

mkdir -p build results/raw data
{
  sw_vers
  sysctl -n machdep.cpu.brand_string hw.ncpu hw.memsize
  go version
  "$NODE" --version
  "$BUN" --version
  swiftc --version | head -2
  clang --version | head -1
  "$CXX" --version | head -1
  pmset -g | grep powermode || true
} >results/env.txt

gen() {
  go build -o build/gen ./cmd/gen
  go build -o build/gosort ./cmd/gosort
  clang -O2 -o build/csort c/sort.c
  "$CXX" -O2 -std=c++17 -I third_party -I third_party/ips4o/include -I/opt/homebrew/include -L/opt/homebrew/lib -o build/cppsort cpp/sort.cpp -ltbb
  swiftc -O -o build/swiftsort swift/sort.swift
}
gen
./build/gen -out data -types i32 -sizes "$(echo "$SIZES" | tr ' ' ',')"
./build/gen -out data -types i64,f64 -sizes "$(echo "$TYPE_SIZES" | tr ' ' ',')"
./build/gen -out data -types str,rec16,rec32 -sizes "$(echo "$SMALL_TYPE_SIZES" | tr ' ' ',')"

run() { # lang impl type shape n
  local lang=$1 impl=$2 typ=$3 shape=$4 n=$5
  local file="data/$typ-$shape-$n.bin"
  [[ $typ == str ]] && file="data/str-$shape-$n.txt"
  local out="${OUT:-results/raw/$lang$SUFFIX.jsonl}"
  case $lang in
    c)     ./build/csort "$impl" "$typ" "$shape" "$file" "$n" "$REPS" >>"$out" ;;
    cpp)   ./build/cppsort "$impl" "$typ" "$shape" "$file" "$n" "$REPS" >>"$out" ;;
    go)    ./build/gosort "$impl" "$typ" "$shape" "$file" "$n" "$REPS" >>"$out" ;;
    swift) ./build/swiftsort "$impl" "$typ" "$shape" "$file" "$n" "$REPS" >>"$out" ;;
    node)  "$NODE" --expose-gc --max-old-space-size=8192 node/sort.mjs "$impl" "$typ" "$shape" "$file" "$n" "$REPS" >>"$out" ;;
    bun)   "$BUN" node/sort.mjs "$impl" "$typ" "$shape" "$file" "$n" "$REPS" >>"$out" ;;
  esac
}

# Stdlib implementations per language and type.
impls() { # lang type
  case $1 in
    c) echo "qsort" ;;
    cpp) [[ $2 == rec16 || $2 == rec32 ]] && echo "std::sort" || echo "std::sort" ;;
    go)
      case $2 in
        rec16|rec32) echo "slices.SortFunc sort.Slice" ;;
        *) echo "slices.Sort sort.Slice" ;;
      esac ;;
    node)
      case $2 in
        i32) echo "Int32Array.sort Array.sort" ;;
        i64) echo "BigInt64Array.sort Array.sort" ;;
        f64) echo "Float64Array.sort Array.sort" ;;
        *) echo "Array.sort" ;;
      esac ;;
    swift) echo "sort" ;;
  esac
}

stdlib_phase() {
  for lang in $LANGS; do
    : >"results/raw/$lang$SUFFIX.jsonl"
    for n in $SIZES; do
      for impl in $(impls "$lang" i32); do run "$lang" "$impl" i32 random "$n"; done
    done
    for shape in $SHAPES; do
      for n in $PATTERN_SIZES; do
        for impl in $(impls "$lang" i32); do run "$lang" "$impl" i32 "$shape" "$n"; done
      done
    done
    for typ in i64 f64; do
      for n in $TYPE_SIZES; do
        for impl in $(impls "$lang" "$typ"); do run "$lang" "$impl" "$typ" random "$n"; done
      done
    done
    for typ in str rec16 rec32; do
      for n in $SMALL_TYPE_SIZES; do
        for impl in $(impls "$lang" "$typ"); do run "$lang" "$impl" "$typ" random "$n"; done
      done
    done
  done
}

cpp_libs_phase() {
  : >"results/raw/cpp-libs$SUFFIX.jsonl"
  OUT="results/raw/cpp-libs$SUFFIX.jsonl"
  for typ in i32 i64 f64; do
    for n in $TYPE_SIZES; do
      for impl in std::stable_sort pdqsort ska_sort ips4o; do
        run cpp "$impl" "$typ" random "$n"
      done
    done
  done
  for shape in sorted reverse nearly few equal organ saw small; do
    for n in $PATTERN_SIZES; do
      for impl in std::stable_sort pdqsort ska_sort ips4o; do
        run cpp "$impl" i32 "$shape" "$n"
      done
    done
  done
  unset OUT
}

go_libs_phase() {
  : >"results/raw/go-libs$SUFFIX.jsonl"
  OUT="results/raw/go-libs$SUFFIX.jsonl"
  for typ in i32 i64 f64; do
    for n in $TYPE_SIZES; do
      for impl in pdqsort slices.SortStableFunc; do
        run go "$impl" "$typ" random "$n"
      done
    done
  done
  for shape in sorted reverse nearly few equal organ saw small; do
    for n in $PATTERN_SIZES; do
      for impl in pdqsort slices.SortStableFunc; do
        run go "$impl" i32 "$shape" "$n"
      done
    done
  done
  unset OUT
}

bun_phase() {
  : >"results/raw/bun$SUFFIX.jsonl"
  OUT="results/raw/bun$SUFFIX.jsonl"
  for n in $SIZES; do
    for impl in Int32Array.sort Array.sort; do run bun "$impl" i32 random "$n"; done
  done
  for shape in sorted reverse nearly few equal organ saw small; do
    for n in $PATTERN_SIZES; do
      for impl in Int32Array.sort Array.sort; do run bun "$impl" i32 "$shape" "$n"; done
    done
  done
  for n in $SMALL_TYPE_SIZES; do
    for impl in Array.sort; do run bun "$impl" str random "$n"; done
  done
  unset OUT
}

for phase in $PHASES; do
  case $phase in
    A) stdlib_phase ;;
    B) cpp_libs_phase ;;
    C) go_libs_phase ;;
    N) bun_phase ;;
    P) echo "parallel phase: see parallel/run.sh" ;;
    *) echo "unknown phase $phase" >&2; exit 2 ;;
  esac
done

echo "done: $(cat results/raw/*.jsonl | wc -l | tr -d ' ') rows"
