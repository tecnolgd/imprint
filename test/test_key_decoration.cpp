// X-15: the shared U-1 key-decoration rule. The printable-character half
// of "every mapped key carries the same platform-independent code on both
// edges" was written out in each translator (win WM_CHAR, x11 KeyPress,
// mac characters) and re-derived by the wasm and python hosts — six copies
// of the same letter/digit classification. decorate_printable_key is now
// the single home; this suite pins it so a copy cannot drift back.
#include "test.hpp"

#include "input.hpp"

namespace
{
    zb::input::input_event key_down_of(const int c)
    {
        zb::input::input_event ev;
        ev.type = zb::input::input_type::key_down;
        zb::input::decorate_printable_key(ev, c);
        return ev;
    }

    zb::input::input_event key_up_of(const int c)
    {
        zb::input::input_event ev;
        ev.type = zb::input::input_type::key_up;
        zb::input::decorate_printable_key(ev, c);
        return ev;
    }
}

int test_key_decoration()
{
    // letters: key_down carries the lowercase code AND ch (shift applied
    // by the platform, so 'W' is key='w' ch='W')
    {
        const zb::input::input_event w = key_down_of('W');
        EXPECT(w.key == 'w');
        EXPECT(w.ch == 'W');
        const zb::input::input_event a = key_down_of('a');
        EXPECT(a.key == 'a');
        EXPECT(a.ch == 'a');
    }

    // digits: verbatim key and ch
    {
        const zb::input::input_event d = key_down_of('7');
        EXPECT(d.key == '7');
        EXPECT(d.ch == '7');
    }

    // key_up: the same code, ch always 0 (the pairing rule a hold-to-walk
    // host relies on)
    {
        const zb::input::input_event w = key_up_of('W');
        EXPECT(w.key == 'w');
        EXPECT(w.ch == 0);
        const zb::input::input_event d = key_up_of('7');
        EXPECT(d.key == '7');
        EXPECT(d.ch == 0);
    }

    // punctuation: ch is set on key_down, key is NOT — it stays
    // layout-specific and the translator's navigation table wins
    {
        const zb::input::input_event comma = key_down_of(',');
        EXPECT(comma.key == 0);
        EXPECT(comma.ch == ',');
        const zb::input::input_event bang = key_down_of('!');
        EXPECT(bang.key == 0);
        EXPECT(bang.ch == '!');
    }

    // space is a navigation key, never a character (B1): the rule declines
    // it and leaves the event untouched
    {
        const zb::input::input_event sp = key_down_of(' ');
        EXPECT(sp.key == 0);
        EXPECT(sp.ch == 0);
        // a translator that already routed space as a navigation key
        // keeps that key: the rule only declines to touch it
        zb::input::input_event sp_nav;
        sp_nav.type = zb::input::input_type::key_down;
        sp_nav.key = static_cast<int>(zb::input::key_code::space);
        EXPECT(!zb::input::decorate_printable_key(sp_nav, ' '));
        EXPECT(sp_nav.key == static_cast<int>(zb::input::key_code::space));
    }

    // control bytes and non-ASCII are outside the printable class
    {
        EXPECT(key_down_of(0x1b).key == 0);
        EXPECT(key_down_of(0x1b).ch == 0);
        EXPECT(key_down_of(0x7f).key == 0);
        EXPECT(key_down_of(0x7f).ch == 0);
        EXPECT(key_down_of(0xe9).key == 0);
        EXPECT(key_down_of(0xe9).ch == 0);
    }

    // the return value reports the mapped classes, so a translator can
    // distinguish "handled as a character" from "not a printable"
    {
        zb::input::input_event ev;
        ev.type = zb::input::input_type::key_down;
        EXPECT(zb::input::decorate_printable_key(ev, 'q'));
        ev = zb::input::input_event{};
        ev.type = zb::input::input_type::key_down;
        EXPECT(!zb::input::decorate_printable_key(ev, '/'));
        ev = zb::input::input_event{};
        ev.type = zb::input::input_type::key_down;
        EXPECT(!zb::input::decorate_printable_key(ev, ' '));
    }

    return test::report("key_decoration");
}
