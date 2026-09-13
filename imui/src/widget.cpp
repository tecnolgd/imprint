#include "widget.hpp"

#include <algorithm>

#include "text/utf8.hpp"

namespace zb::ui
{
    namespace
    {
        // process-level shared fallback provider (batch J6): constructed
        // at the first widget construction, then leaked so it outlives
        // every widget, including static-storage ones destroyed after
        // main. Safe because BitmapProvider is stateless.
        const zb::SharedPtr<BitmapProvider> &fallback_singleton()
        {
            static zb::SharedPtr<BitmapProvider> *p =
                new zb::SharedPtr<BitmapProvider>(zb::make_shared<BitmapProvider>());
            return *p;
        }

        // process-wide default glyph provider (code-contract 2.4, plan 2);
        // leaked like the fallback so static-lifetime widgets never touch
        // a dead pointer
        zb::SharedPtr<GlyphProvider> &default_provider_state()
        {
            static zb::SharedPtr<GlyphProvider> *p = new zb::SharedPtr<GlyphProvider>();
            return *p;
        }
    }  // namespace

    void set_default_glyph_provider(const zb::SharedPtr<GlyphProvider> &provider)
    {
        default_provider_state() = provider;
    }

    const zb::SharedPtr<GlyphProvider> &default_glyph_provider()
    {
        return default_provider_state();
    }

    Widget::Widget() : bitmap_fallback_(fallback_singleton())
    {
    }

    void Widget::set_text(const char *text)
    {
        mark_dirty();
        advance_cache_ = -1;
        mark_layout_dirty();
        text_.clear();
        if (nullptr == text)
        {
            return;
        }
        text_ = utf8_to_utf16(text);
    }

    void Widget::draw(core::Graphics &g) const
    {
        if (!visible || size.width <= 0 || size.height <= 0)
        {
            return;
        }

        // damage culling: outside the reported region the whole subtree
        // is by definition invisible, skip the rasterizer entirely
        if (g.damage_on())
        {
            const auto abs = get_absolute_position();
            if (!g.damage_intersects(abs.x, abs.y, size.width, size.height))
            {
                return;
            }
        }

        // clip_safe restricts g's draw area to this widget and restores it
        // on scope exit; no allocation per widget per frame
        auto area = g.clip_safe(position.x, position.y, size.width, size.height);
        if (!area)
        {
            return;
        }

        draw_background(g);
        draw_at(g);
    }

    void Widget::draw_background(core::Graphics &area) const
    {
        const auto s = get_size();
        // corner radius shared by the background and the border (P-1):
        // px clamps to half the smaller side, the half form resolves
        // 50% at draw time
        int radius = 0;
        if (dress_.radius_kind != 0)
        {
            const int half_min = std::min(s.width, s.height) / 2;
            radius = dress_.radius_kind == 2
                         ? half_min
                         : std::min<int>(dress_.radius_px, half_min);
        }
        // a dressed color below opaque paints blended; on 16bpp
        // (binary alpha) the enable is a visual no-op — alpha_blend
        // returns front for any set bit — so one rule covers both
        auto needs_blend = [](const core::Color &c) { return c.a() < 255; };
        bool blend = background.has_value() && needs_blend(*background);
        if (dress_.bg_kind != 0 &&
            (needs_blend(dress_.bg_from) || needs_blend(dress_.bg_to)))
        {
            blend = true;
        }
        const bool bak = area.is_alpha_enabled();
        if (blend)
        {
            area.enable_alpha(true);
        }
        if (background.has_value())
        {
            // S-1: through fill_rect (not fill) so a wireframe view
            // degrades the face to an outline; pixel-identical to fill()
            // in FULL mode. The dialog mask keeps fill() (immune).
            if (radius > 0)
            {
                area.fill_round_rect_aa(0, 0, s.width - 1, s.height - 1, radius,
                                        *background);
            }
            else
            {
                area.fill_rect(0, 0, s.width - 1, s.height - 1, *background);
            }
        }
        // radial wins when both kinds are set (contract P-1; the
        // builder sets exactly one, this is direct-setter misuse)
        if (dress_.bg_kind == 2)
        {
            area.fill_radial(0, 0, s.width - 1, s.height - 1,
                             s.width * dress_.bg_ax / 100,
                             s.height * dress_.bg_ay / 100, dress_.bg_from,
                             dress_.bg_p0, dress_.bg_to, dress_.bg_p1, radius);
        }
        else if (dress_.bg_kind == 1)
        {
            area.fill_gradient(0, 0, s.width - 1, s.height - 1, dress_.bg_from,
                               dress_.bg_to, dress_.bg_ax != 0, radius);
        }
        if (background_image.has_value())
        {
            area.draw_image(*background_image, 0, 0);
        }
        // border-box outline over the background; a default (alpha-0)
        // color never reaches here through the builder (parse_color
        // rejects transparent), the guard is for direct app misuse
        for (int i = 0; i < dress_.border_w; ++i)
        {
            if (dress_.border_color.a() == 0)
            {
                break;
            }
            if (radius > 0)
            {
                area.draw_round_rect_aa(i, i, s.width - 1 - i, s.height - 1 - i,
                                        radius - i < 0 ? 0 : radius - i,
                                        dress_.border_color);
            }
            else
            {
                area.draw_rect(i, i, s.width - 1 - i, s.height - 1 - i,
                               dress_.border_color);
            }
        }
        if (blend)
        {
            area.enable_alpha(bak);
        }
    }

