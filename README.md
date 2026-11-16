# ESQUE80

![ESQUE80 oscillator page](docs/screenshots/esque80-osc.png)

A cross-wave synthesizer for Akai MPC standalone hardware, modelled on the architecture of
the Ensoniq SQ-80: 8 voices, 3 wavetable oscillators per voice, a 4-pole low-pass filter,
4 envelopes, 3 LFOs and a 15-source modulation matrix.

ESQUE80 is an independent hobby project. It is not affiliated with, endorsed by or
supported by Ensoniq, E-MU, Creative Technology, Akai Professional or inMusic. All product
names are the property of their owners and are used only to describe what this plugin models.

## What you need

- A standalone MPC running **MPC OS 3.10.1 or later**.
- On the MPC: Preferences → Enhancements (Beta) → **Enable Enhancements** and
  **External Plugin Support**.

## Install

1. Unzip. You get a folder named `Lichthaus - VST - ESQUE80`.
2. Copy that folder into a folder named `VST2 Plugins` at the top level of a drive the MPC
   can run plugins from (the internal drive, or an SSD).
3. Restart the MPC. ESQUE80 appears under External Plugins.

Users of the Open MPC Plugin Manager can install it from the catalogue instead.

## Out of the box: stand-in waves

**This download contains no Ensoniq wave data.** Without it, ESQUE80 builds 75 stand-in
waves at start-up and offers 6 built-in presets plus 40 empty user slots. It plays and
every control works, but the stand-in waves are approximations, so it will not sound like
the original instrument.

## Using your own wave data (optional)

ESQUE80 can load wave data that you supply. None is distributed with it, and this project
does not provide it or point to a source for it. If you own the original instrument and have
made your own dump of its ROMs, convert the dump to the files below, put them in the plugin's
`Content/sq80/` folder and restart the MPC. Without them the stand-in waves are used.

### `wave0.bin`, `wave1.bin`, `wave2.bin`, `wave3.bin`

Raw 8-bit sample memory, exactly 65,536 bytes per file, no header. Together they form one
256 KB bank: `wave0.bin` at offset `0x00000`, `wave1.bin` at `0x10000`, `wave2.bin` at
`0x20000`, `wave3.bin` at `0x30000` (the two wave ROMs, then the two expansion sockets).
Samples are unsigned 8-bit; a zero byte ends a one-shot sample. A missing file leaves its
64 KB region silent.

### `waves.txt`

A text table that says, for each of the 75 waves, which sample table the oscillator plays in
each of 16 keyboard ranges (MIDI key number divided by 8). Blank lines and lines starting
with `#` are ignored.

```
W <wave 0-74>
K <range 0-15> <page> <reg> <semi> <fine>
```

Each `W` line is followed by 16 `K` lines, all values decimal:

| Field | Meaning |
| --- | --- |
| `page` | start of the sample table inside its 64 KB bank, in units of 256 bytes |
| `reg` | the oscillator chip's wavetable register: bit 7 = bank bit 0, bit 6 = bank bit 1, bits 5-3 = table size code (256 bytes x 2^code, so 256 to 32,768), bits 2-0 = resolution |
| `semi` | semitone offset of the sample's root pitch (may be negative) |
| `fine` | fine offset in 1/256 of a semitone, 0 to 255 |

A table starts at `bank x 65536 + page x 256`. All 75 x 16 = 1,200 `K` lines must be
present; an incomplete file is rejected and the stand-in waves are used instead. These
values come from the instrument's own operating-system ROM.

### `bankD.pcb`, `bankC.pcb` (optional)

Program banks: 102-byte program blocks, one after another, no header. A block is one program
exactly as the instrument sends it in a SysEx program dump once the nybble pairs are joined
back into bytes (see the SysEx appendix of the instrument's manual): name 6 bytes, four
envelopes of 10 bytes, three LFOs of 4 bytes, three oscillators of 10 bytes, 14 bytes of
voice settings. An "all programs" dump of your own instrument converts directly. Up to 128
programs per file; they appear in the MPC's preset list as `D01...` and `C01...`, after the
built-in presets (`P1...`) and before the user slots (`U01...`).

Please do not redistribute wave or program data with the plugin.

## The panel

Six function keys; press a key again to reach its sub-pages.

| Key | Pages |
| --- | --- |
| OSC | oscillators · OSC MOD (pitch mod routes) · DCA MOD (level mod routes) |
| ENV 1·2 | envelopes 1 and 2 · VEL 1·2 (velocity and keyboard scaling) |
| ENV 3·4 | envelopes 3 and 4 · VEL 3·4 |
| FILTER | cutoff, resonance, tracking, cutoff mod routes, glide |
| LFO | the three LFOs · LFO MOD (depth mod source, final level, reset, humanize) |
| MOD·TUNE | volume, program, pan and pan mod, voice switches |

- Mod sources and LFO waves are tap grids: tap a cell to pick it. The Q-Link knobs and the
  data wheel step through the same choices.
- The envelope displays are live and can be dragged. They show timing accurately; the first
  level (L1) is not drawn, and negative levels sit below mid-height.
- Each Q-Link bank of four follows one panel or block on screen.

## Known limits

- Developed and tested on one device, an **MPC Key 37 (Gen 2)**. The 32-bit ARM build is
  included but has not been run on hardware.
- The stand-in waves are approximate (see above).
- Split and layer settings in imported programs are ignored.
- The MPC keeps decoded panel artwork in memory until it is powered off; restarting the MPC
  app many times without a full power cycle can run it low on memory. This affects all
  plugins with custom panels.

## Download

Releases: https://github.com/lichthaus/mpc-vst-esque80/releases

- `ESQUE80-0.9.1-mpc-sdk.zip`: one folder for any MPC on MPC OS 3.10.1 or later. Copy it into `VST2 Plugins`.
- `ESQUE80-0.9.1-mpc-aarch64.zip` and `ESQUE80-0.9.1-mpc-armv7.zip`: the Open MPC catalogue's per-device packages,
  for its Plugin Manager and installers.

Tested on an MPC Key 37 (Gen 2). The 32-bit (armv7) build has not been run on hardware.

## Building from source

ESQUE80 is built with inMusic's MPC Plugin Builder SDK
(https://github.com/inmusic-software/mpc-plugin-builder-sdk).

```
git clone https://github.com/inmusic-software/mpc-plugin-builder-sdk
git clone https://github.com/lichthaus/mpc-vst-esque80 mpc-plugin-builder-sdk/plugins/ESQUE80
cd mpc-plugin-builder-sdk
python3 tools/build.py plugins/ESQUE80
```

The result is `plugins/ESQUE80/build/Lichthaus - VST - ESQUE80/`. The panel in `skins/` is generated by
`tools/make_skin.py` (needs Pillow, the SDK's cyan film-strip kit and `build/params.json` from a first build);
its header comment lists the paths it expects.

## Licence

ESQUE80 is released under the MIT licence (`LICENSE.txt`). It includes code under other
licences; see `THIRD_PARTY.txt`.
