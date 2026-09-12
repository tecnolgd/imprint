#include "svg_canvas.hpp"

#include <cstdint>

#include "theme.hpp"

#include "theme.hpp"

namespace zb::ui
{
    void SvgCanvas::set_view_box(const int x, const int y, const int w, const int h)
    {
        mark_dirty();
        vb_x_ = x;
        vb_y_ = y;
        vb_w_ = w;
        vb_h_ = h;
        mark_dirty();
        mark_layout_dirty();  // measure() reads the viewBox
    }

    void SvgCanvas::add_line(const Line &l)
    {
        lines_.push_back(l);
        mark_dirty();
    }

    void SvgCanvas::add_text(const Text &t)
    {
        texts_.push_back(t);
        mark_dirty();
    }

    void SvgCanvas::clear_vectors()
    {
        lines_.clear();
        texts_.clear();
        mark_dirty();
    }

    core::imsize_t SvgCanvas::measure() const
    {
        if (vb_w_ > 0 && vb_h_ > 0)
        {
            return {vb_w_, vb_h_};
        }
        return {64, 64};
    }

    int SvgCanvas::map_x(const int vx) const
    {
        if (vb_w_ <= 0)
        {
            return vx;
        }
        const auto s = get_size();
        // truncation toward zero; viewBox authors keep coordinates
        // non-negative, matching the parser contract
        return static_cast<int>(
            (static_cast<int64_t>(vx - vb_x_) * s.width) / vb_w_);
    }

    int SvgCanvas::map_y(const int vy) const
    {
        if (vb_h_ <= 0)
        {
            return vy;
        }
        const auto s = get_size();
        return static_cast<int>(
            (static_cast<int64_t>(vy - vb_y_) * s.height) / vb_h_);
    }

    void SvgCanvas::draw_at(core::Graphics &area) const
    {
        for (const Line &l : lines_)
        {
            area.draw_line_aa(map_x(l.x1), map_y(l.y1),
                              map_x(l.x2), map_y(l.y2), l.color);
        }
        for (const Text &t : texts_)
        {
            if (t.text.empty())
            {
                continue;
            }
            const int adv = advance_of(t.text.data(),
                                       static_cast<int>(t.text.size()));
            int pen = map_x(t.x);
            if (t.anchor == 1)
            {
                pen -= adv / 2;
            }
            else if (t.anchor == 2)
            {
                pen -= adv;
            }
            // themed items read theme().text live (no widget color is
            // consulted: svg text never inherits, per the subset contract)
            draw_text_at(area, t.text.data(),
                         static_cast<int>(t.text.size()),
                         pen, map_y(t.y),
                         t.has_color ? t.color : theme().text);
        }
    }
}  // namespace zb::ui
