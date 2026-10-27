#include "cachesim/trace.h"

#include <cctype>
#include <cstdlib>
#include <stdexcept>

namespace cachesim {
namespace {

uint64_t parseHex(const std::string& s, size_t* pos) {
  size_t i = *pos;
  if (i + 1 < s.size() && s[i] == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X')) i += 2;
  size_t start = i;
  uint64_t v = 0;
  while (i < s.size() && std::isxdigit(static_cast<unsigned char>(s[i]))) {
    char c = s[i];
    int d = std::isdigit(static_cast<unsigned char>(c)) ? c - '0' : (std::tolower(c) - 'a' + 10);
    v = (v << 4) | static_cast<uint64_t>(d);
    ++i;
  }
  if (i == start) throw std::runtime_error("expected hex address in: " + s);
  *pos = i;
  return v;
}

void skipSpace(const std::string& s, size_t* pos) {
  while (*pos < s.size() && std::isspace(static_cast<unsigned char>(s[*pos]))) ++(*pos);
}

}  // namespace

int parseTraceLine(const std::string& line, MemAccess out[2]) {
  size_t pos = 0;
  skipSpace(line, &pos);
  if (pos >= line.size() || line[pos] == '#' || line[pos] == '=') return 0;

  char op = line[pos++];
  if (pos < line.size() && !std::isspace(static_cast<unsigned char>(line[pos])))
    throw std::runtime_error("bad trace op in: " + line);
  skipSpace(line, &pos);
  uint64_t addr = parseHex(line, &pos);
  uint32_t size = 1;
  skipSpace(line, &pos);
  if (pos < line.size() && line[pos] == ',') {
    ++pos;
    skipSpace(line, &pos);
  }
  if (pos < line.size() && std::isdigit(static_cast<unsigned char>(line[pos])))
    size = static_cast<uint32_t>(std::strtoul(line.c_str() + pos, nullptr, 10));

  switch (op) {
    case 'I': case 'i':
      out[0] = {MemAccess::Kind::Instr, addr, size};
      return 1;
    case 'L': case 'l': case 'R': case 'r':
      out[0] = {MemAccess::Kind::Read, addr, size};
      return 1;
    case 'S': case 's': case 'W': case 'w':
      out[0] = {MemAccess::Kind::Write, addr, size};
      return 1;
    case 'M': case 'm':
      out[0] = {MemAccess::Kind::Read, addr, size};
      out[1] = {MemAccess::Kind::Write, addr, size};
      return 2;
    default:
      throw std::runtime_error(std::string("unknown trace op '") + op + "' in: " + line);
  }
}

uint64_t forEachAccess(std::istream& in, const TraceOptions& opts,
                       const std::function<void(const MemAccess&)>& fn) {
  std::string line;
  MemAccess buf[2];
  uint64_t n = 0;
  uint64_t lineno = 0;
  while (std::getline(in, line)) {
    ++lineno;
    int k;
    try {
      k = parseTraceLine(line, buf);
    } catch (const std::runtime_error& e) {
      throw std::runtime_error("line " + std::to_string(lineno) + ": " + e.what());
    }
    for (int i = 0; i < k; ++i) {
      if (buf[i].kind == MemAccess::Kind::Instr && !opts.include_instructions) continue;
      fn(buf[i]);
      ++n;
    }
  }
  return n;
}

}  // namespace cachesim
