// Parallel int32 sort with pthreads: qsort chunks, then merge runs pairwise.
// Usage: cparallel FILE N THREADS REPS
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_THREADS 64

typedef struct { int lo, hi; } run_t;

static int cmp_i32(const void *a, const void *b) {
	int32_t x = *(const int32_t *)a, y = *(const int32_t *)b;
	return (x > y) - (x < y);
}

static void merge_into(int32_t *dst, const int32_t *src, int lo, int mid, int hi) {
	int i = lo, j = mid, k = lo;
	while (i < mid && j < hi) {
		if (src[i] <= src[j]) dst[k++] = src[i++];
		else dst[k++] = src[j++];
	}
	while (i < mid) dst[k++] = src[i++];
	while (j < hi) dst[k++] = src[j++];
}

typedef struct {
	int32_t *work, *buf, *cur, *other;
	run_t *runs;
	int count, pairs;
} ctx_t;

static void *sort_chunk(void *arg) {
	ctx_t *c = arg;
	qsort(c->cur + c->runs[0].lo, (size_t)(c->runs[0].hi - c->runs[0].lo), sizeof(int32_t), cmp_i32);
	return NULL;
}

// One thread per level-pair: each thread takes one pair by index.
typedef struct { int32_t *cur, *other; run_t pair[2]; int odd; } merge_arg_t;

static void *merge_pair(void *arg) {
	merge_arg_t *m = arg;
	if (m->odd) {
		memcpy(m->other + m->pair[0].lo, m->cur + m->pair[0].lo,
		       sizeof(int32_t) * (size_t)(m->pair[0].hi - m->pair[0].lo));
	} else {
		merge_into(m->other, m->cur, m->pair[0].lo, m->pair[0].hi, m->pair[1].hi);
	}
	return NULL;
}

typedef struct { int32_t *work; run_t r; } chunk_arg_t;

static void *chunk_sort(void *arg) {
	chunk_arg_t *c = arg;
	qsort(c->work + c->r.lo, (size_t)(c->r.hi - c->r.lo), sizeof(int32_t), cmp_i32);
	return NULL;
}

static void sort_parallel(int32_t *work, int32_t *buf, int n, int threads) {
	run_t runs[MAX_THREADS];
	int count = 0, chunk = (n + threads - 1) / threads;
	for (int s = 0; s < n; s += chunk) runs[count++] = (run_t){s, s + chunk < n ? s + chunk : n};

	pthread_t th[MAX_THREADS];
	chunk_arg_t ca[MAX_THREADS];
	for (int i = 0; i < count; i++) {
		ca[i] = (chunk_arg_t){work, runs[i]};
		pthread_create(&th[i], NULL, chunk_sort, &ca[i]);
	}
	for (int i = 0; i < count; i++) pthread_join(th[i], NULL);

	int32_t *cur = work, *other = buf;
	while (count > 1) {
		run_t next[MAX_THREADS];
		int ncount = 0, npairs = 0;
		merge_arg_t ma[MAX_THREADS];
		pthread_t mt[MAX_THREADS];

		for (int i = 0; i < count; i += 2) {
			int hi = i + 1 < count ? runs[i + 1].hi : runs[i].hi;
			next[ncount++] = (run_t){runs[i].lo, hi};
			ma[npairs].cur = cur;
			ma[npairs].other = other;
			ma[npairs].pair[0] = runs[i];
			ma[npairs].pair[1] = i + 1 < count ? runs[i + 1] : runs[i];
			ma[npairs].odd = i + 1 >= count;
			pthread_create(&mt[npairs], NULL, merge_pair, &ma[npairs]);
			npairs++;
		}
		for (int i = 0; i < npairs; i++) pthread_join(mt[i], NULL);

		int32_t *tmp = cur; cur = other; other = tmp;
		memcpy(runs, next, sizeof(run_t) * (size_t)ncount);
		count = ncount;
	}
	if (cur != work) memcpy(work, cur, sizeof(int32_t) * (size_t)n);
}

static int sorted(const int32_t *a, int n) {
	for (int i = 1; i < n; i++)
		if (a[i - 1] > a[i]) return 0;
	return 1;
}

static double now_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

int main(int argc, char **argv) {
	if (argc != 5) {
		fprintf(stderr, "usage: cparallel FILE N THREADS REPS\n");
		return 2;
	}
	int n = atoi(argv[2]), threads = atoi(argv[3]), reps = atoi(argv[4]);
	FILE *f = fopen(argv[1], "rb");
	if (!f) { perror(argv[1]); return 1; }
	int32_t *src = malloc((size_t)n * 4), *work = malloc((size_t)n * 4), *buf = malloc((size_t)n * 4);
	if (fread(src, 4, (size_t)n, f) != (size_t)n) { fprintf(stderr, "short read\n"); return 1; }
	fclose(f);

	double warm = now_ms();
	do {
		memcpy(work, src, (size_t)n * 4);
		sort_parallel(work, buf, n, threads);
	} while (now_ms() - warm < 300);

	for (int rep = 1; rep <= reps; rep++) {
		memcpy(work, src, (size_t)n * 4);
		double t0 = now_ms();
		sort_parallel(work, buf, n, threads);
		double ms = now_ms() - t0;
		printf("{\"lang\":\"c\",\"impl\":\"parallel\",\"type\":\"i32\",\"dist\":\"random\",\"n\":%d,"
		       "\"threads\":%d,\"rep\":%d,\"ms\":%.3f,\"ns_el\":%.3f,\"ok\":%s}\n",
		       n, threads, rep, ms, ms * 1e6 / n, sorted(work, n) ? "true" : "false");
	}
	return 0;
}
