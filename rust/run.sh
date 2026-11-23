#!/usr/bin/env bash
# Writes JSONL to stdout. RUSTSORT can point to an existing binary for this host.
set -euo pipefail
cd "$(dirname "$0")/.."
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT
rustsort=${RUSTSORT:-"$build/rustsort"}
if [[ -z ${RUSTSORT:-} ]]; then
  rustc --edition 2021 -O rust/sort.rs -o "$rustsort"
fi
go build -o "$build/gen" ./cmd/gen
go build -o "$build/gosort" ./cmd/gosort
clang -O2 c/sort.c -o "$build/csort"
clang++ -O2 -std=c++17 -I third_party -I third_party/ips4o/include \
  -I/opt/homebrew/include -L/opt/homebrew/lib cpp/sort.cpp -ltbb -o "$build/cppsort"
n=1000000
"$build/gen" -out "$build/data" -types i32 -sizes "$n" >&2
for shape in random sorted reverse nearly few; do
  args=(i32 "$shape" "$build/data/i32-$shape-$n.bin" "$n" 7)
  "$rustsort" sort_unstable "${args[@]}"
  "$rustsort" sort "${args[@]}"
  "$build/csort" qsort "${args[@]}"
  "$build/cppsort" std::sort "${args[@]}"
  "$build/cppsort" std::stable_sort "${args[@]}"
  "$build/gosort" slices.Sort "${args[@]}"
done
