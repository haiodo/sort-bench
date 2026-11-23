# Материал по сортировкам: что пишут и что из этого проверяется

Собрано под стенд `sort-bench`. В первой части - первоисточники (код и release
notes), во второй - вторичные тексты, в третьей - что именно меряется здесь.

## Первоисточники: что стоит за вызовом

| источник | утверждение |
|---|---|
| [Go 1.19 release notes, sort](https://go.dev/doc/go1.19#sort) | «The sorting algorithm has been rewritten to use pattern-defeating quicksort, which is faster for several common scenarios.» С Go 1.19 `sort` и `slices` - pdqsort. |
| [golang/go, `slices/zsortanyfunc.go`](https://github.com/golang/go/blob/master/src/slices/zsortanyfunc.go) | В коде: `pdqsortCmpFunc`, insertion sort до 12 элементов, heapsort при исчерпании лимита, `breakPatterns`, `partialInsertionSort`. |
| [V8 blog, «Getting things sorted in V8»](https://v8.dev/blog/array-sort) | `Array.prototype.sort` с V8 7.0 - TimSort, стабильный. На уже отсортированных данных до 17x быстрее прежнего quicksort; на случайных - бывает медленнее. |
| [V8, `src/runtime/runtime-typedarray.cc`](https://github.com/v8/v8/blob/main/src/runtime/runtime-typedarray.cc) | `Runtime_TypedArraySortFast` для числовых TypedArray вызывает `std::sort` из C++ stdlib. Это не TimSort и не JS-компаратор. |
| [libc++ `__algorithm/sort.h`](https://github.com/llvm/llvm-project/blob/main/libcxx/include/__algorithm/sort.h) | `std::sort` - introsort: quicksort с ninther, heapsort при глубине, insertion sort до 24. Для арифметики - bitset-разбиение, «partly based on Orson Peters' pattern-defeating quicksort». |
| [Apple Libc, `stdlib/FreeBSD/qsort.c`](https://github.com/apple-oss-distributions/Libc/blob/main/stdlib/FreeBSD/qsort.c) | `qsort` - quicksort Бентли-МакИлроя: med3/ninther, heapsort при глубине, insertion sort до 7 элементов, компаратор вызывается через указатель. |
| [Swift `stdlib/public/core/Sort.swift`](https://github.com/swiftlang/swift/blob/main/stdlib/public/core/Sort.swift) | `sort()` стабильный, «adaptive algorithm used is Timsort, modified to perform a straight merge of the elements using a temporary buffer». Буфер - `count/2`. |
| [Swift forums, «Is sort() stable in Swift 5?»](https://forums.swift.org/t/is-sort-stable-in-swift-5/21297) | До Swift 5 стабильность не гарантировалась; в Swift 5 гарантия появилась вместе с Timsort. |

## Вторичные тексты: типичные утверждения

| источник | утверждение | что с ним делать |
|---|---|---|
| [Ugo Arangino, «Sorting in Swift: From Introsort to Timsort» (2026)](https://www.arangino.app/blog/swift-sorting-introsort-to-timsort/) | Swift до 5 использовал introsort, с 5 - Timsort; вариант Swift упрощён: нет galloping, pre-merge binary search и binary insertion sort. | Хорошее объяснение, почему стабильный Timsort Swift на случайных данных заметно медленнее C++ introsort. |
| [Stack Overflow, «Performance of qsort vs std::sort»](https://stackoverflow.com/questions/4708105/performance-of-qsort-vs-stdsort) | `std::sort` быстрее `qsort`, потому что компаратор инлайнится; у `qsort` - косвенный вызов на каждое сравнение. | Проверяется пробой `std::sort` с компаратором-указателем: если она догоняет `qsort`, дело в интерфейсе, а не в языке. |
| [Stack Overflow, «Is there a sorting routine faster than qsort»](https://stackoverflow.com/questions/9846681/is-there-a-sorting-routine-faster-than-qsort) | «C++'s std::sort will beat qsort, because it allows the compiler to optimize away the indirect call». | То же. |
| [Rosetta Code, «Compare sorting algorithms' performance»](https://rosettacode.org/wiki/Compare_sorting_algorithms%27_performance) | Сравнения алгоритмов на разных языках, старые замеры, без единой методики. | Как источник цифр не годится; как список языков - да. |
| [GlebOlsen/benchLANG](https://github.com/GlebOlsen/benchLANG) | «Reproducible benchmark of 20 languages», среди них Swift, Go, JS. | Проверить, есть ли там сортировка и какая методика. |

## Что проверяет этот стенд

1. Одинаковый вход для всех: файлы `int32` и строки готовит один генератор,
   языки читают одни байты.
2. Одинаковый вызов: только штатная сортировка языка, без ручных алгоритмов.
3. Одинаковая методика: прогрев 300 мс, серия сортировок около 200 мс, семь
   серий, медиана, своя процесс на реализацию.
4. Проверка результата: массив после каждой серии проверяется на
   отсортированность; несортированный результат - ошибка прогона.

Цифры - в `results/tables.md`, выводы - в `FINDINGS.md`.

## Что нашёл topic `sorting` в pro2you-my

Поиск по 17 запросам дал 151 адрес. Из них отобраны реализации, которые можно
мерить или цитировать:

| что | где |
|---|---|
| pdqsort | [orlp/pdqsort](https://github.com/orlp/pdqsort), [zhangyunhao116/pdqsort](https://github.com/zhangyunhao116/pdqsort) (Go), [notriddle/pdqsort](https://github.com/notriddle/pdqsort) |
| ips4o | [ips4o/ips4o](https://github.com/ips4o/ips4o), [ips4o/ips4o-benchmark-suite](https://github.com/ips4o/ips4o-benchmark-suite) |
| ska_sort | [skarupke/ska_sort](https://github.com/skarupke/ska_sort), [RadixKit](https://github.com/morishuz/RadixKit) |
| BlockQuicksort | [weissan/BlockQuicksort](https://github.com/weissan/BlockQuicksort), [chkas/blqsort](https://github.com/chkas/blqsort) |
| bitset sort | [minjaehwang/bitsetsort](https://github.com/minjaehwang/bitsetsort) |
| introsort | [AlexandruValeanu/Introsort](https://github.com/AlexandruValeanu/Introsort), [hbollon/Introsort-Fusion](https://github.com/hbollon/Introsort-Fusion) |
| Timsort на C | [patperry/timsort](https://github.com/patperry/timsort/blob/master/timsort.c) |
| наборы бенчмарков | [jeremy-rifkin/Sorting-Benchmarks](https://github.com/jeremy-rifkin/Sorting-Benchmarks), [csrc-lab/sorting-alg-benchmark](https://github.com/csrc-lab/sorting-alg-benchmark), [lukascampos/sorting-algorithms-benchmark](https://github.com/lukascampos/sorting-algorithms-benchmark) |
| старый сводный стенд | [sortbenchmark.org](http://sortbenchmark.org/) |

Статьи и разборы, которые стоит прочитать перед цитированием:

- [pdqsort paper](https://arxiv.org/abs/2106.05123) и [BlockQuicksort](https://arxiv.org/abs/1604.06697);
- [ips4o paper](https://arxiv.org/abs/1705.02257);
- [A New Implementation for std::sort (LLVM, 2023)](https://llvm.org/devmtg/2023-02-25/slides/A-New-Implementation-for-std-sort.pdf) -
  про bitset-разбиение в libc++;
- [libc++ commits, декабрь 2020](https://lists.llvm.org/pipermail/libcxx-commits/2020-December/014474.html) -
  история `std::sort`;
- [pistack: pdqsort vs quadsort vs ips4o vs glidesort](https://pistack.xyz/posts/2026-06-19-sorting-algorithm-libraries-pdqsort-quadsort-ips4o-glidesort);
- [Swift: From Introsort to Timsort](https://www.arangino.app/blog/swift-sorting-introsort-to-timsort).

Чужие стенды в этот прогон не включены: у них разные входы, размеры и методика
замера, поэтому их цифры нельзя ставить рядом с нашими без перепрогона.
