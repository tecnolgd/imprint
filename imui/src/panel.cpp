#include "panel.hpp"

#include <utility>

namespace zb::ui
{
    std::unique_ptr<Widget> Panel::remove_child(Widget *w)
    {
        for (auto it = children.begin(); it != children.end(); ++it)
        {
            if (it->get() == w)
            {
                std::unique_ptr<Widget> out = std::move(*it);
                out->parent = nullptr;
                children.erase(it);
                mark_layout_dirty();
                return out;
            }
        }
        return nullptr;
    }

    void Panel::clear_children()
    {
        for (const auto &child : children)
        {
            child->parent = nullptr;
        }
        children.clear();
        mark_layout_dirty();
    }

    void Panel::layout()
    {
        // the layout owns all child geometry writes: report the whole
        // container area (children cannot move outside its bounds)
        mark_dirty();
        auto pos = padding;
        for (auto &child : children)
        {
            // in-flow margins ride around the border box (H-3)
            if (orientation::vertical == orient)
            {
                child->set_position(padding + child->margin_left(),
                                    pos + child->margin_top());
                pos += child->margin_top() + child->get_size().height +
                       child->margin_bottom() + spacing;
            }
            else
            {
                child->set_position(pos + child->margin_left(), padding + child->margin_top());
                pos += child->margin_left() + child->get_size().width +
                       child->margin_right() + spacing;
            }
            child->layout();
        }
        clear_layout_dirty();
    }

    void Panel::draw_at(core::Graphics &area) const
    {
        for (const auto &child : children)
        {
            child->draw(area);
        }
    }

    Widget *Panel::pick(const int x, const int y)
    {
        // children drawn later are on top, so search in reverse order
        for (auto it = children.rbegin(); it != children.rend(); ++it)
        {
            Widget &child = **it;
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
}
