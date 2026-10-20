#!/usr/bin/env python3
"""X-11: the Python host's framebuffer decode must match the framework.

Run from the repo root:  python3 demo/python/test_framebuffer.py

The ground truth is the FRAMEWORK's own packing, not a hand-written table:
pack_words() prints the pixel word each RGB triple becomes through
Color::from(), using the build's real pixel traits. The test compiles and
runs that probe for the 16bpp configuration (abgr1555) and compares the
host's decode against it at the 5-bit precision the format carries.

Why this exists: myapp.py decoded the 16bpp buffer as RGB565 while the
contract (zbapi.h, core/pixel_traits.hpp) is abgr1555, so red and blue
were swapped in that host and nothing caught it -- python-host is Tier-3
and never ran a 16bpp build. A decode that is only "plausible" is exactly
what this pins down.

Needs a C++ compiler (g++ or clang++) and the repo's imcore headers; no
built library, no pygame, no display.
"""

import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)

from framebuffer import abgr1555_to_rgba, bgra_to_rgba  # noqa: E402

# RGB triples that exercise every channel, the extremes, and values that
# are not 5-bit multiples (so a truncating decode cannot pass by luck)
CASES = [
    (255, 0, 0), (0, 255, 0), (0, 0, 255),
    (255, 255, 255), (0, 0, 0),
    (49, 94, 34), (13, 73, 31), (128, 128, 128), (17, 200, 99),
]

PROBE = r"""
#include <cstdio>
#include "core/color.hpp"
int main()
{
    using zb::ui::core::Color;
    const int cases[][3] = {
%s
    };
    for (const auto &c : cases)
    {
        const Color px = Color::from(static_cast<unsigned char>(c[0]),
                                     static_cast<unsigned char>(c[1]),
                                     static_cast<unsigned char>(c[2]));
        std::printf("%%d %%d %%d %%u %%d %%d %%d\n", c[0], c[1], c[2],
                    static_cast<unsigned>(px.pixel), px.r(), px.g(), px.b());
    }
    return 0;
}
"""


def pack_words():
    """[(rgb, word, r, g, b)] from the framework's own 16bpp packing."""
    cxx = shutil.which("g++") or shutil.which("clang++")
    if not cxx:
        raise SystemExit("test_framebuffer: no C++ compiler (g++ / clang++)")
    rows = ",\n".join("        {%d, %d, %d}" % c for c in CASES)
    with tempfile.TemporaryDirectory() as tmp:
        src = os.path.join(tmp, "pack.cpp")
        exe = os.path.join(tmp, "pack")
        with open(src, "w") as f:
            f.write(PROBE % rows)
        subprocess.run(
            [cxx, "-std=c++17", "-DCOLOR_DEPTH=16", "-DRGB_MODEL=abgr",
             "-DENDIAN=1555", "-I" + os.path.join(ROOT, "imcore", "include"),
             src, "-o", exe],
            check=True)
        out = subprocess.run([exe], check=True, capture_output=True,
                             text=True).stdout
    result = []
    for line in out.strip().splitlines():
        r, g, b, word, rr, gg, bb = (int(v) for v in line.split())
        result.append(((r, g, b), word, (rr, gg, bb)))
    return result


def five_bit(v):
    return v >> 3


def decode_word(decoder, word):
    """Run a host decoder on one little-endian 16bpp word."""
    raw = bytes((word & 0xFF, (word >> 8) & 0xFF))
    rgba = decoder(raw, len(raw))
    return (rgba[0], rgba[1], rgba[2])


def main():
    failures = 0
    packed = pack_words()
    if len(packed) != len(CASES):
        print("FAIL: probe returned %d rows, expected %d"
              % (len(packed), len(CASES)))
        return 1

    # 1. placement: the host's decode inverts the framework's packing at
    #    the 5-bit precision the format carries
    for rgb_in, word, (r, g, b) in packed:
        want = (five_bit(r), five_bit(g), five_bit(b))
        got = tuple(five_bit(v) for v in decode_word(abgr1555_to_rgba, word))
        if got != want:
            failures += 1
            print("FAIL abgr1555 %s word=%d: decoded %s, framework reads %s"
                  % (rgb_in, word, got, want))

    # 2. the alpha bit is not a colour: word 0x8000 is opaque black
    #    (the RGB565 decode read it as red 131)
    got = decode_word(abgr1555_to_rgba, 0x8000)
    if got != (0, 0, 0):
        failures += 1
        print("FAIL abgr1555 alpha bit leaked into a channel: 0x8000 -> %s" % (got,))

    # 3. expansion keeps both ends exact (a plain <<3 caps at 248)
    full = decode_word(abgr1555_to_rgba, 0x7FFF)
    if full != (255, 255, 255):
        failures += 1
        print("FAIL abgr1555 full-scale channel should reach 255: %s" % (full,))

    # 4. the 32bpp path still swaps b/r and pins alpha opaque
    rgba = bgra_to_rgba(bytes((10, 20, 30, 0)), 4)
    if tuple(rgba) != (30, 20, 10, 255):
        failures += 1
        print("FAIL bgra_to_rgba: %s, expected (30, 20, 10, 255)" % (tuple(rgba),))

    # 5. the old RGB565 decode must NOT pass -- a regression to it has to
    #    fail loudly, which is the whole point of the check
    def rgb565(data, length):
        import struct as _s
        n = length // 2
        words = _s.unpack_from("<%dH" % n, bytes(data[:length]))
        out = bytearray(n * 4)
        for i, v in enumerate(words):
            out[i * 4 + 0] = ((v >> 11) & 0x1F) * 255 // 31
            out[i * 4 + 1] = ((v >> 5) & 0x3F) * 255 // 63
            out[i * 4 + 2] = (v & 0x1F) * 255 // 31
            out[i * 4 + 3] = 0xFF
        return bytes(out)

    agree = 0
    for rgb_in, word, (r, g, b) in packed:
        want = (five_bit(r), five_bit(g), five_bit(b))
        got = tuple(five_bit(v) for v in decode_word(rgb565, word))
        agree += (got == want)
    if agree == len(packed):
        failures += 1
        print("FAIL: the RGB565 decode matches the framework too -- the "
              "test cannot tell the two layouts apart")

    if failures:
        print("[framebuffer] FAILED (%d failures)" % failures)
        return 1
    print("[framebuffer] PASSED (0 failures) -- abgr1555 placement, alpha "
          "bit, full-scale expansion, bgra path, RGB565 rejected")
    return 0


if __name__ == "__main__":
    sys.exit(main())
