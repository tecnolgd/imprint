#include "flex_panel.hpp"

#include <algorithm>
#include <utility>

namespace zb::ui
{
    namespace
    {
        // main axis = x for rows, y for columns
        bool is_row(const FlexPanel::flex_direction d)
        {
            return d == FlexPanel::flex_direction::row;
        }

        void set_main_size(Widget &w, const FlexPanel::flex_direction d, const int v)
        {
            // per-axis write: a grown main size must not clear an
            // explicit cross-axis size (set_size_auto clears both flags)
            if (is_row(d))
            {
                w.set_width_auto(v);
            }
            else
            {
                w.set_height_auto(v);
            }
        }

        // aspect-ratio derivation (H-5): an auto main axis with no percent
        // derives from the settled cross axis (explicit or percent); an
        // explicit size, a percent declaration, or an unsettled cross
        // axis keeps the old behavior. Pure function of widget state, so
        // packing and the write pass below always agree.
        bool aspect_derive_main(const Widget &w, const FlexPanel::flex_direction d,
                                int &out)
        {
            if (!w.has_aspect())
            {
                return false;
            }
            const bool main_is_w = is_row(d);
            const bool main_auto =
                main_is_w ? (!w.is_width_explicit() && !w.is_width_percent())
                          : (!w.is_height_explicit() && !w.is_height_percent());
            if (!main_auto)
            {
                return false;
            }
            const bool cross_settled =
                main_is_w ? (w.is_height_explicit() || w.is_height_percent())
                          : (w.is_width_explicit() || w.is_width_percent());
            if (!cross_settled)
            {
                return false;
            }
            const int cross = main_is_w ? w.get_size().height : w.get_size().width;
            if (cross <= 0)
            {
                return false;
            }
            const long long derived =
                static_cast<long long>(cross) *
                (main_is_w ? w.aspect_w() : w.aspect_h()) /
                (main_is_w ? w.aspect_h() : w.aspect_w());
            if (derived <= 0 || derived > 1000000)
            {
                return false;
            }
            out = static_cast<int>(derived);
            return true;
        }

        // the layout demand of a child along the main axis: aspect-ratio
        // derivation first, then an explicit set_size on that axis,
        // otherwise the widget's measure()
        int main_demand(const Widget &w, const FlexPanel::flex_direction d)
        {
            int aspect = 0;
            if (aspect_derive_main(w, d, aspect))
            {
                return aspect;
            }
            const bool own = is_row(d) ? w.is_width_explicit() : w.is_height_explicit();
            const auto demand = own ? w.get_size() : w.measure();
            return is_row(d) ? demand.width : demand.height;
        }

        // the layout demand of a child along the cross axis
        int cross_demand(const Widget &w, const FlexPanel::flex_direction d)
        {
            const bool own = is_row(d) ? w.is_height_explicit() : w.is_width_explicit();
            const auto demand = own ? w.get_size() : w.measure();
            return is_row(d) ? demand.height : demand.width;
        }

        // the child's final main/cross size (after materialization)
        int main_now(const Widget &w, const FlexPanel::flex_direction d)
        {
            return is_row(d) ? w.get_size().width : w.get_size().height;
        }

        int cross_now(const Widget &w, const FlexPanel::flex_direction d)
        {
            return is_row(d) ? w.get_size().height : w.get_size().width;
        }

        // the percentage declared on the child's main/cross axis
        // (0 = the axis is not a percentage, batch L-4)
        int main_percent(const Widget &w, const FlexPanel::flex_direction d)
        {
            return is_row(d) ? w.width_percent() : w.height_percent();
        }

        int cross_percent(const Widget &w, const FlexPanel::flex_direction d)
        {
            return is_row(d) ? w.height_percent() : w.width_percent();
        }