    const GlyphProvider *Widget::primary_provider() const
    {
        if (primary_provider_ != nullptr)
        {
            return primary_provider_.get();
        }
        return default_glyph_provider().get();
    }

    void Widget::draw_text(core::Graphics &area) const
    {
        if (text_.empty())
        {
            return;
        }

        const GlyphProvider *const primary = primary_provider();
        const GlyphProvider *const fallback = bitmap_fallback_.get();

        // pick the provider for a code unit; nullptr means "not covered"
        const auto pick = [&](const char16_t ch) -> const GlyphProvider *
        {
            if (primary != nullptr && primary->covers(ch))
            {
                return primary;
            }
            if (fallback->covers(ch))
            {
                return fallback;
            }
            return nullptr;
        };

            const auto s = get_size();
    const char16_t *const data = text_.data();
    const int len = static_cast<int>(text_.size());

    // total advance: split into covered runs; uncovered units add 0
    const int total = text_advance();

        // line metrics come from the primary provider, or the fallback;
        // line_metrics does not scan the string (no per-glyph loads)
        const text_metrics m = primary != nullptr
                                   ? primary->line_metrics()
                                   : fallback->line_metrics();

        int x = 0;
        switch (halign)
        {
        case h_align::left:
            x = 0;
            break;
        case h_align::center:
            x = (s.width - total) / 2;
            break;
        case h_align::right:
            x = s.width - total;
            break;
        }

        int y = 0;  // baseline of the first (only) line
        switch (valign)
        {
        case v_align::top:
            y = m.ascent;
            break;
        case v_align::center:
            y = (s.height - m.height) / 2 + m.ascent;
            break;
        case v_align::bottom:
            y = s.height - m.height + m.ascent;
            break;
        }

        x += text_offset_.x;
        y += text_offset_.y;

        draw_text_at(area, data, len, x, y);
    }

    bool Widget::hit(const int x, const int y) const
    {
        return visible && x >= 0 && y >= 0 && x < size.width && y < size.height;
    }

    Widget *Widget::find_by_id(const std::string &id)
    {
        if (get_id() == id)
        {
            return this;
        }
        for (size_t i = 0; i < child_count(); ++i)
        {
            if (auto *c = child_at(i))
            {
                if (const auto hit = c->find_by_id(id))
                {
                    return hit;
                }
            }
        }
        return nullptr;
    }

    int Widget::text_advance() const
    {
        if (advance_cache_ >= 0)
        {
            return advance_cache_;
        }
        advance_cache_ = advance_of(text_.data(), static_cast<int>(text_.size()));
        return advance_cache_;
    }

