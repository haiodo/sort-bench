// C++: std::sort, std::stable_sort, pdqsort (orlp), ska_sort and ips4o on the
// shared inputs. Usage: cppsort IMPL TYPE SHAPE FILE N REPS
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <type_traits>
#include <vector>

#include "ips4o/ips4o.hpp"
#include "pdqsort/pdqsort.h"
#include "ska_sort/ska_sort.hpp"

using Clock = std::chrono::steady_clock;
constexpr double kWarmupMs = 300.0;
constexpr double kTargetMs = 200.0;
constexpr long kMaxSorts = 20000;

struct Rec16 { int32_t key; uint32_t p[3]; };
struct Rec32 { int32_t key; uint32_t p[7]; };
static_assert(sizeof(Rec16) == 16, "rec16 size");
static_assert(sizeof(Rec32) == 32, "rec32 size");

struct Rec16Less { bool operator()(const Rec16 &a, const Rec16 &b) const { return a.key < b.key; } };
struct Rec32Less { bool operator()(const Rec32 &a, const Rec32 &b) const { return a.key < b.key; } };

static double now_ms() {
	return std::chrono::duration<double, std::milli>(Clock::now().time_since_epoch()).count();
}

template <typename T>
static std::vector<T> load(const char *path, size_t n) {
	if constexpr (std::is_same_v<T, std::string>) {
		std::ifstream f(path);
		if (!f) { std::perror(path); std::exit(1); }
		std::vector<T> v;
		v.reserve(n);
		std::string line;
		while (std::getline(f, line)) v.push_back(std::move(line));
		if (v.size() != n) {
			std::fprintf(stderr, "want %zu strings, got %zu\n", n, v.size());
			std::exit(1);
		}
		return v;
	} else {
		std::ifstream f(path, std::ios::binary);
		if (!f) { std::perror(path); std::exit(1); }
		std::vector<T> v(n);
		f.read(reinterpret_cast<char *>(v.data()), (std::streamsize)(n * sizeof(T)));
		if ((size_t)f.gcount() != n * sizeof(T)) {
			std::fprintf(stderr, "short read %s\n", path);
			std::exit(1);
		}
		return v;
	}
}

// Payload is derived from the key, so a sort that moves keys without payload
// fails the check.
static uint32_t rec_word(int32_t key, int w) {
	switch (w) {
	case 0: return (uint32_t)key * 2654435761u;
	case 1: return ~(uint32_t)key;
	case 2: return (uint32_t)key + 0x9e3779b9u;
	default: return (uint32_t)key ^ (0x85ebca6bu * (uint32_t)w);
	}
}

template <typename T>
static bool verify(const std::vector<T> &v) {
	for (size_t i = 1; i < v.size(); i++)
		if (v[i] < v[i - 1]) return false;
	return true;
}

template <typename T, int Words>
static bool verify_rec(const std::vector<T> &v) {
	for (size_t i = 0; i < v.size(); i++) {
		if (i > 0 && v[i - 1].key > v[i].key) return false;
		for (int w = 0; w < Words; w++)
			if (v[i].p[w] != rec_word(v[i].key, w)) return false;
	}
	return true;
}

static void emit(const char *impl, const char *type, const char *shape, size_t n, int rep, double ms, bool ok) {
	std::printf("{\"lang\":\"cpp\",\"impl\":\"%s\",\"type\":\"%s\",\"dist\":\"%s\",\"n\":%zu,\"rep\":%d,"
	            "\"ms\":%.4f,\"ns_el\":%.3f,\"ok\":%s}\n",
	            impl, type, shape, n, rep, ms, ms * 1e6 / (double)n, ok ? "true" : "false");
}

static long batch_count(double pilot_ms) {
	if (pilot_ms <= 0) return 1;
	long k = (long)std::ceil(kTargetMs / pilot_ms);
	if (k < 1) k = 1;
	if (k > kMaxSorts) k = kMaxSorts;
	return k;
}

template <typename T, typename Less, typename SortFn>
static void run(const char *impl, const char *type, const char *shape, const char *file,
                size_t n, int reps, Less less, SortFn do_sort, bool (*check)(const std::vector<T> &)) {
	auto src = load<T>(file, n);
	std::vector<T> work(n);
	double pilot = 0, warm = now_ms();
	do {
		std::copy(src.begin(), src.end(), work.begin());
		double t0 = now_ms();
		do_sort(work, less);
		pilot = now_ms() - t0;
	} while (now_ms() - warm < kWarmupMs);

	long K = batch_count(pilot);
	for (int rep = 1; rep <= reps; rep++) {
		std::copy(src.begin(), src.end(), work.begin());
		double total = 0;
		for (long k = 0; k < K; k++) {
			double t0 = now_ms();
			do_sort(work, less);
			total += now_ms() - t0;
			if (k + 1 < K) std::copy(src.begin(), src.end(), work.begin());
		}
		emit(impl, type, shape, n, rep, total / K, check(work));
	}
}

