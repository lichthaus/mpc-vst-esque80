#pragma once
// Compatibility shim: lets the ESP32 SQ-80 engine (lifted verbatim from
// esp32-sq80/src/synth) build against the MPC plugin framework. It stands in for
// <Arduino.h> (timing, PSRAM alloc, Serial logging) and the SD/LittleFS storage
// layer, reading the wave ROM and banks from the plugin's Content/ folder.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static inline uint32_t micros() { return 0; }   // engine uses this only for a perf stat
static inline uint32_t millis() { return 0; }
static inline void* ps_malloc(std::size_t n) { return std::malloc(n); }

struct Sq80SerialStub {
  void println(const char*) {}
  int  printf(const char*, ...) { return 0; }
};
[[maybe_unused]] static Sq80SerialStub Serial;

namespace sq80compat {
  void  setContentDir(const std::string& dir);          // the plugin's Content/ folder
  FILE* openFile(const char* relPath, const char* mode);// relPath like "/sq80/wave0.bin"
}
