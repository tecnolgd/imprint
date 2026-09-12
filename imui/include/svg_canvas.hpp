#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "widget.hpp"

namespace zb::ui
{
    /*
     * Vector-dial canvas: the HTML `svg`/`vectordial` subset as a
     * display-only widget (docs/html-path.md §SVG subset). It holds
     * viewBox-unit strokes (lines through draw_line_aa) and baseline
     * texts (through the widget text seam, provider fallback included)
     * and maps the viewBox onto its bounds by stretch.
     *
     * Deliberately narrow: no paths, no fills, no stroke widths (1px),
     * no aspect preservation. Display-only like GaugeDial: not
     * focusable, no events, plain-rect hit.
     */
    class SvgCanvas : public Widget
    {
    public:
        struct Line
        {
            int x1 = 0, y1 = 0, x2 = 0, y2 = 0;  // viewBox units
            core::Color color{};                 // alpha carries opacity
        };
        struct Text
        {
            std::u16string text;
            int x = 0, y = 0;  // viewBox units; y is the baseline
            core::Color color{};
            bool has_color = false;  // unset = theme text at draw time
            int anchor = 0;          // 0 = start, 1 = middle, 2 = end
        };

        SvgCanvas() = default;

        // viewBox mapping; w/h <= 0 means pixel units (coordinates map 1:1)
        void set_view_box(int x, int y, int w, int h);
        [[nodiscard]] int view_x() const { return vb_x_; }
        [[nodiscard]] int view_y() const { return vb_y_; }
        [[nodiscard]] int view_w() const { return vb_w_; }
        [[nodiscard]] int view_h() const { return vb_h_; }

        void add_line(const Line &l);
        void add_text(const Text &t);
        void clear_vectors();
        [[nodiscard]] const std::vector<Line> &lines() const { return lines_; }
        [[nodiscard]] const std::vector<Text> &texts() const { return texts_; }

        // natural size: the viewBox in pixels, 64x64 without one
        [[nodiscard]] core::imsize_t measure() const override;

    protected:
        void draw_at(core::Graphics &area) const override;

    private:
        [[nodiscard]] int map_x(int vx) const;
        [[nodiscard]] int map_y(int vy) const;

        int vb_x_ = 0, vb_y_ = 0, vb_w_ = 0, vb_h_ = 0;
        std::vector<Line> lines_;
        std::vector<Text> texts_;
    };
}  // namespace zb::ui
