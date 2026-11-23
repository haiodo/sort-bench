#!/usr/bin/env bash
# Parallel int32 sort: chunk sort + pairwise merge. 1, 2, 4, 8, 12 threads.
# Needs data/i32-random-<N>.bin from ./run.sh.
set -euo pipefail
cd "$(dirname "$0")/.."

export PATH="/opt/homebrew/bin:$PATH"
NODE="${NODE:-$(command -v node || echo "$HOME/.nvm/versions/node/v24.19.0/bin/node")}"
N="${N:-10000000}"
REPS="${REPS:-5}"
THREADS="${THREADS:-1 2 4 8 12}"
FILE="data/i32-random-$N.bin"

mkdir -p build results/raw
go build -o build/goparallel ./parallel/go
clang -O2 -o build/cparallel parallel/c/main.c
clang++ -O2 -std=c++17 -o build/cppparallel parallel/cpp/main.cpp
swiftc -O -swift-version 5 -o build/swiftparallel parallel/swift/main.swift

for lang in c cpp go swift node; do
  : >"results/raw/parallel-$lang.jsonl"
done

for T in $THREADS; do
  echo "threads=$T"
  ./build/cparallel "$FILE" "$N" "$T" "$REPS" >>results/raw/parallel-c.jsonl
  ./build/cppparallel "$FILE" "$N" "$T" "$REPS" >>results/raw/parallel-cpp.jsonl
  ./build/goparallel "$FILE" "$N" "$T" "$REPS" >>results/raw/parallel-go.jsonl
  ./build/swiftparallel "$FILE" "$N" "$T" "$REPS" >>results/raw/parallel-swift.jsonl
  "$NODE" parallel/node/main.mjs "$FILE" "$N" "$T" "$REPS" >>results/raw/parallel-node.jsonl
done

echo "done: $(cat results/raw/parallel-*.jsonl | wc -l | tr -d ' ') rows"
