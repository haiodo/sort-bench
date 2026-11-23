// Go: slices.Sort (pdqsort), sort.Slice, slices.SortStableFunc and the
// zhangyunhao116/pdqsort library on the shared inputs.
// Usage: gosort IMPL TYPE SHAPE FILE N REPS
package main

import (
	"cmp"
	"encoding/binary"
	"encoding/json"
	"fmt"
	"math"
	"os"
	"slices"
	"sort"
	"strconv"
	"strings"
	"time"

	"github.com/zhangyunhao116/pdqsort"
)

const (
	warmup   = 300 * time.Millisecond
	target   = 200 * time.Millisecond
	maxSorts = 20000
)

type Rec16 struct {
	Key int32
	P   [3]uint32
}

type Rec32 struct {
	Key int32
	P   [7]uint32
}

type result struct {
	Lang string  `json:"lang"`
	Impl string  `json:"impl"`
	Type string  `json:"type"`
	Dist string  `json:"dist"`
	N    int     `json:"n"`
	Rep  int     `json:"rep"`
	Ms   float64 `json:"ms"`
	NsEl float64 `json:"ns_el"`
	OK   bool    `json:"ok"`
}

func main() {
	if len(os.Args) != 7 {
		fmt.Fprintln(os.Stderr, "usage: gosort IMPL TYPE SHAPE FILE N REPS")
		os.Exit(2)
	}
	impl, typ, shape, file := os.Args[1], os.Args[2], os.Args[3], os.Args[4]
	n, _ := strconv.Atoi(os.Args[5])
	reps, _ := strconv.Atoi(os.Args[6])

	switch typ {
	case "i32":
		runOrdered[int32](impl, typ, shape, file, n, reps)
	case "i64":
		runOrdered[int64](impl, typ, shape, file, n, reps)
	case "f64":
		runOrdered[float64](impl, typ, shape, file, n, reps)
	case "str":
		runOrdered[string](impl, typ, shape, file, n, reps)
	case "rec16":
		runRec(impl, typ, shape, file, n, reps, func(b []byte) Rec16 {
			return Rec16{Key: int32(binary.LittleEndian.Uint32(b)),
				P: [3]uint32{binary.LittleEndian.Uint32(b[4:]), binary.LittleEndian.Uint32(b[8:]), binary.LittleEndian.Uint32(b[12:])}}
		}, 3, func(r Rec16) int32 { return r.Key }, func(r Rec16) []uint32 { return r.P[:] })
	case "rec32":
		runRec(impl, typ, shape, file, n, reps, func(b []byte) Rec32 {
			var r Rec32
			r.Key = int32(binary.LittleEndian.Uint32(b))
			for i := range r.P {
				r.P[i] = binary.LittleEndian.Uint32(b[4+4*i:])
			}
			return r
		}, 7, func(r Rec32) int32 { return r.Key }, func(r Rec32) []uint32 { return r.P[:] })
	default:
		fatal(fmt.Errorf("unknown type %q", typ))
	}
}

func batch(pilot time.Duration) int {
	if pilot <= 0 {
		return 1
	}
	k := int(math.Ceil(float64(target) / float64(pilot)))
	if k < 1 {
		k = 1
	}
	if k > maxSorts {
		k = maxSorts
	}
	return k
}

func runOrdered[T cmp.Ordered](impl, typ, shape, file string, n, reps int) {
	src := loadOrdered[T](file, n)
	work := make([]T, n)

	do := func() {
		switch impl {
		case "slices.Sort":
			slices.Sort(work)
		case "slices.SortStableFunc":
			slices.SortStableFunc(work, cmp.Compare[T])
		case "sort.Slice":
			sort.Slice(work, func(i, j int) bool { return work[i] < work[j] })
		case "pdqsort":
			pdqsort.Slice(work)
		case "noop": // baseline for the memory phase
		default:
			fatal(fmt.Errorf("unknown impl %q for type %s", impl, typ))
		}
	}

	timeIt(impl, typ, shape, n, reps, src, work, do, func() bool { return impl == "noop" || slices.IsSorted(work) })
}

