// X-9: the shared geometry-scaling helper (imui/src/scale.hpp).
//
// The bug class: an int x int product whose left operand derives from
// layout geometry wraps int silently long before the quotient would
// leave the range a caller can use. The audit found the form across
// flex_panel/widget/dialog/panel; the fix routes every site through one
// helper, and this suite pins the helper's arithmetic plus one
// end-to-end consumer (the flex percent resolver) so the class cannot
// come back through a "simplification" back to the raw expression.
#include "test.hpp"

#include <cstdint>
#include <memory>

#include "flex_panel.hpp"
#include "scale.hpp"
#include "widget.hpp"

using namespace zb::ui;

int test_scale()
{
    // in-range inputs are bit-identical to the plain int expression
    // (truncating division: a negative value rounds toward zero)
    {
        EXPECT(detail::pct(300, 50) == 150);
        EXPECT(detail::pct(0, 100) == 0);
        EXPECT(detail::pct(7, 33) == 2);
        EXPECT(detail::pct(-9, 50) == -4);
        EXPECT(detail::pct(-450, 100) == -450);
        EXPECT(detail::ratio(4000, 100, 300) == 1333);
        EXPECT(detail::ratio(100, 30, 100) == 30);
        EXPECT(detail::add(3, 4) == 7);
        EXPECT(detail::add(-3, -4) == -7);
        EXPECT(detail::clamp_int(5) == 5);
    }

    // the intermediate is 64-bit: products that wrap int32 stay exact
    {
        // 100 * INT32_MAX = 2.1e11: the pre-X-9 expression wrapped to -100
        EXPECT(detail::pct(INT32_MAX, 100) == INT32_MAX);
        EXPECT(detail::pct(25000000, 100) == 25000000);
        EXPECT(detail::pct(INT32_MAX, 1) == INT32_MAX / 100);
        EXPECT(detail::ratio(INT32_MAX, 100, 100) == INT32_MAX);
        EXPECT(detail::ratio(1000000000, 3, 2) == 1500000000);
        // 2.5e9: exact in the 64-bit sum, out of int range -> saturates
        EXPECT(detail::add(1500000000, 1000000000) == INT32_MAX);
    }

    // out-of-range results saturate: a degenerate input degrades to
    // "clamped", never to a sign flip
    {
        EXPECT(detail::pct(INT32_MAX, 200) == INT32_MAX);
        EXPECT(detail::pct(INT32_MIN, 200) == INT32_MIN);
        EXPECT(detail::ratio(INT32_MAX, 100, 1) == INT32_MAX);
        EXPECT(detail::ratio(INT32_MIN, 100, 1) == INT32_MIN);
        EXPECT(detail::add(INT32_MAX, 1) == INT32_MAX);
        EXPECT(detail::add(INT32_MIN, -1) == INT32_MIN);
        EXPECT(detail::add(INT32_MIN, INT32_MAX) == -1);
        EXPECT(detail::clamp_int(static_cast<int64_t>(INT32_MAX) + 1) == INT32_MAX);
        EXPECT(detail::clamp_int(static_cast<int64_t>(INT32_MIN) - 1) == INT32_MIN);
    }

    // degenerate denominators stay total (the callers guard them; the
    // helper returns 0 instead of trapping on the divide)
    {
        EXPECT(detail::ratio(100, 5, 0) == 0);
        EXPECT(detail::ratio(100, 5, -3) == 0);
    }

    // end-to-end wiring: a flex percent child resolves through the
    // helper. On a content box wider than INT32_MAX/100 the raw product
    // wrapped (the share came out negative and clamped to 0); the
    // declared 100% share must survive instead.
    {
        FlexPanel p;
        p.set_direction(FlexPanel::flex_direction::row);
        p.set_size(25000000, 40);
        auto child = std::make_unique<Widget>();
        child->set_size(10, 40);       // explicit cross size (height)
        child->set_width_percent(100); // the declaration replaces the width
        Widget *raw = child.get();
        p.add_child(std::move(child));
        p.layout();
        EXPECT(raw->get_size().width == 25000000);
    }

    return test::report("scale");
}
