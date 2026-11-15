#include "banks.hpp"

#include "compat.hpp"
#include <stdio.h>
#include <string.h>


namespace {
struct Bank {
  char letter;
  uint8_t* pcb = nullptr;
  int count = 0;
};
Bank g_banks[3] = {{'D'}, {'C'}, {'U'}};
constexpr int kUserSlots = 40;
const char* kUserPath = "/sq80/user.pcb";

void loadBank(Bank& b, const char* path) {
  FILE* file = sq80compat::openFile(path, "rb");
  if (!file) return;
  fseek(file, 0, SEEK_END);
  long size = ftell(file);
  fseek(file, 0, SEEK_SET);
  int n = size > 0 ? (int)(size / 102) : 0;
  if (n > 128) n = 128;
  if (n <= 0) { fclose(file); return; }
  b.pcb = (uint8_t*)ps_malloc(n * 102);
  if (!b.pcb) { fclose(file); return; }
  size_t got = fread(b.pcb, 1, n * 102, file);
  fclose(file);
  b.count = got / 102;
}
}  // namespace

namespace banks {

void init() {
  loadBank(g_banks[0], "/sq80/bankD.pcb");
  loadBank(g_banks[1], "/sq80/bankC.pcb");
  // user bank: always 40 slots, file contents first, INIT programs for the rest
  Bank& u = g_banks[2];
  loadBank(u, kUserPath);
  uint8_t* full = (uint8_t*)ps_malloc(kUserSlots * 102);
  if (!full) full = (uint8_t*)malloc(kUserSlots * 102);
  if (!full) { u.count = 0; return; }
  uint8_t init[102];
  sq80::Program ip = sq80::preset(0);
  memcpy(ip.name, "INIT  ", 7);
  sq80::encodePcb(ip, init);
  for (int i = 0; i < kUserSlots; ++i) {
    if (u.pcb && i < u.count) memcpy(full + i * 102, u.pcb + i * 102, 102);
    else memcpy(full + i * 102, init, 102);
  }
  if (u.pcb) free(u.pcb);
  u.pcb = full;
  u.count = kUserSlots;
}

int userFirst() { return sq80::presetCount() + g_banks[0].count + g_banks[1].count; }
int userCount() { return g_banks[2].count; }

bool storeUser(int slot, const sq80::Program& p) {
  Bank& u = g_banks[2];
  if (!u.pcb || slot < 0 || slot >= u.count) return false;
  sq80::encodePcb(p, u.pcb + slot * 102);
  FILE* file = sq80compat::openFile(kUserPath, "wb");
  if (!file) return false;
  size_t wrote = fwrite(u.pcb, 1, u.count * 102, file);
  fclose(file);
  return wrote == (size_t)(u.count * 102);
}

int count() { return sq80::presetCount() + g_banks[0].count + g_banks[1].count + g_banks[2].count; }

bool get(int index, sq80::Program& out) {
  int n = count();
  if (n == 0) return false;
  index = ((index % n) + n) % n;
  if (index < sq80::presetCount()) {
    out = sq80::preset(index);
    return true;
  }
  index -= sq80::presetCount();
  for (Bank& b : g_banks) {
    if (index < b.count) {
      sq80::decodePcb(b.pcb + index * 102, out);
      return true;
    }
    index -= b.count;
  }
  return false;
}

void label(int index, char* buf, int len) {
  int n = count();
  if (n == 0) { snprintf(buf, len, "-"); return; }
  index = ((index % n) + n) % n;
  if (index < sq80::presetCount()) {
    snprintf(buf, len, "P%d %s", index + 1, sq80::preset(index).name);
    return;
  }
  index -= sq80::presetCount();
  for (Bank& b : g_banks) {
    if (index < b.count) {
      snprintf(buf, len, "%c%02d %.6s", b.letter, index + 1, (const char*)(b.pcb + index * 102));
      return;
    }
    index -= b.count;
  }
}

}  // namespace banks
