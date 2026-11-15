#!/usr/bin/env python3
"""Build the optional wave-data folder for ESQUE80 from the sq8l-revival/SQ8L sources.

Copy the sq80/ folder this writes into "Lichthaus - VST - ESQUE80/Content/".

    python3 tools/sq8l_to_card.py <path to SQ8L checkout> <output dir>

Writes into <output dir>/sq80/:
    wave0.bin .. wave3.bin   the 256 KiB SQ-80 wave ROM image, 64 KiB per file
    waves.txt                wave -> key range -> DOC wavesample records (75 x 16)
    bankC.pcb, bankD.pcb     factory banks as 102-byte SQ-80 Program Control Blocks
                             (C = SQ8L factory, 128 programs; D = SQ-80 factory, 40)

The wave ROM is Ensoniq's and the banks are Siegfried Kullmann's / Ensoniq's: the SQ8L
repository publishes them for preservation with no licence granted. This script only
reformats them for a private device; nothing it writes belongs in a public repository.
"""
import re
import sys
from pathlib import Path


def ints_of_array(text, name):
    """All integers of the C++ array initialiser `name[...] = { ... };` (comments stripped)."""
    m = re.search(r'\b' + re.escape(name) + r'(\[[^=]*\])*\s*=\s*\{', text)
    if not m:
        raise SystemExit(f'array {name} not found')
    body = text[m.end():]
    body = body[:body.index('};')]
    body = re.sub(r'//[^\n]*', '', body)
    return [int(x) for x in re.findall(r'-?\d+', body)]


# ---- program record (540 bytes) -> SQ-80 PCB (102 bytes), after SysEx.cpp programToNybbles

def source_to_sq80(s):
    if s > 0x0b:
        if s > 0x12:
            if s == 0x34: return 0x0c
            if s == 0x90: return 0x0e
            return 0x0f
        if s == 0x12: return 0x0d
        if s == 0x0c: return 0x0e
        if s == 0x11: return 0x0b
        return 0x0f
    if s == 0x0b: return 0x0a
    if s < 0 or s > 0x0a: return 0x0f
    if s in (0, 1, 2): return s
    if s == 3: return 0x0f
    if s in (4, 5, 6, 7): return s - 1
    if s == 8: return 7
    if s == 9: return 8
    return 9


def s8(b):
    return b - 256 if b >= 128 else b


def u8(v):
    return v & 0xff


def s16(rec, off):
    v = rec[off] | (rec[off + 1] << 8)
    return v - 65536 if v >= 32768 else v


def clamp_s63(v):
    return max(-63, min(63, v))


def clamp_u(v, hi):
    return 0 if v <= 0 else min(v, hi)


def pan_to_sq80(v):
    v = clamp_s63(v)
    if v <= -63: return 0
    return clamp_u(int(v / 8) + 8, 15)


SQ80_DIGITS = [0x21, 0x23, 0x25, 0x28, 0x29, 0x3a, 0x3b, 0x5b, 0x5c, 0x5d]


def name_to_sq80(name):
    out = []
    i = 0
    while i < len(name) and len(out) < 6:
        c = name[i].upper()
        two = i + 1 < len(name) and name[i + 1] == '.'
        if two and c.isdigit():
            out.append(SQ80_DIGITS[int(c)])
            i += 2
            continue
        code = ord(c)
        if 0x20 <= code <= 0x5f and code != 0x20:
            out.append(code)
        i += 1
    while len(out) < 6:
        out.append(0x20)
    return out