template <typename T>
static bool less_ptr_cmp(const T &a, const T &b) { return a < b; }

template <typename T>
static void run_typed(const char *impl, const char *type, const char *shape, const char *file,
                      size_t n, int reps) {
	using Less = std::less<T>;
	Less less;
	const bool numeric = std::is_arithmetic_v<T>;
	const std::string i = impl;

	if (i == "std::sort")
		run<T>(impl, type, shape, file, n, reps, less, [](auto &v, auto &l) { std::sort(v.begin(), v.end(), l); }, verify<T>);
	else if (i == "std::stable_sort")
		run<T>(impl, type, shape, file, n, reps, less, [](auto &v, auto &l) { std::stable_sort(v.begin(), v.end(), l); }, verify<T>);
	else if (i == "std::sort/ptr")
		run<T>(impl, type, shape, file, n, reps, less, [](auto &v, auto &) {
			std::sort(v.begin(), v.end(), &less_ptr_cmp<T>);
		}, verify<T>);
	else if (i == "noop") // baseline for the memory phase
		run<T>(impl, type, shape, file, n, reps, less, [](auto &, auto &) {}, [](const std::vector<T> &) { return true; });
	else if (i == "pdqsort")
		run<T>(impl, type, shape, file, n, reps, less, [](auto &v, auto &l) { pdqsort(v.begin(), v.end(), l); }, verify<T>);
	else if (i == "ska_sort" && numeric)
		run<T>(impl, type, shape, file, n, reps, less, [](auto &v, auto &) { ska_sort(v.begin(), v.end()); }, verify<T>);
	else if (i == "ips4o" && numeric)
		run<T>(impl, type, shape, file, n, reps, less, [](auto &v, auto &) { ips4o::sort(v.begin(), v.end()); }, verify<T>);
	else {
		std::fprintf(stderr, "unknown impl %s for type %s\n", impl, type);
		std::exit(2);
	}
}

template <typename T, int Words>
static void run_rec(const char *impl, const char *type, const char *shape, const char *file,
                    size_t n, int reps) {
	auto less = [](const T &a, const T &b) { return a.key < b.key; };
	const std::string i = impl;
	auto check = [](const std::vector<T> &v) { return verify_rec<T, Words>(v); };

	if (i == "std::sort")
		run<T>(impl, type, shape, file, n, reps, less, [](auto &v, auto &l) { std::sort(v.begin(), v.end(), l); }, check);
	else if (i == "std::stable_sort")
		run<T>(impl, type, shape, file, n, reps, less, [](auto &v, auto &l) { std::stable_sort(v.begin(), v.end(), l); }, check);
	else if (i == "noop")
		run<T>(impl, type, shape, file, n, reps, less, [](auto &, auto &) {}, [](const std::vector<T> &) { return true; });
	else if (i == "pdqsort")
		run<T>(impl, type, shape, file, n, reps, less, [](auto &v, auto &l) { pdqsort(v.begin(), v.end(), l); }, check);
	else if (i == "ska_sort")
		run<T>(impl, type, shape, file, n, reps, less, [](auto &v, auto &) {
			ska_sort(v.begin(), v.end(), [](const T &r) { return r.key; });
		}, check);
	else {
		std::fprintf(stderr, "unknown impl %s for type %s\n", impl, type);
		std::exit(2);
	}
}

int main(int argc, char **argv) {
	if (argc != 7) {
		std::fprintf(stderr, "usage: cppsort IMPL TYPE SHAPE FILE N REPS\n");
		return 2;
	}
	const char *impl = argv[1], *type = argv[2], *shape = argv[3], *file = argv[4];
	size_t n = std::strtoul(argv[5], nullptr, 10);
	int reps = std::atoi(argv[6]);

	const std::string t = type;
	if (t == "i32") run_typed<int32_t>(impl, type, shape, file, n, reps);
	else if (t == "i64") run_typed<int64_t>(impl, type, shape, file, n, reps);
	else if (t == "f64") run_typed<double>(impl, type, shape, file, n, reps);
	else if (t == "str") run_typed<std::string>(impl, type, shape, file, n, reps);
	else if (t == "rec16") run_rec<Rec16, 3>(impl, type, shape, file, n, reps);
	else if (t == "rec32") run_rec<Rec32, 7>(impl, type, shape, file, n, reps);
	else { std::fprintf(stderr, "unknown type %s\n", type); return 2; }
	return 0;
}