        // the main-axis demand a child brings to line packing: a percent
        // child participates with its declared share of the content box,
        // otherwise its fixed demand (explicit axis or measure)
        int main_desired(const Widget &w, const FlexPanel::flex_direction d,
                         const int content_main)
        {
            const int pct = main_percent(w, d);
            return pct > 0 ? pct * content_main / 100 : main_demand(w, d);
        }

        void set_cross_size(Widget &w, const FlexPanel::flex_direction d, const int v)
        {
            if (is_row(d))
            {
                w.set_height_auto(v);
            }
            else
            {
                w.set_width_auto(v);
            }
        }

        bool same_size(const core::imsize_t &a, const core::imsize_t &b)
        {
            return a.width == b.width && a.height == b.height;
        }

        bool same_pos(const core::impoint_t &a, const core::impoint_t &b)
        {
            return a.x == b.x && a.y == b.y;
        }

        void set_main_position(Widget &w, const FlexPanel::flex_direction d, const int main, const int cross)
        {
            if (is_row(d))
            {
                w.set_position(main, cross);
            }
            else
            {
                w.set_position(cross, main);
            }
        }
    }  // namespace

    std::unique_ptr<Widget> FlexPanel::remove_child(Widget *w)
    {
        for (auto it = items.begin(); it != items.end(); ++it)
        {
            if (it->child.get() == w)
            {
                std::unique_ptr<Widget> out = std::move(it->child);
                out->parent = nullptr;
                items.erase(it);
                mark_layout_dirty();
                return out;
            }
        }
        return nullptr;
    }

    void FlexPanel::clear_children()
    {
        for (const auto &item : items)
        {
            item.child->parent = nullptr;
        }
        items.clear();
        mark_layout_dirty();
    }

    core::imsize_t FlexPanel::measure() const
    {
        int main = 0;
        int cross = 0;
        // abs children (P-3) live outside the flow: zero demand on both
        // axes, not even spacing
        bool first = true;
        for (size_t i = 0; i < items.size(); ++i)
        {
            const Widget &child = *items[i].child;
            if (child.is_absolute())
            {
                continue;
            }
            // flex items and percent children contribute nothing on their
            // open axis: their size only exists relative to a resolved
            // parent size, which a measure() has no access to
            const int m = (items[i].flex_grow > 0 || main_percent(child, direction) > 0)
                              ? 0
                              : main_demand(child, direction);
            main += m + (first ? 0 : spacing);
            first = false;
            cross = std::max(cross, cross_percent(child, direction) > 0
                                         ? 0
                                         : cross_demand(child, direction));
        }
        const int pad = 2 * padding;
        if (is_row(direction))
        {
            return {main + pad, cross + pad};
        }
        return {cross + pad, main + pad};
    }

    void FlexPanel::layout()
    {
        // the layout owns all child geometry writes: report the whole
        // container area (children cannot move outside its bounds)
        mark_dirty();
        // convergent passes (H-9, contract §7): re-run the pass while a
        // child size, position, or measure changed, so an auto size fits
        // children whose inputs settle top-down in the same pass
        // (aspect-derived heights). Each level converges its own subtree
        // before returning, so 3 rounds cover arbitrary depth, and
        // derived-free trees settle after the first pass. The flag still
        // clears once, inside one layout() call.
        for (int round = 0; round < 3; ++round)
        {
            if (layout_pass())
            {
                break;
            }
        }
        clear_layout_dirty();
    }

