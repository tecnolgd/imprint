#pragma once

#include <cstdint>

/*
 * Geometry scaling and accumulation with a 64-bit intermediate (X-9).
 *
 * An int x int product whose left operand derives from layout geometry --
 * an extent, an offset, a percentage share -- overflows long before the
 * quotient or the sum leaves the range the caller can use: a 100%
 * declaration times a degenerate extent wraps at 2^31, and the wrap is
 * silent (a negative size, a starved row). The discipline existed
 * locally (knob.cpp: "a wide range times 256 overflows int"); these
 * helpers make it systemic, so call sites keep their plain integer
 * expressions and the widening lives here, once.
 *
 * The failure mode is saturation, not wrapping: a value that cannot be
 * represented clamps to the int range, so a hostile or degenerate input
 * degrades to "as large (or as small) as possible" instead of flipping
 * sign. In-range inputs are bit-identical to the plain int expression
 * (truncating division, same rounding).
 */
namespace zb::ui::detail
{
    // clamps a 64-bit intermediate into the int range
    inline int clamp_int(const int64_t v)
    {
        if (v > INT32_MAX)
        {
            return INT32_MAX;
        }
        if (v < INT32_MIN)
        {
            return INT32_MIN;
        }
        return static_cast<int>(v);
    }

    /* value * pct / 100 -- the percent convention (widget geometry,
     * pseudo-element sides, gradient centers): truncating division, so a
     * negative value rounds toward zero */
    inline int pct(const int value, const int pct_of_value)
    {
        return clamp_int(static_cast<int64_t>(value) * pct_of_value / 100);
    }

    /* value * num / den -- a proportional share against a total (the
     * dialog button shrink, the scrollbar thumb, a percent line share).
     * den carries the 64-bit form so a total that itself grew past int
     * (a row of hostile widths) stays exact; den <= 0 is a caller bug and
     * returns 0, keeping the helper total instead of trapping on the
     * divide */
    inline int ratio(const int value, const int num, const int64_t den)
    {
        if (den <= 0)
        {
            return 0;
        }
        return clamp_int(static_cast<int64_t>(value) * num / den);
    }

    // a + b, saturating -- the geometry accumulation form (margins ride
    // around a box, a cursor advances, a row total grows)
    inline int add(const int a, const int b)
    {
        return clamp_int(static_cast<int64_t>(a) + b);
    }
}
