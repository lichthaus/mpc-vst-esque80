// Program list: the built-in presets followed by the SQ-80 program banks found in storage
// (/sq80/bankD.pcb = 40 SQ-80 factory programs, /sq80/bankC.pcb = 128), each a sequence of
// 102-byte Program Control Blocks exactly as a SysEx dump carries them, then the user bank
// U01..U40 in /sq80/user.pcb (created with INIT programs when missing, rewritten on STORE).
#pragma once
#include "program.hpp"
namespace banks {
void init();                                  // loads the .pcb files (after storage::begin)
int count();
bool get(int index, sq80::Program& out);
void label(int index, char* buf, int len);    // e.g. "P1 BASIC", "D07 HUSH", "C23 ...", "U01 ..."
int userFirst();                              // program index of U01
int userCount();                              // 40
bool storeUser(int slot, const sq80::Program& p);  // 0-based slot; writes the bank file
}