    // one packing pass; true when nothing changed (settled). Every write
    // below records its delta but still calls the setter verbatim, so
    // damage/mark behavior is identical to the old single pass.
    bool FlexPanel::layout_pass()
    {
        bool changed = false;
        const auto &s = get_size();
        const int avail_main = (is_row(direction) ? s.width : s.height) - 2 * padding;
        const int avail_cross = (is_row(direction) ? s.height : s.width) - 2 * padding;

        // cross-axis percent sizes resolve first, against the content-box
        // cross size: they have no sibling interaction and do not depend
        // on the line packing (batch L-4). Abs children (P-3) resolve
        // later against their containing block, never here.
        for (const auto &item : items)
        {
            if (item.child->is_absolute())
            {
                continue;
            }
            const int pct = cross_percent(*item.child, direction);
            if (pct > 0)
            {
                const int resolved = std::max(0, pct * avail_cross / 100);
                changed |= (cross_now(*item.child, direction) != resolved);
                set_cross_size(*item.child, direction, resolved);
            }
        }

        // aspect-ratio write pass (H-5): derived main-axis sizes land here
        // so percent grandchildren below resolve against a real base;
        // packing further down re-derives the identical values through
        // main_demand (pure function, no drift between passes). Abs
        // children resolve in resolve_abs, never here.
        for (auto &item : items)
        {
            if (item.child->is_absolute())
            {
                continue;
            }
            int derived = 0;
            if (aspect_derive_main(*item.child, direction, derived))
            {
                changed |= (main_now(*item.child, direction) != derived);
                set_main_size(*item.child, direction, derived);
            }
        }

        // split the items into lines: a line breaks when the fixed demands
        // (plus spacing) exceed the available main-axis space; a percent
        // child participates with its desired share (the flex items absorb
        // leftover space, so they keep contributing 0). Abs children
        // (P-3) are outside the flow and never break lines.
        std::vector<std::vector<size_t>> lines;
        {
            std::vector<size_t> cur;
            int need = 0;
            for (size_t i = 0; i < items.size(); ++i)
            {
                if (items[i].child->is_absolute())
                {
                    continue;
                }
                const int pct = main_percent(*items[i].child, direction);
                const int item_need = (items[i].flex_grow > 0 && pct == 0)
                                          ? 0
                                          : main_desired(*items[i].child, direction, avail_main);
                if (wrap && !cur.empty() && need + spacing + item_need > avail_main)
                {
                    lines.push_back(std::move(cur));
                    cur.clear();
                    need = 0;
                }
                need += item_need + (cur.empty() ? 0 : spacing);
                cur.push_back(i);
            }
            if (!cur.empty())
            {
                lines.push_back(std::move(cur));
            }
        }
        if (lines.empty())
        {
            // no normal flow, but abs children still resolve (P-3)
            for (auto &item : items)
            {
                if (item.child->is_absolute())
                {
                    resolve_abs(*item.child, changed);
                }
            }
            return !changed;
        }

        // a line item claims main-axis space either as a flex grow
        // participant or as a fixed demand; a percent child is neither:
        // its size is resolved below and its grow weight is ignored
        const auto grows = [&](const size_t i) {
            return items[i].flex_grow > 0 && main_percent(*items[i].child, direction) == 0;
        };

        int cross_pos = padding;
        for (const auto &line : lines)
        {
            // resolve the percent children of the line first (batch L-4):
            // the fixed demands claim their space, each percent child
            // takes its declared share of the content box, and shares that
            // overflow the remainder are scaled into it proportionally to
            // their percentages -- the last percent child takes the
            // leftover pixel, so a scaled line sums exactly to the
            // remainder (the same exact-sum rule as the grow distribution)
            {
                int fixed = static_cast<int>(line.size() - 1) * spacing;
                int sum_desired = 0;
                for (const size_t i : line)
                {
                    const Widget &child = *items[i].child;
                    if (grows(i))
                    {
                        continue;  // absorbs leftover space, no fixed claim
                    }
                    const int want = main_desired(child, direction, avail_main);
                    if (main_percent(child, direction) > 0)
                    {
                        sum_desired += want;
                    }
                    else
                    {
                        fixed += want;
                    }
                }
                const int remaining = std::max(0, avail_main - fixed);
                if (sum_desired > remaining)
                {
                    int total_pct = 0;
                    int count = 0;
                    for (const size_t i : line)
                    {
                        const int pct = main_percent(*items[i].child, direction);
                        if (pct > 0)
                        {
                            total_pct += pct;
                            ++count;
                        }
                    }
                    int given = 0;
                    int seen = 0;
                    for (const size_t i : line)
                    {
                        const int pct = main_percent(*items[i].child, direction);
                        if (pct > 0)
                        {
                            ++seen;
                            const int share = (seen == count) ? remaining - given
                                                              : remaining * pct / total_pct;
                            const int clamped = std::max(0, share);
                            changed |= (main_now(*items[i].child, direction) != clamped);
                            set_main_size(*items[i].child, direction, clamped);
                            given += share;
                        }
                    }
                }
                else
                {
                    for (const size_t i : line)
                    {
                        Widget &child = *items[i].child;
                        if (main_percent(child, direction) > 0)
                        {
                            // shares floor at 0 like the overflow branch:
                            // a container smaller than its padding gives a
                            // negative content box and a raw percent would
                            // size the child negative
                            const int share =
                                std::max(0, main_desired(child, direction, avail_main));
                            changed |= (main_now(child, direction) != share);
                            set_main_size(child, direction, share);
                        }
                    }
                }
            }

            // give the flex items their share of the leftover space; integer
            // division drops the remainder, so the last flex item takes the
            // leftover pixels and the shares sum to exactly free
            int fixed = 0;
            int total_weight = 0;
            for (const size_t i : line)
            {
                if (grows(i))
                {
                    total_weight += items[i].flex_grow;
                }
                else
                {
                    const Widget &child = *items[i].child;
                    // a percent child's size was just resolved: its actual
                    // size is the fixed claim (not its desired share)
                    fixed += main_percent(child, direction) > 0
                                 ? main_now(child, direction)
                                 : main_demand(child, direction);
                }
            }
            fixed += static_cast<int>(line.size() - 1) * spacing;
            const int free = avail_main - fixed;

            int flex_count = 0;
            int flex_seen = 0;
            int distributed_size = 0;
            for (const size_t i : line)
            {
                if (grows(i))
                {
                    ++flex_count;
                }
            }
            for (const size_t i : line)
            {
                if (grows(i) && total_weight > 0)
                {
                    ++flex_seen;
                    const int grow = (flex_seen == flex_count)
                                         ? std::max(0, free) - distributed_size
                                         : std::max(0, free) * items[i].flex_grow / total_weight;
                    const int grown = std::max(0, grow);
                    changed |= (main_now(*items[i].child, direction) != grown);
                    set_main_size(*items[i].child, direction, grown);
                    distributed_size += grow;
                }
            }

            // materialize the sizes of auto-sized children (per axis) so
            // hit-testing works; an axis with an explicit size keeps both
            // its value and its flag; grown main sizes (set above) are
            // already written; percent axes were resolved above
            for (const size_t i : line)
            {
                Widget &child = *items[i].child;
                const bool row = is_row(direction);
                const bool main_explicit = row ? child.is_width_explicit() : child.is_height_explicit();
                const bool cross_explicit = row ? child.is_height_explicit() : child.is_width_explicit();
                if (!main_explicit && main_percent(child, direction) == 0
                    && items[i].flex_grow == 0)
                {
                    const int demand = main_demand(child, direction);
                    changed |= (main_now(child, direction) != demand);
                    if (row)
                    {
                        child.set_width_auto(demand);
                    }
                    else
                    {
                        child.set_height_auto(demand);
                    }
                }
                if (!cross_explicit && cross_percent(child, direction) == 0)
                {
                    const int cross = cross_demand(child, direction);
                    changed |= (cross_now(child, direction) != cross);
                    if (row)
                    {
                        child.set_height_auto(cross);
                    }
                    else
                    {
                        child.set_width_auto(cross);
                    }
                }
            }

            // place the line items along the main axis: justification
            // (H-7a, contract §flex) only moves the pen — sizes above are
            // final. Free space sits on top of spacing (the minimum gap);
            // F <= 0 or start keeps the padding origin. Closed forms over
            // settled sizes, so the H-9 loop sees no drift.
            int sum = 0;
            for (const size_t i : line)
            {
                sum += main_now(*items[i].child, direction);
            }
            const int n = static_cast<int>(line.size());
            const int slack = avail_main - sum - (n - 1) * spacing;
            int line_cross = 0;
            for (const size_t i : line)
            {
                line_cross = std::max(line_cross, cross_now(*items[i].child, direction));
            }

            // cross-axis alignment (H-7b, contract §flex): the effective
            // alignment is the item's override or the container default;
            // stretch grows auto-cross children to the line extent here
            // (explicit/percent axes keep their size and sit at the line
            // top, the CSS non-auto rule), every other mode only offsets
            // the placement below. Closed over settled demands, so the
            // H-9 loop sees no drift.
            const auto eff_align = [&](const size_t i) {
                switch (items[i].align_self)
                {
                    case self_align::start:
                        return align::start;
                    case self_align::center:
                        return align::center;
                    case self_align::end:
                        return align::end;
                    case self_align::stretch:
                        return align::stretch;
                    case self_align::auto_:
                    default:
                        return align_items;
                }
            };
            for (const size_t i : line)
            {
                if (eff_align(i) != align::stretch)
                {
                    continue;
                }
                Widget &child = *items[i].child;
                const bool row = is_row(direction);
                const bool cross_explicit =
                    row ? child.is_height_explicit() : child.is_width_explicit();
                if (cross_explicit || cross_percent(child, direction) > 0)
                {
                    continue;
                }
                changed |= (cross_now(child, direction) != line_cross);
                set_cross_size(child, direction, line_cross);
            }
            int lead = 0;
            if (slack > 0)
            {
                if (justify_content == justify::end)
                {
                    lead = free;
                }
                else if (justify_content == justify::center)
                {
                    lead = free / 2;
                }
            }
            int k = 0;
            int before = 0;
            for (const size_t i : line)
            {
                Widget &child = *items[i].child;
                int pos = padding + lead + before + k * spacing;                if (slack > 0)
                {
                    if (justify_content == justify::space_between && n > 1)
                    {
                        pos += k * slack / (n - 1);
                    }
                    else if (justify_content == justify::space_around)
                    {
                        pos += (2 * k + 1) * slack / (2 * n);
                    }
                }
                // a grandchild may grow the child's measure without moving
                // the child itself — snapshot all three so the next round
                // re-reads fresh demands (H-9)
                const auto size_before = child.get_size();
                const auto pos_before = child.get_position();
                const auto measure_before = child.measure();
                // H-7b: the child sits inside the line extent per its
                // effective alignment (line_cross bounds every cross size
                // above, so these offsets never go negative)
                const int c = cross_now(child, direction);
                int cross_off = 0;
                switch (eff_align(i))
                {
                    case align::center:
                        cross_off = (line_cross - c) / 2;
                        break;
                    case align::end:
                        cross_off = line_cross - c;
                        break;
                    case align::start:
                    case align::stretch:
                    default:
                        break;
                }
                set_main_position(child, direction, pos, cross_pos + cross_off);
                before += main_now(child, direction);
                ++k;
                child.layout();
                changed |= !same_size(size_before, child.get_size());
                changed |= !same_pos(pos_before, child.get_position());
                changed |= !same_size(measure_before, child.measure());
            }
            cross_pos += line_cross + spacing;
        }
        // absolute children resolve after the normal flow (P-3)
        for (auto &item : items)
        {
            if (item.child->is_absolute())
            {
                resolve_abs(*item.child, changed);
            }
        }
        return !changed;
    }

