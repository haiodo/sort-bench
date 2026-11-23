# sort-bench

Воспроизводимый стенд для сравнения сортировок на одной машине и одном входе:
штатные сортировки C, C++, Go, Swift и Node, библиотеки C++ и Go, распределения
входа, типы элементов, параллельные сортировки и Bun.

> **TL;DR (EN):** one machine, one input, five standard libraries plus C++/Go
> sorting libraries. The same bytes feed every language; every measurement is a
> batch of sorts long enough to keep the CPU at full clock. Shapes, element
> types, parallel chunk+merge sorts and Bun are separate phases.

Цифры: `results/tables.md`. Выводы: `FINDINGS.md`. Источники и что за вызовом:
`SOURCES.md`.

Дополнение с Rust сравнивает `sort` и `sort_unstable` с C, C++ и Go на пяти распределениях миллиона целых. [Методика и команда запуска](rust/README.md), [сырые результаты на Mac mini M4 Pro](results/rust-mini/raw.jsonl).

## Фазы

| фаза | что меряет | где код |
|---|---|---|
| A | штатные сортировки: C `qsort`, C++ `std::sort`, Go `slices.Sort`/`sort.Slice`, Node `Int32Array`/`BigInt64Array`/`Float64Array`/`Array.sort`, Swift `sort()`; int32 random на 1 тыс.-10 млн, 8 распределений, int64/float64/строки/записи 16 и 32 Б | `c/`, `cpp/`, `cmd/gosort`, `node/`, `swift/` |
| B | библиотеки C++: `std::stable_sort`, pdqsort (orlp), ska_sort, ips4o | `cpp/sort.cpp` |
| C | библиотеки Go: zhangyunhao116/pdqsort, `slices.SortStableFunc` | `cmd/gosort` |
| N | Node против Bun на одном входе | `node/sort.mjs` |
| P | параллельная сортировка: T чанков, затем попарные merge; T = 1, 2, 4, 8, 12 | `parallel/` |
| M | память: пик RSS реализации минус `noop`-прогон того же стенда | `mem.sh` |

## Методика

- Вход готовит один генератор (`cmd/gen`), все языки читают одни байты:
  little-endian int32/int64/float64, строки `key-%010d`, записи `key + payload`.
- Распределения int32: `random`, `sorted`, `reverse`, `nearly` (0.1% случайных
  свопов), `few` (1000 различных), `equal`, `organ` (возрастание, затем
  убывание), `saw` (пила по 1024), `small` (значения < 2^16).
- Один замер - серия сортировок, а не одна: 300 мс прогрева и серия около 200 мс.
  На короткой сортировке (5-30 мкс) процессор ещё не вышел на частоту, и цифры
  завышаются в 2-9 раз; это первая находка стенда.
- Между сортировками массив копируется из исходного; копирование в замер не
  входит, но держит процессор под нагрузкой.
- Семь серий, в таблицах медиана; `ns_el` - медиана серии, делённая на N.
- После каждой серии проверяется, что массив отсортирован. Для записей
  проверяется и payload: сортировка, которая двигает только ключи, падает.
- Каждая реализация - в своём процессе: JIT и куча прошлых участников не влияют.

## Запуск

```bash
./run.sh A B C N   # фазы; по умолчанию A
./parallel/run.sh  # фаза P, после ./run.sh A
./mem.sh           # фаза M, int32 1 и 10 млн
python3 parse.py   # results/tables.md
```

C++ можно собрать другим компилятором и получить отдельный набор строк:
`CXX=g++-16 SUFFIX=-gcc LANGS=cpp ./run.sh A B` (нужен `brew install gcc`).

Нужны Go 1.27+, clang, clang++, Swift 6+, Node 24+; для фазы B - заголовки
pdqsort/ska_sort/ips4o и TBB (`brew install tbb`); для фазы N - Bun; для
сравнения компиляторов - GCC (`brew install gcc`). Пути к Node и Bun можно
задать через `NODE=` и `BUN=`, компилятор C++ - через `CXX=`.

Размеры и число серий: `SIZES="1000 1000000" REPS=5 ./run.sh A`.

Результаты: `results/raw/*.jsonl` (по строке на серию), окружение -
`results/env.txt`, сводка - `results/tables.md`.

`./probe.sh` - отдельная проба: тот же `std::sort`, но компаратор передан
указателем на функцию. Показывает, сколько в разрыве C и C++ даёт интерфейс
`qsort`, а сколько язык.

## Оговорки

- Одна машина, один прогон. Абсолютные цифры привязаны к M4 Pro; на x86, другом
  libc++ или libstdc++ их надо перемерять.
- Однопоточные фазы A-C, N: параллельные сортировки - отдельная фаза P.
- Память - пик RSS процесса минус `noop`-прогон, а не точный размер аллокаций:
  аллокатор держит освобождённые страницы, GC - запас. Записи и строки по
  памяти не мерялись.
- C++ в основных таблицах - clang++/libc++; сборка g++/libstdc++ лежит рядом
  (`*-gcc.jsonl`) и разобрана в `FINDINGS.md`.
- Питание: Mac mini на сети, Energy Mode Automatic (High Power без sudo не
  выставился). Все замеры в одном режиме.
- Node `Int32Array.sort`, `BigInt64Array.sort` и `Float64Array.sort` - это не
  «сортировка на JS»: V8 передаёт работу `std::sort` из libc++.
- Swift сравнивает `String` по Unicode-скалярам, а не `memcmp`; для ASCII-ключей
  порядок тот же, цена другая.
- Только int32/int64/float64/ASCII-строки/записи с целым ключом. Структуры с
  дорогим компаратором и длинные строки не проверялись.

## Источники и лицензии

| каталог | источник | лицензия |
|---|---|---|
| `third_party/pdqsort` | [orlp/pdqsort](https://github.com/orlp/pdqsort) | zlib |
| `third_party/ska_sort` | [skarupke/ska_sort](https://github.com/skarupke/ska_sort) | Boost Software License 1.0 |
| `third_party/ips4o` | [ips4o/ips4o](https://github.com/ips4o/ips4o) | BSD 2-Clause |

Остальной код - MIT, см. `LICENSE`. Подробный разбор источников - в
`SOURCES.md`.
