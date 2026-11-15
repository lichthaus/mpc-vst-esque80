# Third-party sources and notices

ESQUE80's own code is MIT licensed (see LICENSE). It builds on the following.

## MAME es5503 (BSD-3-Clause)

The per-sample oscillator algorithm is taken from MAME's `es5503.cpp` and reduced to the
modes this synthesizer uses.

    license: BSD-3-Clause
    copyright-holders: R. Belmont

    Redistribution and use in source and binary forms, with or without modification, are
    permitted provided that the following conditions are met:

    1. Redistributions of source code must retain the above copyright notice, this list of
       conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright notice, this list
       of conditions and the following disclaimer in the documentation and/or other
       materials provided with the distribution.
    3. Neither the name of the copyright holder nor the names of its contributors may be
       used to endorse or promote products derived from this software without specific
       prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
    EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
    MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
    THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
    SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
    OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
    HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
    TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
    SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

## MPC Plugin Builder SDK (MIT)

The plugin is built with inMusic's MPC Plugin Builder SDK, whose framework is compiled into
the binary. Copyright (c) 2026 inMusic Brands, Inc. Released under the MIT licence, the same
terms as LICENSE.

## SQ8L (reference only, no code included)

The pitch, filter, amplifier and modulation behaviour was re-implemented from reading the
sources of SQ8L (github.com/sq8l-revival/SQ8L), which are published for preservation with no
licence granted. No SQ8L code is included in ESQUE80.

## Wave ROM, wave table and factory banks (not included)

The 256 KB wave ROM, the wave table and the factory sound banks of the original instrument
belong to their copyright holders. They are not part of ESQUE80 and are not distributed with
it. `tools/sq8l_to_card.py` only reformats files that you supply, for your own use.

## Reference documents

Rainer Buchty's "Inside SQ80" and the SQ-80 Musician's Manual.

## Trademarks

Ensoniq and SQ-80 are trademarks of their respective owners. Akai and MPC are trademarks of
inMusic Brands, Inc. "VST" is a trademark of Steinberg Media Technologies GmbH. ESQUE80 is
not affiliated with or endorsed by any of them.
