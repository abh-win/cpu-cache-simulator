#pragma once

// Memory trace input. Two text formats are accepted (auto-detected per line):
//
//   Valgrind Lackey  (`valgrind --tool=lackey --trace-mem=yes prog`)
//       I  0400d7d4,8        instruction fetch
//        L 04f6b868,8        load
//        S 7ff000398,8       store
//        M 0421c7f0,4        modify (load followed by store)
//
//   Simple           <op> <address> [size]
//       op: r/R/l/L = read, w/W/s/S = write, m/M = modify; address hex (0x optional)
//
// Blank lines and lines starting with '#' or '=' (Valgrind banners) are skipped.

#include <cstdint>
#include <functional>
#include <istream>
#include <optional>
#include <string>

namespace cachesim {

struct MemAccess {
  enum class Kind { Read, Write, Instr } kind;
  uint64_t addr;
  uint32_t size;
};

// Parses one line. Returns 0, 1 or 2 accesses (a modify yields read+write).
// Throws std::runtime_error on malformed input.
int parseTraceLine(const std::string& line, MemAccess out[2]);

struct TraceOptions {
  bool include_instructions = false;  // simulate "I" records as reads
};

// Streams a trace, calling `fn` for each access. Returns the number of
// accesses delivered.
uint64_t forEachAccess(std::istream& in, const TraceOptions& opts,
                       const std::function<void(const MemAccess&)>& fn);

}  // namespace cachesim
