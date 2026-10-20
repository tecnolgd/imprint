"""Framebuffer decoding for the Python host (zbapi C ABI).

Pure byte math, no pygame: the host's frame_rgba() calls these, and
demo/python/test_framebuffer.py checks them against the framework's own
pixel traits without needing a display or a built library.

The formats are the ones zbapi.h promises:

  COLOR_DEPTH=32 -> 4 bytes/pixel, bgra (ZB_FORMAT_BGRA8)
  COLOR_DEPTH=16 -> 2 bytes/pixel, abgr1555 (ZB_FORMAT_ABGR1555)

"abgr1555" is the word XBBBBBGGGGGRRRRR (core/pixel_traits.hpp,
im_pixel_traits_abgr_1555: r_shift 0, g_shift 5, b_shift 10, a_shift 15).
Red sits in the LOW five bits. It is NOT RGB565 -- decoding it that way
swaps red and blue and lets the alpha bit leak into the red channel.
"""

import struct

# 5-bit -> 8-bit expansion by bit replication: v << 3 | v >> 2 keeps both
# ends exact (0 -> 0, 31 -> 255), which is what a display wants. The
# framework's own accessors read bits << 3 (0..248); both agree at the
# 5-bit level, which is the precision the format actually carries.
_R5 = tuple(((v << 3) | (v >> 2)) for v in range(32))


def bgra_to_rgba(data, length):
    """32bpp framebuffer (B,G,R,A bytes) -> rgba bytes.

    The alpha byte is NOT part of the buffer contract (it is padding) --
    pinned to opaque so the per-pixel-alpha blit can never make pixels
    transparent (borders drawn with colors::Black used to vanish).
    """
    buf = bytes(data[:length])
    out = bytearray(buf)
    out[0::4] = buf[2::4]
    out[2::4] = buf[0::4]
    out[3::4] = b"\xff" * (length // 4)
    return bytes(out)


def abgr1555_to_rgba(data, length):
    """16bpp framebuffer (abgr1555 little-endian words) -> rgba bytes.

    Red = bits 0-4, green = bits 5-9, blue = bits 10-14, alpha = bit 15.
    The single alpha bit is a presence flag, not opacity: pygame
    composites the surface, so the byte is pinned opaque (the bgra path
    does the same for the 4th byte).
    """
    n = length // 2
    words = struct.unpack_from("<%dH" % n, bytes(data[:length]))
    out = bytearray(n * 4)
    for i, v in enumerate(words):
        o = i * 4
        out[o + 0] = _R5[v & 0x1F]
        out[o + 1] = _R5[(v >> 5) & 0x1F]
        out[o + 2] = _R5[(v >> 10) & 0x1F]
        out[o + 3] = 0xFF
    return bytes(out)
