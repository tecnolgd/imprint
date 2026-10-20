#include "test.hpp"

#include <cstring>
#include <vector>

#include "core/pixel_convert.hpp"

using namespace zb::ui;

// A-1 presentation seam: the row converter is the only place the
// internal buffer becomes a panel format (contract: code-contract S3)
int test_pixel_convert()
{
    // per-format pixel width
    EXPECT(core::panel_pixel_bytes(core::panel_format::native) == sizeof(core::Color));
    EXPECT(core::panel_pixel_bytes(core::panel_format::bgr565) == 2);

    // native: a byte-exact copy of the internal row
    {
        core::Color row[3] = {core::Color::from(1, 2, 3),
                              core::Color::from(250, 100, 50),
                              core::colors::White};
        std::vector<uint8_t> buf(sizeof(row), 0);
        EXPECT(core::convert_row(core::panel_format::native, row, 3, buf.data(), buf.size()) == sizeof(row));
        EXPECT(0 == std::memcmp(row, buf.data(), sizeof(row)));
    }

    // bgr565: pure channels pack to the textbook 565 words
    {
        core::Color row[3] = {core::Color::from(255, 0, 0),
                              core::Color::from(0, 255, 0),
                              core::Color::from(0, 0, 255)};
        uint8_t buf[6] = {0};
        EXPECT(core::convert_row(core::panel_format::bgr565, row, 3, buf, sizeof(buf)) == 6);
        // red 0xF800, green 0x07E0, blue 0x001F; little-endian bytes
        EXPECT(buf[0] == 0x00 && buf[1] == 0xF8);
        EXPECT(buf[2] == 0xE0 && buf[3] == 0x07);
        EXPECT(buf[4] == 0x1F && buf[5] == 0x00);
    }

    // bgr565: a mid gray keeps its per-channel quantized value
    {
        core::Color row[1] = {core::Color::from(128, 128, 128)};
        uint8_t buf[2] = {0};
        EXPECT(core::convert_row(core::panel_format::bgr565, row, 1, buf, sizeof(buf)) == 2);
        const int px = buf[0] | (buf[1] << 8);
        EXPECT((px >> 11) == (128 >> 3));
        // green is 6-bit in the panel word: truncated from the 8-bit
        // channel at 32bpp; replicated from the 5-bit internal channel
        // at 16bpp (128 -> g5 16 -> 33). Pure channels saturate at both
        // depths, which is why the textbook words above hold everywhere
        if constexpr (core::ImColor_Depth == 16)
            EXPECT(((px >> 5) & 0x3F) == 33);
        else
            EXPECT(((px >> 5) & 0x3F) == (128 >> 2));
        EXPECT((px & 0x1F) == (128 >> 3));
    }

    // silent rejection: short capacity or zero count writes nothing
    {
        core::Color row[2] = {core::colors::White, core::colors::Black};
        uint8_t buf[2] = {0xAA, 0xAA};
        EXPECT(core::convert_row(core::panel_format::bgr565, row, 2, buf, 3) == 0);
        EXPECT(buf[0] == 0xAA && buf[1] == 0xAA);  // untouched
        EXPECT(core::convert_row(core::panel_format::bgr565, row, 0, buf, sizeof(buf)) == 0);
        EXPECT(core::convert_row(core::panel_format::bgr565, nullptr, 2, buf, sizeof(buf)) == 0);
    }

    // X-10: the fast-path predicate reports the build's real channel
    // placement, so a presentation edge can refuse a same-width panel with
    // a different order. Measured from the pixel word, so this holds for
    // the 32bpp byte-offset layouts (host-byte-order dependent) and the
    // 16bpp bit-shift layout alike.
    {
        const core::native_layout l = core::native_panel_layout();

        // every channel is stored, and the three placements are disjoint
        // (a shared bit would mean the probe cannot tell channels apart)
        for (const core::channel_bits *c : {&l.r, &l.g, &l.b})
        {
            EXPECT(c->bits > 0);
            EXPECT(c->shift >= 0);
            EXPECT(c->shift + c->bits <= core::ImColor_Depth);
        }
        EXPECT(l.r.shift != l.g.shift && l.g.shift != l.b.shift && l.r.shift != l.b.shift);
        EXPECT(l.r.bits + l.g.bits + l.b.bits <= core::ImColor_Depth);

        // 16bpp is the one format whose placement is declared as shifts
        // in the traits: lock the probe against that declaration. The
        // branch is selected through a template parameter so the 32bpp
        // traits (which have no *_shift members) never instantiate it.
        const auto lock_against_traits = [&l](auto traits_tag)
        {
            using T = decltype(traits_tag);
            if constexpr (T::depth == 16)
            {
                EXPECT(l.r.shift == T::r_shift && l.r.bits == 5);
                EXPECT(l.g.shift == T::g_shift && l.g.bits == 5);
                EXPECT(l.b.shift == T::b_shift && l.b.bits == 5);
            }
            else
            {
                // 32bpp: eight bits per channel
                EXPECT(l.r.bits == 8 && l.g.bits == 8 && l.b.bits == 8);
            }
        };
        lock_against_traits(core::ColorTraits{});

        // the descriptor agrees with the accessors it was measured from:
        // a pure channel reads back full at the probed placement and zero
        // elsewhere, which is what a panel comparison relies on. "Full"
        // is the depth's own maximum (255 at 32bpp, 248 at 16bpp -- the
        // 5-bit channel reads expanded), not 255.
        const core::Color red = core::Color::from(255, 0, 0);
        const uint8_t full = static_cast<uint8_t>(core::Color::from(255, 0, 0).r());
        EXPECT(full == (core::ImColor_Depth == 32 ? 255 : 248));
        EXPECT(red.r() == full && red.g() == 0 && red.b() == 0);
        // mask to the channel's own width: at 16bpp a 0xFF mask would
        // reach the alpha bit (shift 15) from the blue descriptor
        const auto channel_of = [](const unsigned word, const core::channel_bits &c)
        {
            return (word >> c.shift) & ((1u << c.bits) - 1u);
        };
        const unsigned word = static_cast<unsigned>(red.pixel);
        EXPECT(channel_of(word, l.r) != 0);
        EXPECT(channel_of(word, l.g) == 0);
        EXPECT(channel_of(word, l.b) == 0);
    }

    return test::report("pixel_convert");
}
