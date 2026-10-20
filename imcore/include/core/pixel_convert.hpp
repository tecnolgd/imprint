#pragma once

#include <cstddef>
#include <cstdint>

#include "color.hpp"

namespace zb::ui::core
{
    /*
     * Presentation seam (A-1): the panel formats the presentation edge
     * can convert the internal buffer into. The kernel renders exactly
     * one internal format per build; a new panel format adds a value
     * here plus a convert_row branch -- never a kernel COLOR_DEPTH
     * matrix entry (docs/ARCHITECTURE.md 4.4).
     */
    enum class panel_format
    {
        native,  // the build's internal layout: a byte copy
        bgr565,  // 16-bit RGB565 words, little-endian byte order
    };

    /* bytes per pixel of the panel format; 0 for an unknown value */
    size_t panel_pixel_bytes(const panel_format f);

    /* one colour channel's placement inside the pixel word */
    struct channel_bits
    {
        int shift = 0;  // bit offset of the channel from the word's LSB
        int bits = 0;   // channel width in bits
    };

    /* the three colour channels of the build's internal format */
    struct native_layout
    {
        channel_bits r{};
        channel_bits g{};
        channel_bits b{};
    };

    /*
     * X-10: channel placement of the build's internal format, measured
     * from the pixel word instead of assumed from the host byte order.
     *
     * A presentation edge may take the byte-copy fast path (`native`) only
     * when the panel's own channel descriptors equal these. An equal
     * bytes-per-pixel is NOT evidence of an equal layout: the fb shell
     * memcpy'd a same-width panel with a different channel order straight
     * into the framebuffer (docs/code-contract.md 3, presentation seam).
     *
     * Measured, not declared: the 32bpp traits place their channels at
     * memory byte offsets, so the byte->bit mapping depends on the host's
     * byte order, while the 16bpp traits declare bit shifts directly.
     * Probing one single-channel `Color` per channel reads the effective
     * placement in both cases without a host-endianness assumption.
     * Alpha is excluded on purpose: the 4th 32bpp byte is not part of the
     * buffer contract and a panel may declare no alpha at all.
     */
    native_layout native_panel_layout();

    /*
     * Converts one row of `count` internal colors into panel bytes.
     * Returns the number of bytes written; 0 (and nothing written) when
     * the destination is shorter than count * panel_pixel_bytes(f), the
     * format is unknown, or the inputs are invalid. Silent rejection:
     * the caller owns the warning (docs/code-contract.md 3).
     */
    size_t convert_row(const panel_format f, const Color *src, const int count,
                       uint8_t *dst, const size_t cap);
}
