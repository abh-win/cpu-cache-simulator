# CPU Cache Simulator

A trace-driven, multi-level (L1/L2/L3) set-associative cache simulator in C++17. It models
replacement policy, write policy and inclusion policy, computes hierarchical AMAT, and
classifies every miss as compulsory, capacity or conflict using a shadow fully-associative cache.

## Features

- **Geometry**: any power-of-two block size and set count; direct-mapped through fully associative (`full`).
- **Replacement**: LRU, FIFO, tree-PLRU and Random behind a pure virtual `ReplacementPolicy`
  interface (`include/cachesim/replacement.h`). `makePolicies(cfg)` is a factory that builds one
  `std::unique_ptr<ReplacementPolicy>` per set, sized to the associativity (tree-PLRU allocates
  `ways-1` tree bits and rejects non-power-of-two associativity).
- **Write policies** (all 4): write-back / write-through × write-allocate / no-write-allocate, set per level.
- **Inclusion**: inclusive (an L2 eviction back-invalidates L1 and rescues dirty L1 data), exclusive
  (misses fill only L1; L1 victims, clean or dirty, are installed in L2; an L2 hit moves the line up),
  and NINE (non-inclusive, non-exclusive).
- **AMAT**: measured, as total simulated cycles per reference, and analytic, as
  `t1 + m1·(t2 + m2·(… + mN·t_mem))` from the local miss rates. The two agree whenever write
  traffic is buffered (the default). With `--no-write-buffer`, write-backs and write-through
  stores stall the access, and the measured value shows that cost.
- **3C miss classification**: each level has a `MissClassifier` with a set of blocks seen so far
  (compulsory) and an LRU fully-associative shadow cache of the same capacity, fed the same demand
  stream. A miss that the shadow also misses is capacity; a miss the shadow would have hit is conflict.
- **Traces**: Valgrind Lackey format (`valgrind --tool=lackey --trace-mem=yes ./prog`) or a simple
  `<r|w|m> <hex-addr> [size]` format, auto-detected per line. References that span a block boundary
  are split. `tracegen` produces synthetic kernels (conflict streams, naive/tiled matmul and
  transpose, streaming, random, strided).
- ~5.3 M references/s with 3C classification, ~7.6 M/s without (i5-1235U, two levels).

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
./build/cachesim_tests                 # 32 GoogleTest cases
scripts/run_experiments.sh             # generates traces/, writes every table below
```

## Usage

```bash
# Level spec: size:block:assoc:policy[:hit_latency]
./build/cachesim --trace traces/matmul96.trace \
    --l1 32K:64:8:lru:1 --l2 256K:64:8:plru:10 \
    --l1-write wb-wa --l2-write wb-wa --inclusion inclusive --mem-latency 100

# Sweep L1 associativity × policy; also writes a CSV
./build/sweep --trace traces/conflict4.trace --l1 32K:32:1:lru:1 \
    --assoc 1,2,4,8,16,full --policies lru,fifo,plru,random --csv results/sweep.csv