def record_to_pcb(rec):
    """Port of programToNybbles (non-ESQ-1 mode) producing the raw 102 bytes."""
    p = rec
    B = p[0x22:]  # parameter body
    n = p[2]
    name = bytes(p[3:3 + n]).decode('latin-1')
    out = bytearray(102)
    out[0:6] = bytes(name_to_sq80(name))
    for e in range(4):
        d = 0x6a + 14 * e
        o = 6 + 10 * e
        for j in range(3):
            out[o + j] = u8(s8(p[d + 1 + j]) * 2)
        for j in range(4):
            out[o + 3 + j] = p[d + 7 + j] & 0x3f
        t4 = s8(p[d + 0x0a])
        if (t4 >> 6) & 1:
            out[o + 6] |= 0x80
        out[o + 7] = u8(p[d + 5] << 2)
        out[o + 8] = p[d + 6] & 0x3f
        out[o + 9] = p[d + 0x0b] & 0x3f
    for l in range(3):
        d = 0xa2 + 16 * l
        o = 0x2e + 4 * l
        freq = clamp_u(s8(p[d] & 0x7f), 0x3f)
        out[o] = (freq & 0x3f) | ((p[d + 3] & 3) << 6)
        src = source_to_sq80(s16(p, d + 7))
        out[o + 1] = (p[d + 4] & 0x3f) | ((src & 0x0c) << 4)
        out[o + 2] = (p[d + 6] & 0x3f) | ((src & 3) << 6)
        reset = 1 if s8(p[d + 1]) >= 0 else 0
        human = 1 if s8(p[d + 2]) > 0 else 0
        out[o + 3] = (p[d + 5] & 0x3f) | (reset << 7) | (human << 6)
    for oi in range(3):
        d = 0x22 + 0x18 * oi
        o = 0x3a + 10 * oi
        v = (s8(p[d]) + 3) * 12 + s8(p[d + 1])
        f = s8(p[d + 2])
        if f < 0:
            v -= 1
            f += 0x20
            if v < 0: v = 0
        out[o] = v & 0x7f
        out[o + 1] = u8(f << 3)
        out[o + 2] = source_to_sq80(s16(p, d + 5)) | (source_to_sq80(s16(p, d + 8)) << 4)
        out[o + 3] = u8(s8(p[d + 7]) * 2)
        out[o + 4] = u8(s8(p[d + 0x0a]) * 2)
        w = s16(p, d + 3)
        out[o + 5] = w & 0xff
        out[o + 6] = u8(p[d + 0x0e] * 2) & 0x7f
        out[o + 6] |= (p[d + 0x0f] & 1) << 7
        out[o + 7] = source_to_sq80(s16(p, d + 0x10)) | (source_to_sq80(s16(p, d + 0x13)) << 4)
        out[o + 8] = u8(s8(p[d + 0x12]) * 2)
        out[o + 9] = u8(s8(p[d + 0x15]) * 2)
    o = 0x58
    a = 0x132
    out[o] = u8(p[a] * 2) & 0x7f
    out[o + 0x0c] = (pan_to_sq80(s8(p[a + 1])) << 4) | source_to_sq80(s16(p, a + 5))
    out[o + 0x0d] = clamp_s63(s8(p[a + 7])) & 0x7f
    f = 0x106
    out[o + 1] = p[f] & 0x7f
    out[o + 2] = clamp_u(s8(p[f + 1]), 31)
    out[o + 3] = source_to_sq80(s16(p, f + 4)) | (source_to_sq80(s16(p, f + 7)) << 4)
    out[o + 4] = clamp_s63(s8(p[f + 6])) & 0x7f
    out[o + 5] = clamp_s63(s8(p[f + 9])) & 0x7f
    out[o + 6] = (clamp_u(s8(p[f + 2]), 63) * 2) & 0x7f
    m = 0x170
    am = s8(p[m + 1])
    if am > 1: am = 1
    sync = s8(p[m])
    if am > 0 and sync > 0: am = 0
    out[o] |= (am & 1) << 7
    out[o + 1] |= (sync & 1) << 7
    out[o + 4] |= (p[m + 4] & 1) << 7
    out[o + 5] |= (p[m + 2] & 1) << 7
    out[o + 6] |= (p[m + 5] & 1) << 7
    out[o + 0x0d] |= (p[m + 7] & 1) << 7
    out[o + 7] = (p[m + 3] & 0x3f) | ((p[m + 6] & 1) << 7)
    return bytes(out)


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    src = Path(sys.argv[1])
    out = Path(sys.argv[2]) / 'sq80'
    out.mkdir(parents=True, exist_ok=True)

    rom = ints_of_array((src / 'src/engine/data/WaveRomData.cpp').read_text(), 'kWaveRom')
    assert len(rom) == 262144, len(rom)
    for i in range(4):
        (out / f'wave{i}.bin').write_bytes(bytes(rom[i * 65536:(i + 1) * 65536]))

    wp = (src / 'src/engine/data/WaveParamsData.cpp').read_text()
    keymap = ints_of_array(wp, 'kWaveKeyMap')
    recs = ints_of_array(wp, 'kWaveSampleRecords')
    assert len(keymap) == 1200 and len(recs) == 604
    with open(out / 'waves.txt', 'w') as f:
        f.write('# wave -> key/8 bucket -> DOC wavesample: page waveReg semiOfs fineOfs\n')
        f.write('# waveReg: bit7 bank0, bit6 bank1, bits5-3 size code, bits2-0 resolution\n')
        for w in range(75):
            f.write(f'W {w}\n')
            for k in range(16):
                r = keymap[w * 16 + k] * 4
                page, reg, semi, fine = recs[r], recs[r + 1], recs[r + 2], recs[r + 3]
                f.write(f'K {k} {page} {reg} {s8(semi)} {fine}\n')

    fb = (src / 'src/engine/data/FactoryBanks.cpp').read_text()
    for bank, count in (('C', 128), ('D', 40)):
        data = ints_of_array(fb, f'kFactoryBank{bank}')
        assert len(data) == count * 540, (bank, len(data))
        pcbs = bytearray()
        names = []
        for i in range(count):
            rec = data[i * 540:(i + 1) * 540]
            pcb = record_to_pcb(rec)
            pcbs += pcb
            names.append(pcb[:6])
        (out / f'bank{bank}.pcb').write_bytes(bytes(pcbs))
        print(f'bank {bank}: {count} programs, first:', [bytes(n).decode("latin-1") for n in names[:8]])
    print('wrote', out)


if __name__ == '__main__':
    main()
