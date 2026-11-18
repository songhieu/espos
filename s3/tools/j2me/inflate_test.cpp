// Checks the VM's raw-deflate decoder (src/j2me/vm_inflate.h) against vectors made by Python's zlib
// (j2me/test/gen_inflate.py): NAME.raw is the data, NAME.def its raw deflate stream, NAME.zlib (some) the zlib
// stream PNG IDAT data is. Every API must give the data back; cut-short and damaged streams must fail or stay in
// bounds (run it under ASan: make -C tools/j2me asan). Prints PASS/FAIL lines; exit 1 on a failure.
#include <dirent.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../../src/j2me/vm_inflate.h"

static bool readFile(const std::string& p, std::vector<uint8_t>& out) {
  FILE* f = std::fopen(p.c_str(), "rb");
  if (!f) return false;
  std::fseek(f, 0, SEEK_END);
  long n = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  out.resize((size_t)n);
  bool ok = n == 0 || std::fread(out.data(), 1, (size_t)n, f) == (size_t)n;
  std::fclose(f);
  return ok;
}

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: inflate-test DIR\n");
    return 2;
  }
  std::string dir = argv[1];
  std::vector<std::string> names;
  if (DIR* d = opendir(dir.c_str())) {
    while (dirent* e = readdir(d)) {
      std::string n = e->d_name;
      if (n.size() > 4 && n.compare(n.size() - 4, 4, ".def") == 0) names.push_back(n.substr(0, n.size() - 4));
    }
    closedir(d);
  }
  int fails = 0, checks = 0;
  uint32_t seed = 12345;
  auto rnd = [&seed]() { return seed = seed * 1103515245u + 12345u; };
  for (const std::string& n : names) {
    std::vector<uint8_t> raw, def, zl;
    if (!readFile(dir + "/" + n + ".raw", raw) || !readFile(dir + "/" + n + ".def", def)) {
      std::printf("FAIL %s: cannot read\n", n.c_str());
      fails++;
      continue;
    }
    bool ok = true;
    std::vector<uint8_t> out;
    ok &= j2me::inflateRaw(def.data(), def.size(), out) && out == raw;  // no hint: grows
    ok &= j2me::inflateRaw(def.data(), def.size(), out, 1) && out == raw;  // a hint far too small
    ok &= j2me::inflateRaw(def.data(), def.size(), out, raw.size() + 100) && out == raw;
    j2me::PVec<uint8_t> pout;
    ok &= j2me::inflateRaw(def.data(), def.size(), pout) && pout.size() == raw.size() &&
          std::equal(pout.begin(), pout.end(), raw.begin());
    std::vector<uint8_t> exact(raw.size() + 1);
    ok &= j2me::inflateRaw(def.data(), def.size(), exact.data(), raw.size()) &&
          std::memcmp(exact.data(), raw.data(), raw.size()) == 0;
    if (!raw.empty()) ok &= !j2me::inflateRaw(def.data(), def.size(), exact.data(), raw.size() - 1);  // too small
    if (readFile(dir + "/" + n + ".zlib", zl) && zl.size() > 2)  // PNG style: skip the header, the checksum is left
      ok &= j2me::inflateRaw(zl.data() + 2, zl.size() - 2, out) && out == raw;
    checks += 7;
    // cut short: must fail (and stay in bounds)
    for (size_t cut = 0; cut < def.size(); cut += def.size() / 16 + 1) {
      std::vector<uint8_t> part(def.begin(), def.begin() + (long)cut);
      if (j2me::inflateRaw(part.data(), part.size(), out) && out == raw && cut + 2 < def.size()) {
        std::printf("FAIL %s: cut at %zu still decoded everything\n", n.c_str(), cut);
        ok = false;
      }
      checks++;
    }
    // damaged: any answer, but no crash (ASan checks the bounds)
    for (int k = 0; k < 40 && !def.empty(); k++) {
      std::vector<uint8_t> bad = def;
      for (int j = 0; j < 1 + k % 4; j++) bad[rnd() % bad.size()] ^= (uint8_t)(1u << (rnd() % 8));
      j2me::inflateRaw(bad.data(), bad.size(), out);
      j2me::inflateRaw(bad.data(), bad.size(), exact.data(), raw.size());
      checks++;
    }
    std::printf("%s inflate %s (%zu -> %zu bytes)\n", ok ? "PASS" : "FAIL", n.c_str(), def.size(), raw.size());
    if (!ok) fails++;
  }
  std::printf("%zu vectors, %d checks, %d failed\n", names.size(), checks, fails);
  return fails || names.empty() ? 1 : 0;
}
