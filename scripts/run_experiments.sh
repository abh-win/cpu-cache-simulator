#!/usr/bin/env bash
# Regenerates the synthetic traces and every table in README.md.
# Usage: scripts/run_experiments.sh [build-dir]
set -euo pipefail
cd "$(dirname "$0")/.."
B=${1:-build}
mkdir -p traces results

[ -f traces/conflict4.trace ]      || $B/tracegen conflict --arrays 4 --stride 32K --n 100000 --elem 8 > traces/conflict4.trace
[ -f traces/matmul96.trace ]       || $B/tracegen matmul --n 96 > traces/matmul96.trace
[ -f traces/matmul96_tiled.trace ] || $B/tracegen matmul --n 96 --tile 16 > traces/matmul96_tiled.trace
[ -f traces/transpose256.trace ]   || $B/tracegen transpose --n 256 > traces/transpose256.trace
[ -f traces/transpose256_tiled.trace ] || $B/tracegen transpose --n 256 --tile 8 > traces/transpose256_tiled.trace
[ -f traces/random256k.trace ]     || $B/tracegen random --ws 256K --n 500000 > traces/random256k.trace
[ -f traces/random144k.trace ]     || $B/tracegen random --ws 144K --n 1000000 --seed 2 > traces/random144k.trace

echo "== 1. Associativity sweep on the conflict-heavy kernel (single 32 KiB L1, 32 B blocks, 100-cycle memory)"
$B/sweep --trace traces/conflict4.trace --l1 32K:32:1:lru:1 --assoc 1,2,4,8,16,full \
         --policies lru,fifo,plru,random --csv results/conflict_sweep.csv

echo; echo "== 2. Replacement policies on matrix multiply (96x96 doubles), 8 KiB L1 + 64 KiB L2"
$B/sweep --trace traces/matmul96.trace --l1 8K:64:4:lru:1 --l2 64K:64:8:lru:10 --assoc 2,4,8 \
         --policies lru,fifo,plru,random --csv results/matmul_policies.csv

echo; echo "== 3. Naive vs tiled matmul and transpose (32K L1 8-way LRU + 256K L2 8-way PLRU)"
for t in matmul96 matmul96_tiled transpose256 transpose256_tiled; do
  echo "-- $t"
  $B/cachesim --trace traces/$t.trace --l1 32K:64:8:lru:1 --l2 256K:64:8:plru:10 2>/dev/null \
    | grep -E "local miss rate|AMAT"
done

echo; echo "== 4. Inclusion policies, random 144 KiB working set (32K L1 + 128K L2, 30% writes)"
for inc in inclusive nine exclusive; do
  echo "-- $inc"
  $B/cachesim --trace traces/random144k.trace --l1 32K:64:8:lru:1 --l2 128K:64:8:lru:10 \
              --inclusion $inc 2>/dev/null | grep -E "local miss rate|memory:|AMAT|back-inv|victim"
done

echo; echo "== 5. Write policies (L1 only, 8 KiB, random 256 KiB working set, writes stall)"
for wp in wb-wa wb-nwa wt-wa wt-nwa; do
  echo "-- $wp"
  $B/cachesim --trace traces/random256k.trace --l1 8K:64:4:lru:1 --l1-write $wp --no-write-buffer 2>/dev/null \
    | grep -E "memory:|AMAT"
done
