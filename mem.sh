#!/usr/bin/env bash
# Peak RSS per implementation, each in its own process. A noop run of the same
# harness loads and copies the array but sorts nothing; peak(impl) - peak(noop)
# is what the sort itself needs. int32 only: 1M and 10M.
set -euo pipefail
cd "$(dirname "$0")"

export PATH="/opt/homebrew/bin:$PATH"
NODE="${NODE:-$(command -v node || echo "$HOME/.nvm/versions/node/v24.19.0/bin/node")}"
BUN="${BUN:-$(command -v bun || echo /opt/homebrew/bin/bun)}"
SIZES="${SIZES:-1000000 10000000}"
REPS="${REPS:-1}"

mkdir -p build results/raw data
go build -o build/gen ./cmd/gen
go build -o build/gosort ./cmd/gosort
clang -O2 -o build/csort c/sort.c
clang++ -O2 -std=c++17 -I third_party -I third_party/ips4o/include \
  -I/opt/homebrew/include -L/opt/homebrew/lib -o build/cppsort cpp/sort.cpp -ltbb
swiftc -O -o build/swiftsort swift/sort.swift

for n in $SIZES; do
  [[ -f data/i32-random-$n.bin ]] || ./build/gen -out data -types i32 -sizes "$n"
done

peak() { # peak RSS of one process, bytes
  /usr/bin/time -l "$@" 2>&1 >/dev/null | awk '/maximum resident set size/{print $1}'
}

measure() { # lang impl n
  local lang=$1 impl=$2 n=$3 file="data/i32-random-$n.bin" bytes
  case $lang in
    c) bytes=$(peak ./build/csort "$impl" i32 random "$file" "$n" "$REPS") ;;
    cpp) bytes=$(peak ./build/cppsort "$impl" i32 random "$file" "$n" "$REPS") ;;
    go) bytes=$(peak ./build/gosort "$impl" i32 random "$file" "$n" "$REPS") ;;
    node) bytes=$(peak "$NODE" --expose-gc node/sort.mjs "$impl" i32 random "$file" "$n" "$REPS") ;;
    bun) bytes=$(peak "$BUN" node/sort.mjs "$impl" i32 random "$file" "$n" "$REPS") ;;
    swift) bytes=$(peak ./build/swiftsort "$impl" i32 random "$file" "$n" "$REPS") ;;
  esac
  [[ -n "$bytes" ]] || { echo "no RSS: $lang $impl n=$n" >&2; exit 1; }
  printf '{"lang":"%s","impl":"%s","type":"i32","dist":"random","n":%d,"peak_bytes":%s}\n' \
    "$lang" "$impl" "$n" "$bytes"
}

: >results/raw/mem.jsonl
for n in $SIZES; do
  measure c noop "$n" >>results/raw/mem.jsonl
  measure c qsort "$n" >>results/raw/mem.jsonl
  measure cpp noop "$n" >>results/raw/mem.jsonl
  for impl in std::sort std::stable_sort pdqsort ska_sort ips4o; do
    measure cpp "$impl" "$n" >>results/raw/mem.jsonl
  done
  measure go noop "$n" >>results/raw/mem.jsonl
  for impl in slices.Sort sort.Slice pdqsort slices.SortStableFunc; do
    measure go "$impl" "$n" >>results/raw/mem.jsonl
  done
  measure node noop "$n" >>results/raw/mem.jsonl
  measure node noop-array "$n" >>results/raw/mem.jsonl
  for impl in Int32Array.sort Array.sort; do
    measure node "$impl" "$n" >>results/raw/mem.jsonl
  done
  measure bun noop "$n" >>results/raw/mem.jsonl
  measure bun noop-array "$n" >>results/raw/mem.jsonl
  for impl in Int32Array.sort Array.sort; do
    measure bun "$impl" "$n" >>results/raw/mem.jsonl
  done
  measure swift noop "$n" >>results/raw/mem.jsonl
  measure swift sort "$n" >>results/raw/mem.jsonl
done

echo "done: $(wc -l <results/raw/mem.jsonl | tr -d ' ') rows"