    // one abs child against its containing block (P-3, contract §7):
    // the anchor is the nearest positioned ancestor (this panel is the
    // common case); offsets resolve against the anchor content box in
    // this panel's coordinates. Sizes write per-axis so percent/explicit
    // declarations survive for the next pass (the measure-defaults-to-
    // size invariant keeps later passes stable, like the normal flow).
    void FlexPanel::resolve_abs(Widget &child, bool &changed)
    {
        static constexpr int16_t kUnset = INT16_MIN;
        const auto &s = get_size();
        int cbw = std::max(0, s.width - 2 * padding);
        int cbh = std::max(0, s.height - 2 * padding);
        int orgx = padding;
        int orgy = padding;
        if (const Widget *anchor = child.positioned_ancestor();
            anchor != nullptr && anchor != this)
        {
            const int apad = anchor->content_inset();
            const auto asz = anchor->get_size();
            cbw = std::max(0, asz.width - 2 * apad);
            cbh = std::max(0, asz.height - 2 * apad);
            const auto aa = anchor->get_absolute_position();
            const auto ta = get_absolute_position();
            orgx = aa.x - ta.x + apad;
            orgy = aa.y - ta.y + apad;
        }
        auto off_px = [&](const int side, const int base) {
            return child.abs_off_pct(side)
                       ? child.abs_off(side) * base / 100
                       : child.abs_off(side);
        };
        const bool has_l = child.abs_off(0) != kUnset;
        const bool has_t = child.abs_off(1) != kUnset;
        const bool has_r = child.abs_off(2) != kUnset;
        const bool has_b = child.abs_off(3) != kUnset;
        const int l = has_l ? off_px(0, cbw) : 0;
        const int t = has_t ? off_px(1, cbh) : 0;
        const int r = has_r ? off_px(2, cbw) : 0;
        const int b = has_b ? off_px(3, cbh) : 0;
        int w = 0;
        if (has_l && has_r && !child.is_width_explicit() &&
            !child.is_width_percent())
        {
            w = std::max(0, cbw - l - r);
        }
        else if (child.is_width_explicit())
        {
            w = child.get_size().width;
        }
        else if (child.is_width_percent())
        {
            w = child.width_percent() * cbw / 100;
        }
        else
        {
            w = std::max(0, child.measure().width);
        }
        int h = 0;
        if (has_t && has_b && !child.is_height_explicit() &&
            !child.is_height_percent())
        {
            h = std::max(0, cbh - t - b);
        }
        else if (child.is_height_explicit())
        {
            h = child.get_size().height;
        }
        else if (child.is_height_percent())
        {
            h = child.height_percent() * cbh / 100;
        }
        else
        {
            h = std::max(0, child.measure().height);
        }
        int x = orgx + (has_l ? l : (has_r ? cbw - r - w : 0));
        int y = orgy + (has_t ? t : (has_b ? cbh - b - h : 0));
        if (child.abs_tr(0) != kUnset)
        {
            x += child.abs_tr_pct(0) ? w * child.abs_tr(0) / 100
                                     : child.abs_tr(0);
        }
        if (child.abs_tr(1) != kUnset)
        {
            y += child.abs_tr_pct(1) ? h * child.abs_tr(1) / 100
                                     : child.abs_tr(1);
        }
        const auto size_before = child.get_size();
        const auto pos_before = child.get_position();
        const auto measure_before = child.measure();
        child.set_width_auto(w);
        child.set_height_auto(h);
        child.set_position(x, y);
        child.layout();
        changed |= !same_size(size_before, child.get_size());
        changed |= !same_pos(pos_before, child.get_position());
        changed |= !same_size(measure_before, child.measure());
    }

    void FlexPanel::draw_at(core::Graphics &area) const
    {
        for (const auto &item : items)
        {
            item.child->draw(area);
        }
    }

    Widget *FlexPanel::pick(const int x, const int y)
    {
        // children drawn later are on top, so search in reverse order
        for (auto it = items.rbegin(); it != items.rend(); ++it)
        {
            Widget &child = *it->child;
            const auto p = child.get_position();
            if (child.hit(x - p.x, y - p.y))
            {
                if (auto *inner = child.pick(x - p.x, y - p.y))
                {
                    return inner;
                }
                return &child;
            }
        }
        return nullptr;
    }
}  // namespace zb::ui
