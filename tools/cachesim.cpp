// cachesim: run a memory trace through a configurable cache hierarchy.
//
//   cachesim --trace traces/matmul.trace
//            --l1 32K:64:8:lru:1 --l2 256K:64:8:plru:10
//            [--l1-write wb-wa] [--l2-write wb-wa] [--inclusion inclusive|exclusive|nine]
//            [--mem-latency 100] [--no-write-buffer] [--instr] [--no-classify]
//
// Level spec: size:block:assoc:policy[:hit_latency], assoc may be "full".
// Policies: lru, fifo, plru, random. Write modes: wb-wa, wb-nwa, wt-wa, wt-nwa.

#include <chrono>
#include <iostream>

#include "cachesim/hierarchy.h"
#include "cachesim/trace.h"
#include "cli_common.h"

using namespace cachesim;

int main(int argc, char** argv) {
  auto args = cli::parseArgs(argc, argv);
  if (args.count("help") || !args.count("trace")) {
    std::cerr << "usage: cachesim --trace FILE|- [--l1 SPEC] [--l2 SPEC] [--l3 SPEC] "
                 "[--lN-write wb-wa|wb-nwa|wt-wa|wt-nwa] [--inclusion inclusive|exclusive|nine] "
                 "[--mem-latency N] [--no-write-buffer] [--instr] [--no-classify]\n";
    return 2;
  }
  try {
    HierarchyConfig cfg = cli::hierarchyFromArgs(args);
    Hierarchy h(cfg);
    TraceOptions topts;
    topts.include_instructions = args.count("instr") > 0;
    auto in = cli::openTrace(args.at("trace"));
    auto t0 = std::chrono::steady_clock::now();
    uint64_t n = forEachAccess(*in, topts, [&](const MemAccess& a) {
      h.access(a.addr, a.size, a.kind == MemAccess::Kind::Write);
    });
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::cout << h.report();
    std::cerr << "simulated " << n << " references in " << secs << " s ("
              << static_cast<uint64_t>(n / std::max(secs, 1e-9)) << " refs/s)\n";
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }
  return 0;
}
