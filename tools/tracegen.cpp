// tracegen: synthetic memory traces in the simple "<op> <hex-addr> <size>" format.
//
//   tracegen conflict [--arrays 4] [--stride 32K] [--n 100000] [--elem 8]
//       Streams over `arrays` arrays whose base addresses are `stride` apart
//       (stride = cache size => every array maps to the same sets). Loop body:
//       read a0[i] ... read a{k-2}[i], write a{k-1}[i]  (e.g. d[i] = a[i]+b[i]+c[i]).
//   tracegen matmul [--n 128] [--tile 0]      C += A*B on doubles (ijk, or tiled if tile>0)
//   tracegen transpose [--n 512] [--tile 0]   B = A^T on doubles
//   tracegen stream [--bytes 4M] [--elem 8] [--passes 2]
//   tracegen random [--ws 1M] [--n 1000000] [--elem 8] [--seed 1] [--write-frac 0.3]
//   tracegen stride [--stride 4K] [--count 64] [--passes 100]

#include <cinttypes>
#include <cstdio>
#include <random>
#include <string>

#include "cachesim/config.h"
#include "cli_common.h"

using namespace cachesim;

namespace {

void emit(char op, uint64_t addr, uint32_t size) {
  std::printf("%c %" PRIx64 " %u\n", op, addr, size);
}

uint64_t num(const std::map<std::string, std::string>& a, const char* k, const char* def) {
  return parseSize(cli::get(a, k, def));
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: tracegen conflict|matmul|transpose|stream|random|stride [opts]\n");
    return 2;
  }
  std::string kind = argv[1];
  auto args = cli::parseArgs(argc - 1, argv + 1);
  const uint64_t base = 0x10000000;

  if (kind == "conflict") {
    uint64_t arrays = num(args, "arrays", "4"), stride = num(args, "stride", "32K");
    uint64_t n = num(args, "n", "100000"), elem = num(args, "elem", "8");
    for (uint64_t i = 0; i < n; ++i) {
      for (uint64_t a = 0; a < arrays; ++a)
        emit(a + 1 == arrays ? 'W' : 'R', base + a * stride + (i * elem) % stride, uint32_t(elem));
    }
  } else if (kind == "matmul") {
    uint64_t n = num(args, "n", "128"), tile = num(args, "tile", "0");
    uint64_t A = base, B = A + n * n * 8 + 4096, C = B + n * n * 8 + 4096;
    auto at = [&](uint64_t m, uint64_t i, uint64_t j) { return m + (i * n + j) * 8; };
    if (tile == 0) {
      for (uint64_t i = 0; i < n; ++i)
        for (uint64_t j = 0; j < n; ++j) {
          emit('R', at(C, i, j), 8);
          for (uint64_t k = 0; k < n; ++k) {
            emit('R', at(A, i, k), 8);
            emit('R', at(B, k, j), 8);
          }
          emit('W', at(C, i, j), 8);
        }
    } else {
      for (uint64_t ii = 0; ii < n; ii += tile)
        for (uint64_t kk = 0; kk < n; kk += tile)
          for (uint64_t jj = 0; jj < n; jj += tile)
            for (uint64_t i = ii; i < std::min(ii + tile, n); ++i)
              for (uint64_t k = kk; k < std::min(kk + tile, n); ++k) {
                emit('R', at(A, i, k), 8);
                for (uint64_t j = jj; j < std::min(jj + tile, n); ++j) {
                  emit('R', at(B, k, j), 8);
                  emit('M', at(C, i, j), 8);
                }
              }
    }
  } else if (kind == "transpose") {
    uint64_t n = num(args, "n", "512"), tile = num(args, "tile", "0");
    uint64_t A = base, B = A + n * n * 8;
    if (tile == 0) tile = n;
    for (uint64_t ii = 0; ii < n; ii += tile)
      for (uint64_t jj = 0; jj < n; jj += tile)
        for (uint64_t i = ii; i < std::min(ii + tile, n); ++i)
          for (uint64_t j = jj; j < std::min(jj + tile, n); ++j) {
            emit('R', A + (i * n + j) * 8, 8);
            emit('W', B + (j * n + i) * 8, 8);
          }
  } else if (kind == "stream") {
    uint64_t bytes = num(args, "bytes", "4M"), elem = num(args, "elem", "8");
    uint64_t passes = num(args, "passes", "2");
    for (uint64_t p = 0; p < passes; ++p)
      for (uint64_t off = 0; off < bytes; off += elem) emit('R', base + off, uint32_t(elem));
  } else if (kind == "random") {
    uint64_t ws = num(args, "ws", "1M"), n = num(args, "n", "1000000"), elem = num(args, "elem", "8");
    std::mt19937_64 rng(num(args, "seed", "1"));
    double wf = std::stod(cli::get(args, "write-frac", "0.3"));
    std::uniform_real_distribution<double> u(0, 1);
    for (uint64_t i = 0; i < n; ++i)
      emit(u(rng) < wf ? 'W' : 'R', base + (rng() % (ws / elem)) * elem, uint32_t(elem));
  } else if (kind == "stride") {
    uint64_t stride = num(args, "stride", "4K"), count = num(args, "count", "64");
    uint64_t passes = num(args, "passes", "100");
    for (uint64_t p = 0; p < passes; ++p)
      for (uint64_t i = 0; i < count; ++i) emit('R', base + i * stride, 8);
  } else {
    std::fprintf(stderr, "unknown pattern %s\n", kind.c_str());
    return 2;
  }
  return 0;
}
