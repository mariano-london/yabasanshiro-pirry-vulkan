#pragma once
/* On-disk cache for compiled shaders (SPIR-V).

   The renderer compiles ~60 GLSL shaders with shaderc, most of them at start-up and the rest the first time a game uses an
   effect (the RBG compute variants compile mid-game). On the RK3566 that is the very slow launch and the multi-second freezes
   the first time each effect appears (a 6.3 s freeze at the first demo scene, 0.5-1.7 s ones after). The result only depends on
   the source text and the options, so it is computed once and stored:

     <cache dir>/<hash>.spv      cache dir = $YABA_SHADER_CACHE, else $HOME/.yabasanshiro/shadercache

   The key is two independent 64-bit hashes of (options tag, shaderc SPIR-V version, shader kind, full source). Files are
   written to a temporary name and renamed, and validated on load (size multiple of 4, SPIR-V magic), so a half-written or
   corrupt file is just a miss. It is a drop-in for CompileGlslToSpv: same GetNumErrors/GetErrorMessage/cbegin/cend. */
#include <shaderc/shaderc.hpp>
#include <string>
#include <vector>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <chrono>

struct YabaSpvResult {
  std::vector<uint32_t> words;
  size_t errors = 0;
  std::string message;
  size_t GetNumErrors() const { return errors; }
  std::string GetErrorMessage() const { return message; }
  const uint32_t * cbegin() const { return words.data(); }
  const uint32_t * cend() const { return words.data() + words.size(); }
};

namespace yaba_shader_cache {
inline uint64_t fnv1a(const std::string & s, uint64_t h) {
  for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
  return h;
}
inline std::string cacheDir() {
  const char * e = getenv("YABA_SHADER_CACHE");
  if (e && *e) return e;
  const char * home = getenv("HOME");
  return std::string(home ? home : "/tmp") + "/.yabasanshiro/shadercache";
}
inline void mkdirs(const std::string & path) {
  std::string cur;
  for (size_t i = 0; i < path.size(); i++) {
    cur += path[i];
    if (path[i] == '/' && i > 0) mkdir(cur.c_str(), 0755);
  }
  mkdir(path.c_str(), 0755);
}
}

inline YabaSpvResult YabaCompileGlslCached(shaderc::Compiler & compiler, const std::string & source, shaderc_shader_kind kind,
                                           const char * name, const shaderc::CompileOptions & options, const char * optionsTag)
{
  using namespace yaba_shader_cache;
  YabaSpvResult out;
  uint32_t spvVersion = 0, spvRev = 0;
  shaderc_get_spv_version(&spvVersion, &spvRev);
  char head[160];
  snprintf(head, sizeof(head), "%s|spv%08x.%u|kind%d|", optionsTag, spvVersion, spvRev, (int)kind);
  const std::string keyed = std::string(head) + source;
  char fname[64];
  snprintf(fname, sizeof(fname), "%016llx%016llx.spv", (unsigned long long)fnv1a(keyed, 1469598103934665603ull),
           (unsigned long long)fnv1a(keyed, 0x9E3779B97F4A7C15ull));
  const std::string dir = cacheDir();
  const std::string path = dir + "/" + fname;

  if (FILE * f = fopen(path.c_str(), "rb")) {
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz >= 20 && (sz % 4) == 0) {
      out.words.resize(sz / 4);
      if (fread(out.words.data(), 1, (size_t)sz, f) == (size_t)sz && out.words[0] == 0x07230203u) {
        fclose(f);
        printf("SHADERCACHE: hit  %s (%ld bytes)\n", name, sz);
        return out;
      }
    }
    fclose(f);
    out.words.clear();
  }

  const auto t0 = std::chrono::steady_clock::now();
  shaderc::SpvCompilationResult r = compiler.CompileGlslToSpv(source, kind, name, options);
  out.errors = r.GetNumErrors();
  out.message = r.GetErrorMessage();
  if (out.errors == 0) {
    out.words.assign(r.cbegin(), r.cend());
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    printf("SHADERCACHE: miss %s compiled in %.0f ms\n", name, ms);
    mkdirs(dir);
    const std::string tmp = path + ".tmp" + std::to_string((long)getpid());
    if (FILE * f = fopen(tmp.c_str(), "wb")) {
      const size_t n = out.words.size() * 4;
      const bool ok = fwrite(out.words.data(), 1, n, f) == n;
      fclose(f);
      if (ok) rename(tmp.c_str(), path.c_str()); else remove(tmp.c_str());
    }
  }
  return out;
}
