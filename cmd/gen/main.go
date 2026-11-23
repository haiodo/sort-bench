// gen writes shared inputs for every language: little-endian numeric arrays,
// newline-separated string keys and fixed-size records. All languages read the
// same bytes.
//
// Shapes: random, sorted, reverse, nearly (0.1% random swaps), few (1000
// distinct), equal (all equal), organ (ascending then descending), saw
// (repeating 1024-value ramps), small (values < 2^16).
package main

import (
	"encoding/binary"
	"flag"
	"fmt"
	"math"
	"os"
	"path/filepath"
	"slices"
)

func mulberry32(seed uint32) func() uint32 {
	return func() uint32 {
		seed += 0x6D2B79F5
		t := seed
		t = (t ^ (t >> 15)) * (t | 1)
		t ^= t + (t^(t>>7))*(t|61)
		return t ^ (t >> 14)
	}
}

var i32Shapes = []string{"random", "sorted", "reverse", "nearly", "few", "equal", "organ", "saw", "small"}
var plainShapes = []string{"random", "sorted", "reverse"}

func main() {
	out := flag.String("out", "data", "output dir")
	types := flag.String("types", "i32,i64,f64,str,rec16,rec32", "comma separated")
	sizes := flag.String("sizes", "1000,10000,100000,1000000,10000000", "comma separated")
	flag.Parse()
	if err := os.MkdirAll(*out, 0o755); err != nil {
		fatal(err)
	}

	for _, typ := range split(*types) {
		for _, ns := range split(*sizes) {
			n := atoi(ns)
			genType(*out, typ, n)
		}
	}
}

func genType(out, typ string, n int) {
	rnd := mulberry32(42)
	base := make([]int32, n)
	for i := range base {
		base[i] = int32(rnd() >> 1) // < 2^31: SMI in every engine
	}

	switch typ {
	case "i32":
		for _, shape := range i32Shapes {
			vals := shapeI32(base, shape, rnd)
			write(filepath.Join(out, fmt.Sprintf("i32-%s-%d.bin", shape, n)), packI32(vals))
		}
	case "i64":
		vals := make([]int64, n)
		for i := range vals {
			// 53 bits: exact in float64, so JS Number comparisons stay fair.
			vals[i] = int64(rnd()>>11)<<21 | int64(rnd()>>11)
		}
		genPlain(out, "i64", n, vals, func(v int64) []byte {
			b := make([]byte, 8)
			binary.LittleEndian.PutUint64(b, uint64(v))
			return b
		})
	case "f64":
		vals := make([]float64, n)
		for i := range vals {
			vals[i] = float64(uint64(rnd())<<32|uint64(rnd())) / (1 << 64)
		}
		genPlain(out, "f64", n, vals, func(v float64) []byte {
			b := make([]byte, 8)
			binary.LittleEndian.PutUint64(b, math.Float64bits(v))
			return b
		})
	case "str":
		for _, shape := range plainShapes {
			vals := shapeI32(base, shape, rnd)
			write(filepath.Join(out, fmt.Sprintf("str-%s-%d.txt", shape, n)), packStrings(vals))
		}
	case "rec16", "rec32":
		for _, shape := range plainShapes {
			vals := shapeI32(base, shape, rnd)
			write(filepath.Join(out, fmt.Sprintf("%s-%s-%d.bin", typ, shape, n)), packRecs(vals, typ == "rec32"))
		}
	default:
		fatal(fmt.Errorf("unknown type %q", typ))
	}
	fmt.Printf("%s n=%d ok\n", typ, n)
}

func genPlain[T int64 | float64](out, typ string, n int, vals []T, enc func(T) []byte) {
	for _, shape := range plainShapes {
		v := slices.Clone(vals)
		switch shape {
		case "sorted":
			slices.Sort(v)
		case "reverse":
			slices.Sort(v)
			slices.Reverse(v)
		}
		buf := make([]byte, 0, n*len(enc(vals[0])))
		for _, x := range v {
			buf = append(buf, enc(x)...)
		}
		write(filepath.Join(out, fmt.Sprintf("%s-%s-%d.bin", typ, shape, n)), buf)
	}
}

func shapeI32(base []int32, shape string, rnd func() uint32) []int32 {
	n := len(base)
	switch shape {
	case "random":
		return base
	case "sorted":
		v := slices.Clone(base)
		slices.Sort(v)
		return v
	case "reverse":
		v := slices.Clone(base)
		slices.Sort(v)
		slices.Reverse(v)
		return v
	case "nearly":
		v := slices.Clone(base)
		slices.Sort(v)
		for i := 0; i < n/1000; i++ {
			a, b := int(rnd()%uint32(n)), int(rnd()%uint32(n))
			v[a], v[b] = v[b], v[a]
		}
		return v
	case "few":
		v := make([]int32, n)
		for i := range v {
			v[i] = int32(rnd() % 1000)
		}
		return v
	case "equal":
		v := make([]int32, n)
		for i := range v {
			v[i] = 42
		}
		return v
	case "organ":
		v := make([]int32, n)
		for i := range v {
			if i < n/2 {
				v[i] = int32(i)
			} else {
				v[i] = int32(n - i)
			}
		}
		return v
	case "saw":
		v := make([]int32, n)
		for i := range v {
			v[i] = int32(i % 1024)
		}
		return v
	case "small":
		v := make([]int32, n)
		for i := range v {
			v[i] = int32(rnd() % (1 << 16))
		}
		return v
	}
	return nil
}

// Record payload is derived from the key: a sort that moves only keys and
// leaves payload behind is detected by the harness.
func packRecs(vals []int32, wide bool) []byte {
	size := 16
	words := 3
	if wide {
		size, words = 32, 7
	}
	buf := make([]byte, len(vals)*size)
	for i, key := range vals {
		off := i * size
		binary.LittleEndian.PutUint32(buf[off:], uint32(key))
		for w := 0; w < words; w++ {
			binary.LittleEndian.PutUint32(buf[off+4+4*w:], recWord(key, w))
		}
	}
	return buf
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
		return uint32(key) ^ uint32(0x85ebca6b*uint32(w))
	}
}

func packI32(vals []int32) []byte {
	buf := make([]byte, 4*len(vals))
	for i, v := range vals {
		binary.LittleEndian.PutUint32(buf[4*i:], uint32(v))
	}
	return buf
}

func packStrings(vals []int32) []byte {
	buf := make([]byte, 0, len(vals)*15)
	for _, v := range vals {
		buf = fmt.Appendf(buf, "key-%010d\n", v)
	}
	return buf
}

func write(path string, b []byte) {
	if err := os.WriteFile(path, b, 0o644); err != nil {
		fatal(err)
	}
}

func split(s string) []string {
	var out []string
	start := 0
	for i := 0; i <= len(s); i++ {
		if i == len(s) || s[i] == ',' {
			if i > start {
				out = append(out, s[start:i])
			}
			start = i + 1
		}
	}
	return out
}

func atoi(s string) int {
	n := 0
	fmt.Sscan(s, &n)
	if n <= 0 {
		fatal(fmt.Errorf("bad size %q", s))
	}
	return n
}

func fatal(err error) {
	fmt.Fprintln(os.Stderr, err)
	os.Exit(1)
}