    int Widget::text_ascent() const
    {
        const GlyphProvider *const primary = primary_provider();
        const GlyphProvider *const fallback = bitmap_fallback_.get();
        const text_metrics m = primary != nullptr
                                   ? primary->line_metrics()
                                   : fallback->line_metrics();
        return m.ascent;
    }

    int Widget::advance_of(const char16_t *data, const int len) const
    {
        const GlyphProvider *const primary = primary_provider();
        const GlyphProvider *const fallback = bitmap_fallback_.get();

        // pick the provider for a code unit; nullptr means "not covered"
        const auto pick = [&](const char16_t ch) -> const GlyphProvider *
        {
            if (primary != nullptr && primary->covers(ch))
            {
                return primary;
            }
            if (fallback->covers(ch))
            {
                return fallback;
            }
            return nullptr;
        };

        if (len <= 0)
        {
            return 0;
        }
        int total = 0;
        for (int i = 0; i < len;)
        {
            const GlyphProvider *cur = pick(data[i]);
            int j = i;
            while (j < len && pick(data[j]) == cur)
            {
                ++j;
            }
            if (cur != nullptr)
            {
                total += cur->measure(data + i, j - i).width;
            }
            i = j;
        }
        return total;
    }

    void Widget::draw_text_at(core::Graphics &area, const char16_t *data, const int len,
                              const int x, const int y) const
    {
        draw_text_at(area, data, len, x, y, effective_text_color());
    }

    void Widget::draw_text_at(core::Graphics &area, const char16_t *data, const int len,
                              const int x, const int y, const core::Color &color) const
    {
        const GlyphProvider *const primary = primary_provider();
        const GlyphProvider *const fallback = bitmap_fallback_.get();

        const auto pick = [&](const char16_t ch) -> const GlyphProvider *
        {
            if (primary != nullptr && primary->covers(ch))
            {
                return primary;
            }
            if (fallback->covers(ch))
            {
                return fallback;
            }
            return nullptr;
        };

        // draw run by run; uncovered units keep the pen position
        int pen = 0;
        for (int i = 0; i < len;)
        {
            const GlyphProvider *cur = pick(data[i]);
            int j = i;
            while (j < len && pick(data[j]) == cur)
            {
                ++j;
            }
            if (cur != nullptr)
            {
                cur->write(area, data + i, j - i, x + pen, y, color);
                pen += cur->measure(data + i, j - i).width;
            }
            i = j;
        }
    }

    int Widget::text_height() const
    {
        const GlyphProvider *const primary = primary_provider();
        const GlyphProvider *const fallback = bitmap_fallback_.get();
        // line metrics come from the primary provider, or the fallback;
        // line_metrics does not scan the string (no per-glyph loads)
        const text_metrics m = primary != nullptr
                                   ? primary->line_metrics()
                                   : fallback->line_metrics();
        return m.height;
    }

    bool Widget::is_descendant_of(const Widget *ancestor) const
    {
        for (const Widget *w = this; w != nullptr; w = w->parent)
        {
            if (w == ancestor)
            {
                return true;
            }
        }
        return false;
    }

    void Widget::walk_damage(int *out_l, int *out_t, int *out_r, int *out_b)
    {
        take_dirty(out_l, out_t, out_r, out_b);
        // consume at read time: damage reported after this point (a
        // setter firing during the draw) is not wiped by the paint pass
        // and survives into the next frame
        clear_dirty();
        bool pending = false;
        for (size_t i = 0; i < child_count(); ++i)
        {
            if (auto *c = child_at(i))
            {
                c->walk_damage(out_l, out_t, out_r, out_b);
                pending = pending || c->subtree_dirty_;
            }
        }
        subtree_dirty_ = pending;
    }

    void Widget::walk_clear_damage()
    {
        clear_dirty();
        subtree_dirty_ = false;
        for (size_t i = 0; i < child_count(); ++i)
        {
            if (auto *c = child_at(i))
            {
                c->walk_clear_damage();
            }
        }
    }
}
