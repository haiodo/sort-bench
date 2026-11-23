// Parallel int32 sort: sort T chunks in parallel, then merge runs pairwise,
// one goroutine per pair per level. Usage: goparallel FILE N THREADS REPS
package main

import (
	"encoding/binary"
	"encoding/json"
	"fmt"
	"os"
	"slices"
	"strconv"
	"sync"
	"time"
)

type run struct{ lo, hi int }

type result struct {
	Lang    string  `json:"lang"`
	Impl    string  `json:"impl"`
	Type    string  `json:"type"`
	Dist    string  `json:"dist"`
	N       int     `json:"n"`
	Threads int     `json:"threads"`
	Rep     int     `json:"rep"`
	Ms      float64 `json:"ms"`
	NsEl    float64 `json:"ns_el"`
	OK      bool    `json:"ok"`
}

func mergeInto(dst, src []int32, lo, mid, hi int) {
	i, j, k := lo, mid, lo
	for i < mid && j < hi {
		if src[i] <= src[j] {
			dst[k] = src[i]
			i++
		} else {
			dst[k] = src[j]
			j++
		}
		k++
	}
	copy(dst[k:], src[i:mid])
	k += mid - i
	copy(dst[k:], src[j:hi])
}

func parallelSort(work, buf []int32, threads int) []int32 {
	n := len(work)
	runs := make([]run, 0, threads)
	chunk := (n + threads - 1) / threads
	for s := 0; s < n; s += chunk {
		runs = append(runs, run{s, min(n, s+chunk)})
	}

	var wg sync.WaitGroup
	for _, r := range runs {
		wg.Add(1)
		go func(r run) {
			defer wg.Done()
			slices.Sort(work[r.lo:r.hi])
		}(r)
	}
	wg.Wait()

	cur, other := work, buf
	for len(runs) > 1 {
		next := make([]run, 0, (len(runs)+1)/2)
		for i := 0; i < len(runs); i += 2 {
			hi := runs[i].hi
			if i+1 < len(runs) {
				hi = runs[i+1].hi
			}
			next = append(next, run{runs[i].lo, hi})
			if i+1 < len(runs) {
				a, b := runs[i], runs[i+1]
				wg.Add(1)
				go func() {
					defer wg.Done()
					mergeInto(other, cur, a.lo, a.hi, b.hi)
				}()
			} else {
				wg.Add(1)
				go func() {
					defer wg.Done()
					copy(other[runs[i].lo:runs[i].hi], cur[runs[i].lo:runs[i].hi])
				}()
			}
		}
		wg.Wait()
		cur, other = other, cur
		runs = next
	}
	return cur
}

func main() {
	if len(os.Args) != 5 {
		fmt.Fprintln(os.Stderr, "usage: goparallel FILE N THREADS REPS")
		os.Exit(2)
	}
	file := os.Args[1]
	n, _ := strconv.Atoi(os.Args[2])
	threads, _ := strconv.Atoi(os.Args[3])
	reps, _ := strconv.Atoi(os.Args[4])

	raw, err := os.ReadFile(file)
	if err != nil {
		panic(err)
	}
	if len(raw) != n*4 {
		panic("short read")
	}
	src := make([]int32, n)
	for i := range src {
		src[i] = int32(binary.LittleEndian.Uint32(raw[4*i:]))
	}
	work := make([]int32, n)
	buf := make([]int32, n)

	start := time.Now()
	for time.Since(start) < 300*time.Millisecond {
		copy(work, src)
		out := parallelSort(work, buf, threads)
		if len(out) > 0 {
			_ = out[0]
		}
	}

	for rep := 1; rep <= reps; rep++ {
		copy(work, src)
		t0 := time.Now()
		out := parallelSort(work, buf, threads)
		ms := float64(time.Since(t0).Nanoseconds()) / 1e6
		ok := slices.IsSorted(out)
		if out != nil && &out[0] != &work[0] {
			copy(work, out)
		}
		b, _ := json.Marshal(result{Lang: "go", Impl: "parallel", Type: "i32", Dist: "random", N: n,
			Threads: threads, Rep: rep, Ms: ms, NsEl: ms * 1e6 / float64(n), OK: ok})
		fmt.Println(string(b))
	}
}

func min(a, b int) int {
	if a < b {
		return a
	}
	return b
}
