// sweep: run one trace across many L1 configurations and tabulate miss
// rates, the 3C breakdown and AMAT.
//
//   sweep --trace FILE [--l1 32K:64:1:lru:1] [--l2 SPEC] [--assoc 1,2,4,8,16]
//         [--policies lru,fifo,plru,random] [--sizes 32K] [--csv out.csv]
//         [--inclusion ...] [--mem-latency 100]
//
// The trace is loaded into memory once and replayed for every configuration.

#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>

#include "cachesim/hierarchy.h"
#include "cachesim/trace.h"
#include "cli_common.h"

using namespace cachesim;

int main(int argc, char** argv) {
  auto args = cli::parseArgs(argc, argv);
  if (!args.count("trace")) {
    std::cerr << "usage: sweep --trace FILE [--l1 SPEC] [--l2 SPEC] [--assoc 1,2,4,8,16] "
                 "[--policies lru,...] [--sizes 32K,64K] [--csv FILE]\n";
    return 2;
  }
  try {
    std::vector<MemAccess> trace;
    {
      auto in = cli::openTrace(args.at("trace"));
      TraceOptions topts;
      topts.include_instructions = args.count("instr") > 0;
      forEachAccess(*in, topts, [&](const MemAccess& a) { trace.push_back(a); });
    }
    HierarchyConfig base = cli::hierarchyFromArgs(args);
    auto assocs = cli::splitList(cli::get(args, "assoc", "1,2,4,8,16"));
    auto policies = cli::splitList(cli::get(args, "policies", toString(base.levels[0].policy)));
    auto sizes = cli::splitList(cli::get(args, "sizes", std::to_string(base.levels[0].size_bytes)));

    std::unique_ptr<std::ofstream> csv;
    if (args.count("csv")) {
      csv = std::make_unique<std::ofstream>(args.at("csv"));
      *csv << "l1_size,l1_assoc,policy,accesses,misses,miss_rate,compulsory,capacity,conflict,"
              "amat_measured,amat_analytic\n";
    }

    std::cout << "trace: " << args.at("trace") << " (" << trace.size() << " references), "
              << base.levels.size() << " level(s), memory " << base.memory_latency << " cycles\n\n";
    std::cout << std::left << std::setw(8) << "L1 size" << std::setw(7) << "assoc" << std::setw(8)
              << "policy" << std::right << std::setw(10) << "miss%" << std::setw(11) << "compuls."
              << std::setw(11) << "capacity" << std::setw(11) << "conflict" << std::setw(10)
              << "AMAT" << "\n";

    for (const auto& sz : sizes) {
      for (const auto& pol : policies) {
        for (const auto& as : assocs) {
          HierarchyConfig cfg = base;
          cfg.levels[0].size_bytes = parseSize(sz);
          cfg.levels[0].assoc = as == "full" ? 0 : static_cast<uint32_t>(std::stoul(as));
          cfg.levels[0].policy = parsePolicy(pol);
          Hierarchy h(cfg);
          for (const auto& a : trace) h.access(a.addr, a.size, a.kind == MemAccess::Kind::Write);
          const LevelStats& s = h.stats().levels[0];
          std::cout << std::left << std::setw(8) << (std::to_string(cfg.levels[0].size_bytes / 1024) + "K")
                    << std::setw(7) << as << std::setw(8) << toString(cfg.levels[0].policy)
                    << std::right << std::fixed << std::setprecision(2) << std::setw(9)
                    << 100 * s.missRate() << "%" << std::setw(11) << s.compulsory << std::setw(11)
                    << s.capacity << std::setw(11) << s.conflict << std::setw(10)
                    << h.stats().measuredAMAT() << "\n";
          if (csv)
            *csv << cfg.levels[0].size_bytes << "," << as << "," << toString(cfg.levels[0].policy)
                 << "," << s.accesses() << "," << s.misses() << "," << s.missRate() << ","
                 << s.compulsory << "," << s.capacity << "," << s.conflict << ","
                 << h.stats().measuredAMAT() << "," << h.analyticAMAT() << "\n";
        }
      }
    }
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }
  return 0;
}
