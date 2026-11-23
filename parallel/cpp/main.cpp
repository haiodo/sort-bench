// Parallel int32 sort: sort T chunks in parallel, then merge runs pairwise,
// one thread per pair per level. Usage: cppparallel FILE N THREADS REPS
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

using Clock = std::chrono::steady_clock;

struct Run { int lo, hi; };

static double now_ms() {
	return std::chrono::duration<double, std::milli>(Clock::now().time_since_epoch()).count();
}

static void sort_parallel(int32_t *work, int32_t *buf, int n, int threads) {
	std::vector<Run> runs;
	int chunk = (n + threads - 1) / threads;
	for (int s = 0; s < n; s += chunk)
		runs.push_back({s, std::min(n, s + chunk)});

	std::vector<std::thread> ts;
	for (auto r : runs)
		ts.emplace_back([=] { std::sort(work + r.lo, work + r.hi); });
	for (auto &t : ts) t.join();

	int32_t *cur = work, *other = buf;
	while (runs.size() > 1) {
		std::vector<Run> next;
		for (size_t i = 0; i < runs.size(); i += 2)
			next.push_back({runs[i].lo, i + 1 < runs.size() ? runs[i + 1].hi : runs[i].hi});

		std::vector<std::thread> mt;
		for (size_t i = 0; i < runs.size(); i += 2) {
			if (i + 1 == runs.size()) {
				Run r = runs[i];
				mt.emplace_back([=] { std::memcpy(other + r.lo, cur + r.lo, sizeof(int32_t) * (size_t)(r.hi - r.lo)); });
			} else {
				Run a = runs[i], b = runs[i + 1];
				mt.emplace_back([=] { std::merge(cur + a.lo, cur + a.hi, cur + b.lo, cur + b.hi, other + a.lo); });
			}
		}
		for (auto &t : mt) t.join();
		std::swap(cur, other);
		runs.swap(next);
	}
	if (cur != work) std::memcpy(work, cur, sizeof(int32_t) * (size_t)n);
}

static bool sorted(const int32_t *a, int n) {
	for (int i = 1; i < n; i++)
		if (a[i - 1] > a[i]) return false;
	return true;
}

int main(int argc, char **argv) {
	if (argc != 5) {
		std::fprintf(stderr, "usage: cppparallel FILE N THREADS REPS\n");
		return 2;
	}
	const char *file = argv[1];
	int n = std::atoi(argv[2]), threads = std::atoi(argv[3]), reps = std::atoi(argv[4]);

	FILE *f = std::fopen(file, "rb");
	if (!f) { std::perror(file); return 1; }
	std::vector<int32_t> src(n), work(n), buf(n);
	if (std::fread(src.data(), 4, (size_t)n, f) != (size_t)n) {
		std::fprintf(stderr, "short read\n");
		return 1;
	}
	std::fclose(f);

	double warm = now_ms();
	do {
		std::copy(src.begin(), src.end(), work.begin());
		sort_parallel(work.data(), buf.data(), n, threads);
	} while (now_ms() - warm < 300);

	for (int rep = 1; rep <= reps; rep++) {
		std::copy(src.begin(), src.end(), work.begin());
		double t0 = now_ms();
		sort_parallel(work.data(), buf.data(), n, threads);
		double ms = now_ms() - t0;
		std::printf("{\"lang\":\"cpp\",\"impl\":\"parallel\",\"type\":\"i32\",\"dist\":\"random\",\"n\":%d,"
		            "\"threads\":%d,\"rep\":%d,\"ms\":%.3f,\"ns_el\":%.3f,\"ok\":%s}\n",
		            n, threads, rep, ms, ms * 1e6 / n, sorted(work.data(), n) ? "true" : "false");
	}
	return 0;
}
