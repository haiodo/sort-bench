// C baseline: qsort from libc on the shared int32/int64/float64/string/record
// inputs. Usage: csort IMPL TYPE SHAPE FILE N REPS
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define WARMUP_MS 300.0
#define TARGET_MS 200.0
#define MAX_SORTS 20000

typedef struct { int32_t key; uint32_t p[3]; } rec16_t;
typedef struct { int32_t key; uint32_t p[7]; } rec32_t;

_Static_assert(sizeof(rec16_t) == 16, "rec16 size");
_Static_assert(sizeof(rec32_t) == 32, "rec32 size");

enum kind { K_I32, K_I64, K_F64, K_REC16, K_REC32, K_STR };

static int cmp_i32(const void *a, const void *b) {
	int32_t x = *(const int32_t *)a, y = *(const int32_t *)b;
	return (x > y) - (x < y);
}

static int cmp_i64(const void *a, const void *b) {
	int64_t x = *(const int64_t *)a, y = *(const int64_t *)b;
	return (x > y) - (x < y);
}

static int cmp_f64(const void *a, const void *b) {
	double x = *(const double *)a, y = *(const double *)b;
	return (x > y) - (x < y);
}

static int cmp_rec16(const void *a, const void *b) {
	int32_t x = ((const rec16_t *)a)->key, y = ((const rec16_t *)b)->key;
	return (x > y) - (x < y);
}

static int cmp_rec32(const void *a, const void *b) {
	int32_t x = ((const rec32_t *)a)->key, y = ((const rec32_t *)b)->key;
	return (x > y) - (x < y);
}

static int cmp_str(const void *a, const void *b) {
	return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static double now_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

static void *read_raw(const char *path, size_t bytes) {
	FILE *f = fopen(path, "rb");
	if (!f) { perror(path); exit(1); }
	void *buf = malloc(bytes);
	if (fread(buf, 1, bytes, f) != bytes) {
		fprintf(stderr, "short read %s\n", path);
		exit(1);
	}
	fclose(f);
	return buf;
}

static char **read_strs(const char *path, int n) {
	FILE *f = fopen(path, "rb");
	if (!f) { perror(path); exit(1); }
	char *buf = NULL;
	size_t cap = 0;
	getdelim(&buf, &cap, '\0', f); // whole file
	fclose(f);

	char **a = malloc((size_t)n * sizeof *a);
	int k = 0;
	char *p = buf;
	while (k < n) {
		char *nl = strchr(p, '\n');
		if (!nl) break;
		*nl = '\0';
		a[k++] = p;
		p = nl + 1;
	}
	if (k != n) {
		fprintf(stderr, "want %d strings, got %d\n", n, k);
		exit(1);
	}
	return a;
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

static int ok_rec(const void *base, int n, int wide) {
	const uint8_t *p = base;
	int esz = wide ? 32 : 16, words = wide ? 7 : 3;
	int32_t prev = INT32_MIN;
	for (int i = 0; i < n; i++, p += esz) {
		int32_t key;
		memcpy(&key, p, 4);
		if (i > 0 && prev > key) return 0;
		prev = key;
		for (int w = 0; w < words; w++) {
			uint32_t v;
			memcpy(&v, p + 4 + 4 * w, 4);
			if (v != rec_word(key, w)) return 0;
		}
	}
	return 1;
}

static int ok_i32(const int32_t *a, int n) {
	for (int i = 1; i < n; i++)
		if (a[i - 1] > a[i]) return 0;
	return 1;
}

static int ok_i64(const int64_t *a, int n) {
	for (int i = 1; i < n; i++)
		if (a[i - 1] > a[i]) return 0;
	return 1;
}

static int ok_f64(const double *a, int n) {
	for (int i = 1; i < n; i++)
		if (a[i - 1] > a[i]) return 0;
	return 1;
}

static int ok_str(char **a, int n) {
	for (int i = 1; i < n; i++)
		if (strcmp(a[i - 1], a[i]) > 0) return 0;
	return 1;
}

static int verify(enum kind k, const void *work, int n) {
	switch (k) {
	case K_I32: return ok_i32(work, n);
	case K_I64: return ok_i64(work, n);
	case K_F64: return ok_f64(work, n);
	case K_REC16: return ok_rec(work, n, 0);
	case K_REC32: return ok_rec(work, n, 1);
	case K_STR: return ok_str((char **)work, n);
	}
	return 0;
}

static void emit(const char *impl, const char *type, const char *shape, int n, int rep, double ms, int ok) {
	printf("{\"lang\":\"c\",\"impl\":\"%s\",\"type\":\"%s\",\"dist\":\"%s\",\"n\":%d,\"rep\":%d,"
	       "\"ms\":%.4f,\"ns_el\":%.3f,\"ok\":%s}\n",
	       impl, type, shape, n, rep, ms, ms * 1e6 / n, ok ? "true" : "false");
}

static long batch_count(double pilot_ms) {
	if (pilot_ms <= 0) return 1;
	long k = (long)ceil(TARGET_MS / pilot_ms);
	if (k < 1) k = 1;
	if (k > MAX_SORTS) k = MAX_SORTS;
	return k;
}

int main(int argc, char **argv) {
	if (argc != 7) {
		fprintf(stderr, "usage: csort IMPL TYPE SHAPE FILE N REPS\n");
		return 2;
	}
	const char *impl = argv[1], *type = argv[2], *shape = argv[3], *file = argv[4];
	int n = atoi(argv[5]), reps = atoi(argv[6]);

	enum kind k;
	size_t esz;
	int (*cmp)(const void *, const void *);
	if (!strcmp(type, "i32")) { k = K_I32; esz = 4; cmp = cmp_i32; }
	else if (!strcmp(type, "i64")) { k = K_I64; esz = 8; cmp = cmp_i64; }
	else if (!strcmp(type, "f64")) { k = K_F64; esz = 8; cmp = cmp_f64; }
	else if (!strcmp(type, "rec16")) { k = K_REC16; esz = 16; cmp = cmp_rec16; }
	else if (!strcmp(type, "rec32")) { k = K_REC32; esz = 32; cmp = cmp_rec32; }
	else if (!strcmp(type, "str")) { k = K_STR; esz = sizeof(char *); cmp = cmp_str; }
	else { fprintf(stderr, "unknown type %s\n", type); return 2; }

	void *src;
	if (k == K_STR) src = read_strs(file, n);
	else src = read_raw(file, (size_t)n * esz);

	int noop = !strcmp(impl, "noop"); // baseline for the memory phase
	void *work = malloc((size_t)n * esz);
	double pilot = 0, warm = now_ms();
	do {
		memcpy(work, src, (size_t)n * esz);
		double t0 = now_ms();
		if (!noop) qsort(work, (size_t)n, esz, cmp);
		pilot = now_ms() - t0;
	} while (now_ms() - warm < WARMUP_MS);

	long K = batch_count(pilot);
	for (int rep = 1; rep <= reps; rep++) {
		memcpy(work, src, (size_t)n * esz);
		double total = 0;
		for (long j = 0; j < K; j++) {
			double t0 = now_ms();
			if (!noop) qsort(work, (size_t)n, esz, cmp);
			total += now_ms() - t0;
			if (j + 1 < K) memcpy(work, src, (size_t)n * esz);
		}
		emit(impl, type, shape, n, rep, total / K, noop || verify(k, work, n));
	}
	return 0;
}