# Real program traces (needs valgrind)
valgrind --tool=lackey --trace-mem=yes --log-file=prog.trace ./prog
./build/cachesim --trace prog.trace --instr      # --instr also simulates instruction fetches
```

## Results (`results/experiments.txt`)

### 1. Associativity sweep on a conflict-heavy kernel

The kernel `d[i] = a[i] + b[i] + c[i]` uses four 32 KiB arrays whose bases are exactly one cache
size apart, so element `i` of every array maps to the same set. The cache is a single 32 KiB L1
with 32 B blocks and 1-cycle hits, backed by 100-cycle memory.

| assoc | miss rate | compulsory | capacity | conflict | AMAT (cycles) |
|---|---|---|---|---|---|
| 1 (direct) | 100 % | 4,096 | 95,904 | 300,000 | **101.0** |
| 2 | 100 % | 4,096 | 95,904 | 300,000 | 101.0 |
| **4** | 25 % | 4,096 | 95,904 | **0** | **26.0** |
| 8 / 16 / full | 25 % | 4,096 | 95,904 | 0 | 26.0 |

All four streams collide in every set. Below 4 ways, each access evicts a line that is needed
again three accesses later, so the 300 K conflict misses disappear only at 4-way. The remaining
25 % are one miss per 32 B block per array (4 × 8 B elements per block). The shadow cache
classifies them as compulsory or capacity, because the 128 KiB footprint is larger than the cache.
With LRU, FIFO and PLRU the 4-way result is exact. With Random it is not: 48.1 cycles at 4-way,
28.4 at 16-way.

### 2. Replacement policies on naive matmul (96×96 doubles, 8 KiB L1 + 64 KiB L2)

L1 miss rate is about 51.7 % for every policy and associativity from 2 to 8, and about 88 % of
those misses are conflict misses. The column walk through `B` has a stride of 768 B (12 blocks),
which reaches only 8 of the 32 sets. A fully-associative cache of the same size would hold the
column. Random does slightly better here (50.0 % at 8-way, AMAT 12.10 vs 12.49) because it
sometimes escapes the LRU-pathological cyclic pattern.

### 3. Locality transformations (32 KiB 8-way L1 + 256 KiB 8-way PLRU L2)

| kernel | L1 miss rate | AMAT |
|---|---|---|
| matmul 96 naive (ijk) | 6.31 % | 1.83 |
| matmul 96 tiled (16) | 0.34 % | 1.16 |
| transpose 256 naive | 56.3 % | 62.9 |
| transpose 256 tiled (8) | 12.5 % | 14.8 |

### 4. Inclusion policies (random 144 KiB working set, 32 KiB L1 + 128 KiB L2, 30 % writes)

| inclusion | L2 local miss rate | memory reads | memory writes | AMAT |
|---|---|---|---|---|
| inclusive | 14.5 % | 112,712 | 87,843 | 20.05 |
| NINE | 14.5 % | 112,725 | 87,860 | 20.05 |
| **exclusive** | **6.2 %** | **48,201** | 41,409 | **13.60** |

The exclusive hierarchy's effective capacity is L1 + L2 = 160 KiB, which covers the 144 KiB working
set; inclusive and NINE are limited to the L2's 128 KiB. Inclusive recorded 21 back-invalidations.

### 5. Write policies (8 KiB L1 only, random 256 KiB working set, `--no-write-buffer`)

| policy | memory reads | memory writes | AMAT measured | AMAT analytic |
|---|---|---|---|---|
| WB-WA | 484,477 | 148,432 | 127.6 | 97.9 |
| WB-NWA | 339,195 | 149,892 | 98.8 | 97.9 |
| WT-WA | 484,477 | 149,971 | 127.9 | 97.9 |
| WT-NWA | 339,195 | 149,971 | 98.8 | 97.9 |

With no write buffer, every write-back or write-through store stalls the access. That is why
measured AMAT exceeds the read-path formula, and why no-write-allocate wins on a write-heavy
workload with little reuse.

## Layout

```
include/cachesim/   config.h  replacement.h  cache.h  classifier.h  hierarchy.h  trace.h
src/                config.cpp  replacement.cpp  cache.cpp  classifier.cpp  hierarchy.cpp  trace.cpp
tools/              cachesim.cpp (CLI)  sweep.cpp  tracegen.cpp
tests/              replacement, cache, classifier (3C), hierarchy (latency, write modes, inclusion invariants), trace parsing
scripts/            run_experiments.sh
```

## Modelling assumptions

- Tags only (no data); all levels share one block size.
- Latency is additive along the miss path (L1 tag check, then L2 tag check, then memory); there
  is no overlap or MSHR model.
- Write-backs do not update replacement state in the receiving level.
- Exclusive hierarchies require write-back/write-allocate levels (the configuration is rejected otherwise).