func runRec[T any](impl, typ, shape, file string, n, reps int, decode func([]byte) T, words int, key func(T) int32, payload func(T) []uint32) {
	raw, err := os.ReadFile(file)
	check(err)
	esz := 16
	if words == 7 {
		esz = 32
	}
	if len(raw) != n*esz {
		fatal(fmt.Errorf("want %d bytes, got %d", n*esz, len(raw)))
	}
	src := make([]T, n)
	for i := range src {
		src[i] = decode(raw[i*esz:])
	}
	work := make([]T, n)

	cmpFn := func(a, b T) int { return cmp.Compare(key(a), key(b)) }

	do := func() {
		switch impl {
		case "slices.SortFunc":
			slices.SortFunc(work, cmpFn)
		case "slices.SortStableFunc":
			slices.SortStableFunc(work, cmpFn)
		case "sort.Slice":
			sort.Slice(work, func(i, j int) bool { return cmpFn(work[i], work[j]) < 0 })
		case "noop":
		default:
			fatal(fmt.Errorf("unknown impl %q for type %s", impl, typ))
		}
	}

	checkOK := func() bool {
		if impl == "noop" {
			return true
		}
		for i := 1; i < n; i++ {
			if cmpFn(work[i-1], work[i]) > 0 {
				return false
			}
		}
		for i := 0; i < n; i++ {
			for w, got := range payload(work[i]) {
				if got != recWord(key(work[i]), w) {
					return false
				}
			}
		}
		return true
	}

	timeIt(impl, typ, shape, n, reps, src, work, do, checkOK)
}

func recWord(key int32, w int) uint32 {
	switch w {
	case 0:
		return uint32(key) * 2654435761
	case 1:
		return ^uint32(key)
	case 2:
		return uint32(key) + 0x9e3779b9
	default:
		return uint32(key) ^ (0x85ebca6b * uint32(w))
	}
}

func timeIt[T any](impl, typ, shape string, n, reps int, src, work []T, do func(), sorted func() bool) {
	var pilot time.Duration
	start := time.Now()
	for time.Since(start) < warmup {
		copy(work, src)
		t0 := time.Now()
		do()
		pilot = time.Since(t0)
	}
	K := batch(pilot)

	for rep := 1; rep <= reps; rep++ {
		copy(work, src)
		var total time.Duration
		for k := 0; k < K; k++ {
			t0 := time.Now()
			do()
			total += time.Since(t0)
			if k+1 < K {
				copy(work, src)
			}
		}
		ms := float64(total.Nanoseconds()) / 1e6 / float64(K)
		b, err := json.Marshal(result{Lang: "go", Impl: impl, Type: typ, Dist: shape, N: n, Rep: rep,
			Ms: ms, NsEl: ms * 1e6 / float64(n), OK: sorted()})
		check(err)
		fmt.Println(string(b))
	}
}

func loadOrdered[T cmp.Ordered](file string, n int) []T {
	raw, err := os.ReadFile(file)
	check(err)
	if _, ok := any(*new(T)).(string); ok {
		s := strings.Split(strings.TrimSuffix(string(raw), "\n"), "\n")
		if len(s) != n {
			fatal(fmt.Errorf("want %d strings, got %d", n, len(s)))
		}
		out := make([]T, n)
		for i := range s {
			out[i] = any(s[i]).(T)
		}
		return out
	}
	esz := 4
	if _, ok := any(*new(T)).(int64); ok {
		esz = 8
	}
	if _, ok := any(*new(T)).(float64); ok {
		esz = 8
	}
	if len(raw) != n*esz {
		fatal(fmt.Errorf("want %d bytes, got %d", n*esz, len(raw)))
	}
	out := make([]T, n)
	for i := range out {
		switch any(*new(T)).(type) {
		case int32:
			out[i] = any(int32(binary.LittleEndian.Uint32(raw[i*4:]))).(T)
		case int64:
			out[i] = any(int64(binary.LittleEndian.Uint64(raw[i*8:]))).(T)
		case float64:
			out[i] = any(math.Float64frombits(binary.LittleEndian.Uint64(raw[i*8:]))).(T)
		}
	}
	return out
}

func check(err error) {
	if err != nil {
		fatal(err)
	}
}

func fatal(err error) {
	fmt.Fprintln(os.Stderr, err)
	os.Exit(1)
}
