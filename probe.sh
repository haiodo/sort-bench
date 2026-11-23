#!/usr/bin/env bash
# std::sort with a function-pointer comparator: isolates the cost of qsort's
# interface from the cost of sorting. Needs data/ from ./run.sh.
set -euo pipefail
cd "$(dirname "$0")"
export PATH="/opt/homebrew/bin:$PATH"
mkdir -p build results/raw
clang++ -O2 -std=c++17 -I third_party -I third_party/ips4o/include -I/opt/homebrew/include -L/opt/homebrew/lib -o build/cppsort cpp/sort.cpp -ltbb
: >results/raw/cpp-probe.jsonl
for n in 1000000 10000000; do
  ./build/cppsort std::sort/ptr i32 random "data/i32-random-$n.bin" "$n" 7 >>results/raw/cpp-probe.jsonl
done
echo "results/raw/cpp-probe.jsonl"
