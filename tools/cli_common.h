#pragma once

// Shared command-line handling for cachesim and sweep.

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "cachesim/config.h"

namespace cachesim::cli {

// --key=value or --key value; bare --flag maps to "1".
inline std::map<std::string, std::string> parseArgs(int argc, char** argv) {
  std::map<std::string, std::string> m;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a.rfind("--", 0) != 0) {
      std::cerr << "unexpected argument: " << a << "\n";
      std::exit(2);
    }
    a = a.substr(2);
    auto eq = a.find('=');
    if (eq != std::string::npos) {
      m[a.substr(0, eq)] = a.substr(eq + 1);
    } else if (i + 1 < argc && std::string(argv[i + 1]).rfind("--", 0) != 0) {
      m[a] = argv[++i];
    } else {
      m[a] = "1";
    }
  }
  return m;
}

inline std::string get(const std::map<std::string, std::string>& m, const std::string& k,
                       const std::string& def) {
  auto it = m.find(k);
  return it == m.end() ? def : it->second;
}

// Builds a hierarchy config from --l1/--l2/--l3 specs and policies.
inline HierarchyConfig hierarchyFromArgs(const std::map<std::string, std::string>& args) {
  HierarchyConfig h;
  for (int i = 1; i <= 3; ++i) {
    std::string key = "l" + std::to_string(i);
    auto it = args.find(key);
    if (it == args.end()) {
      if (i == 1) h.levels.push_back(parseLevelSpec("L1", "32K:64:8:lru:1"));
      continue;
    }
    if (it->second == "none") continue;
    CacheConfig c = parseLevelSpec("L" + std::to_string(i), it->second);
    parseWritePolicy(get(args, key + "-write", "wb-wa"), &c.write_hit, &c.write_miss);
    h.levels.push_back(c);
  }
  if (args.count("l1-write") && !h.levels.empty())
    parseWritePolicy(args.at("l1-write"), &h.levels[0].write_hit, &h.levels[0].write_miss);
  h.inclusion = parseInclusion(get(args, "inclusion", "nine"));
  h.memory_latency = static_cast<uint32_t>(std::stoul(get(args, "mem-latency", "100")));
  h.write_buffer = !args.count("no-write-buffer");
  h.classify_misses = !args.count("no-classify");
  return h;
}

inline std::unique_ptr<std::istream> openTrace(const std::string& path) {
  if (path == "-") return std::make_unique<std::istream>(std::cin.rdbuf());
  auto f = std::make_unique<std::ifstream>(path);
  if (!*f) {
    std::cerr << "cannot open trace " << path << "\n";
    std::exit(1);
  }
  return f;
}

inline std::vector<std::string> splitList(const std::string& s) {
  std::vector<std::string> out;
  size_t start = 0;
  while (start <= s.size()) {
    size_t c = s.find(',', start);
    if (c == std::string::npos) c = s.size();
    if (c > start) out.push_back(s.substr(start, c - start));
    start = c + 1;
  }
  return out;
}

}  // namespace cachesim::cli
