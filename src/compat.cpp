#include "compat.hpp"
namespace sq80compat {
static std::string g_dir;
void setContentDir(const std::string& dir) { g_dir = dir; }
FILE* openFile(const char* rel, const char* mode) {
  if (g_dir.empty() || !rel) return nullptr;
  std::string p = g_dir + rel;   // g_dir = ".../Content", rel = "/sq80/wave0.bin"
  return std::fopen(p.c_str(), mode);
}
}  // namespace sq80compat
