#include "ui_builder.hpp"

#include "button.hpp"
#include "checkbox.hpp"
#include "flex_panel.hpp"
#include "gauge_dial.hpp"
#include "knob.hpp"
#include "label.hpp"
#include "list_box.hpp"
#include "logging.hpp"
#include "panel.hpp"
#include "progress_bar.hpp"
#include "radio_button.hpp"
#include "slider.hpp"
#include "text_input.hpp"
#include "toggle_switch.hpp"
#include "trend_line.hpp"
#include "widget.hpp"

namespace zb::ui
{
    // the shared color resolver (declared in html.hpp, defined below at
    // this scope so the header stays light for the ui_embed tool)
    bool parse_color(const std::string &s, core::Color &out);

    namespace
    {
        // --- tolerant value extraction (never throws) -------------------

        long long as_int(const prop_value &v, const long long fallback)
        {
            if (const auto *i = std::get_if<long long>(&v))
            {
                return *i;
            }
            return fallback;
        }
        bool as_bool(const prop_value &v)
        {
            if (const auto *b = std::get_if<bool>(&v))
            {
                return *b;
            }
            return false;
        }

        // whether the node declares the property at all (a present 0 is
        // not the same as absent -- explicit geometry, batch K / N8)
        bool has_prop(const ui_node &n, const char *name)
        {
            for (const auto &p : n.props)
            {
                if (p.first == name)
                {
                    return true;
                }
            }
            return false;
        }

        // prop `name`, or `fallback` when missing (kind must match the
        // builder that produced it; a wrong kind yields the fallback)
        template <class T>
        T prop_of(const ui_node &n, const char *name, const T &fallback)
        {
            for (const auto &[k, v] : n.props)
            {
                if (k != name)
                {
                    continue;
                }
                if constexpr (std::is_same_v<T, bool>)
                {
                    return as_bool(v);
                }
                if constexpr (std::is_same_v<T, long long>)
                {
                    return as_int(v, fallback);
                }
                if constexpr (std::is_same_v<T, std::string>)
                {
                    if (const auto *s = std::get_if<std::string>(&v))
                    {
                        return *s;
                    }
                    return fallback;
                }
            }
            return fallback;
        }

        // --- the tag table -----------------------------------------------

        // the string keys here are the ones a future designer-file
        // deserializer must emit; keep in sync with the builders in
        // ui_builder.hpp
        bool is_container_tag(const std::string &t)
        {
            return t == "column" || t == "row" || t == "panel";
        }

        // tag table -> concrete widget; the property table below can
        // static_cast safely because it only runs on widgets this
        // function made (no RTTI on NDS)
        std::unique_ptr<Widget> make_widget(const ui_node &n, bool *is_flex)
        {
            const std::string &t = n.type;
            *is_flex = (t == "column" || t == "row");
            if (t == "label")
            {
                return std::make_unique<Label>();
            }
            if (t == "button")
            {
                return std::make_unique<Button>();
            }
            if (t == "checkbox")
            {
                return std::make_unique<Checkbox>();
            }
            if (t == "radio")
            {
                return std::make_unique<RadioButton>();
            }
            if (t == "slider")
            {
                return std::make_unique<Slider>();
            }
            if (t == "progress_bar")
            {
                return std::make_unique<ProgressBar>();
            }
            if (t == "toggle")
            {
                return std::make_unique<ToggleSwitch>();
            }
            if (t == "gauge")
            {
                return std::make_unique<GaugeDial>();
            }
            if (t == "knob")
            {
                return std::make_unique<Knob>();
            }
            if (t == "trend")
            {
                return std::make_unique<TrendLine>();
            }
            if (t == "list_box")
            {
                return std::make_unique<ListBox>();
            }
            if (t == "text_input")
            {
                return std::make_unique<TextInput>();
            }
            if (t == "column" || t == "row")
            {
                return std::make_unique<FlexPanel>();
            }
            if (t == "panel")
            {
                return std::make_unique<Panel>();
            }
            LW << "ui_builder: unknown tag '" << t << "'; node skipped";
            return nullptr;
        }

        Checkbox *as_checkbox(Widget &w)
        {
            return static_cast<Checkbox *>(&w);
        }
        RadioButton *as_radio(Widget &w)
        {
            return static_cast<RadioButton *>(&w);
        }
        Slider *as_slider(Widget &w)
        {
            return static_cast<Slider *>(&w);
        }
        ProgressBar *as_progress_bar(Widget &w)
        {
            return static_cast<ProgressBar *>(&w);
        }
        ToggleSwitch *as_toggle(Widget &w)
        {
            return static_cast<ToggleSwitch *>(&w);
        }
        GaugeDial *as_gauge(Widget &w)
        {
            return static_cast<GaugeDial *>(&w);
        }
        Knob *as_knob(Widget &w)
        {
            return static_cast<Knob *>(&w);
        }
        TrendLine *as_trend(Widget &w)
        {
            return static_cast<TrendLine *>(&w);
        }
        ListBox *as_list(Widget &w)
        {
            return static_cast<ListBox *>(&w);
        }
        FlexPanel *as_flex(Widget &w)
        {
            return static_cast<FlexPanel *>(&w);
        }
        Panel *as_panel(Widget &w)
        {
            return static_cast<Panel *>(&w);
        }

        // the percent form of a geometry prop: "N%" -> N clamped into
        // 1..100 (0 = no percent declaration); any other value -> 0
        // (tolerated like every mistyped prop)
        int as_percent(const prop_value &v)
        {
            const auto *s = std::get_if<std::string>(&v);
            if (s == nullptr || s->size() < 2 || s->back() != '%')
            {
                return 0;
            }
            long long n = 0;
            for (std::size_t i = 0; i + 1 < s->size(); ++i)
            {
                const char c = (*s)[i];
                if (c < '0' || c > '9')
                {
                    return 0;
                }
                const int d = c - '0';
                // the same pre-multiply guard as ui_file.cpp parse_int:
                // without it an overlong digit run (n already past
                // (100-d)/10) overflows n*10+d before the clamp below
                if (n > (100 - d) / 10)
                {
                    return 100;  // an overlong run still clamps to 100
                }
                n = n * 10 + d;
            }
            return static_cast<int>(n);
        }

        // color value parsing lives at zb::ui scope below (parse_color,
        // shared by the background/color props and the HTML page box).

        // --- common properties (every widget) ---------------------------

        void apply_common(Widget &w, const ui_node &n)
        {
            if (!n.id.empty())
            {
                w.set_id(n.id);
            }
            // presence-gated: a declared 0 is an explicit value, not an
            // omission (batch K / N8). Percent values ("N%", batch L-4)
            // apply after the pixel form: set_size marks both axes
            // explicit, the declaration then clears its axis's
            // explicitness
            const bool has_width = has_prop(n, "width");
            const bool has_height = has_prop(n, "height");
            if (has_width || has_height)
            {
                if (has_width == has_height)
                {
                    // both declared (or neither leniently): two-axis set
                    w.set_size(static_cast<int>(prop_of(n, "width", 0LL)),
                               static_cast<int>(prop_of(n, "height", 0LL)));
                }
                else
                {
                    // a one-axis declaration keeps the widget's own other
                    // axis (its measure, e.g. a button's text height) --
                    // set_size marks both explicit, so clear the missing
                    // axis's flag right after
                    const int wp = static_cast<int>(prop_of(n, "width", 0LL));
                    const int hp = static_cast<int>(prop_of(n, "height", 0LL));
                    if (has_width)
                    {
                        w.set_size(wp, w.get_size().height);
                        w.set_height_auto(w.get_size().height);
                    }
                    else
                    {
                        w.set_size(w.get_size().width, hp);
                        w.set_width_auto(w.get_size().width);
                    }
                }
                const int w_pct = as_percent(prop_of(n, "width", std::string{}));
                const int h_pct = as_percent(prop_of(n, "height", std::string{}));
                if (w_pct > 0)
                {
                    w.set_width_percent(w_pct);
                }
                if (h_pct > 0)
                {
                    w.set_height_percent(h_pct);
                }
            }
            if (has_prop(n, "pos_x") || has_prop(n, "pos_y"))
            {
                w.set_position(static_cast<int>(prop_of(n, "pos_x", 0LL)),
                               static_cast<int>(prop_of(n, "pos_y", 0LL)));
            }
            const std::string text = prop_of(n, "text", std::string{});
            if (!text.empty())
            {
                w.set_text(text.c_str());
            }
            if (!prop_of(n, "visible", true))
            {
                w.set_visible(false);
            }
            core::Color c;
            if (parse_color(prop_of(n, "background", std::string{}), c))
            {
                w.set_background_color(c);
            }
            if (parse_color(prop_of(n, "color", std::string{}), c))
            {
                w.set_text_color(c);
            }
        }

        // --- control-specific properties --------------------------------

        void apply_control_props(Widget &w, const ui_node &n)
        {
            const std::string &t = n.type;
            if (t == "checkbox")
            {
                Checkbox &c = *as_checkbox(w);
                if (prop_of(n, "checked", false))
                {
                    c.set_checked(true);
                }
                return;
            }
            if (t == "radio")
            {
                RadioButton &r = *as_radio(w);
                r.set_group(static_cast<int>(prop_of(n, "group", 0LL)));
                if (prop_of(n, "checked", false))
                {
                    r.set_checked(true);
                }
                return;
            }
            if (t == "slider")
            {
                Slider &s = *as_slider(w);
                s.set_range(static_cast<int>(prop_of(n, "min", 0LL)),
                            static_cast<int>(prop_of(n, "max", 100LL)));
                s.set_step(static_cast<int>(prop_of(n, "step", 1LL)));
                return;
            }
            if (t == "progress_bar")
            {
                ProgressBar &p = *as_progress_bar(w);
                p.set_range(static_cast<int>(prop_of(n, "min", 0LL)),
                            static_cast<int>(prop_of(n, "max", 100LL)));
                p.set_value(static_cast<int>(prop_of(n, "value", 0LL)));
                return;
            }
            if (t == "toggle")
            {
                if (prop_of(n, "checked", false))
                {
                    as_toggle(w)->set_checked(true);
                }
                return;
            }
            if (t == "gauge")
            {
                GaugeDial &g = *as_gauge(w);
                g.set_range(static_cast<int>(prop_of(n, "min", 0LL)),
                            static_cast<int>(prop_of(n, "max", 100LL)));
                g.set_value(static_cast<int>(prop_of(n, "value", 0LL)));
                return;
            }
            if (t == "knob")
            {
                Knob &k = *as_knob(w);
                k.set_range(static_cast<int>(prop_of(n, "min", 0LL)),
                            static_cast<int>(prop_of(n, "max", 100LL)));
                k.set_step(static_cast<int>(prop_of(n, "step", 1LL)));
                k.set_value(static_cast<int>(prop_of(n, "value", 0LL)));
                return;
            }
            if (t == "list_box")
            {
                ListBox &l = *as_list(w);
                l.set_visible_rows(static_cast<size_t>(prop_of(n, "rows", 4LL)));
                if (!n.items.empty())
                {
                    l.set_items(n.items);
                }
                return;
            }
            if (t == "column" || t == "row")
            {
                FlexPanel &f = *as_flex(w);
                f.set_direction(t == "row" ? FlexPanel::flex_direction::row
                                           : FlexPanel::flex_direction::column);
                f.set_spacing(static_cast<int>(prop_of(n, "spacing", 0LL)));
                f.set_padding(static_cast<int>(prop_of(n, "padding", 0LL)));
                f.set_wrap(prop_of(n, "wrap", false));
            }
        }

        // --- materialization --------------------------------------------

        // materializes `n` into `container`; `container_is_flex` picks
        // the add_child signature
        void materialize(Widget &container, const bool container_is_flex, const ui_node &n)
        {
            bool is_flex = false;
            auto w = make_widget(n, &is_flex);
            if (w == nullptr)
            {
                return;
            }
            apply_common(*w, n);
            apply_control_props(*w, n);

            Widget *added = w.get();
            if (container_is_flex)
            {
                as_flex(container)->add_child(std::move(w), n.flex_grow);
            }
            else
            {
                as_panel(container)->add_child(std::move(w));
            }

            // a leaf tag cannot host children: recursing would static_cast
            // it to a container (no RTTI) and write through a bogus
            // pointer -- drop the children instead (contract: leaf
            // children are dropped with a warning)
            if (!n.children.empty() && !is_container_tag(n.type))
            {
                LW << "ui_builder: '" << n.type
                   << "' is not a container tag; its children are dropped";
            }
            else
            {
                for (const ui_node &c : n.children)
                {
                    materialize(*added, is_flex, c);
                }
            }
        }
    }  // namespace

    // --- shared color value parsing (B2 export) -----------------------
    // accepts "#rgb", "#rrggbb", the named subset, or "transparent";
    // malformed strings and "transparent" yield false (nothing set --
    // default background / theme text stays). Returns false also for
    // an empty string (absent prop). Names and "transparent" are ASCII
    // case-insensitive (B4, HTML semantics; hex digits already were).

    bool parse_color(const std::string &s, core::Color &out)
    {
        if (s.empty())
        {
            return false;
        }
        std::string name = s;
        for (char &c : name)
        {
            if (c >= 'A' && c <= 'Z')
            {
                c = static_cast<char>(c - 'A' + 'a');
            }
        }
        if (name == "transparent")
        {
            return false;
        }
        if (s[0] == '#')
        {
            auto hex = [](const char c) -> int {
                if (c >= '0' && c <= '9')
                {
                    return c - '0';
                }
                if (c >= 'a' && c <= 'f')
                {
                    return c - 'a' + 10;
                }
                if (c >= 'A' && c <= 'F')
                {
                    return c - 'A' + 10;
                }
                return -1;
            };
            const std::size_t len = s.size() - 1;
            if (len == 3)
            {
                const int r = hex(s[1]);
                const int g = hex(s[2]);
                const int b = hex(s[3]);
                if (r < 0 || g < 0 || b < 0)
                {
                    return false;
                }
                out = core::Color::from(r * 17, g * 17, b * 17);
                return true;
            }
            if (len == 6)
            {
                const int r0 = hex(s[1]);
                const int r1 = hex(s[2]);
                const int g0 = hex(s[3]);
                const int g1 = hex(s[4]);
                const int b0 = hex(s[5]);
                const int b1 = hex(s[6]);
                if (r0 < 0 || r1 < 0 || g0 < 0 || g1 < 0 ||
                    b0 < 0 || b1 < 0)
                {
                    return false;
                }
                out = core::Color::from(r0 * 16 + r1, g0 * 16 + g1,
                                        b0 * 16 + b1);
                return true;
            }
            return false;
        }
        if (name == "black")
        {
            out = core::colors::Black;
        }
        else if (name == "white")
        {
            out = core::colors::White;
        }
        else if (name == "red")
        {
            out = core::colors::Red;
        }
        else if (name == "green")
        {
            out = core::colors::Green;
        }
        else if (name == "blue")
        {
            out = core::colors::Blue;
        }
        else if (name == "yellow")
        {
            out = core::Color::from(255, 255, 0);
        }
        else if (name == "gray" || name == "grey")
        {
            out = core::Color::from(128, 128, 128);
        }
        else if (name == "cyan")
        {
            out = core::Color::from(0, 255, 255);
        }
        else if (name == "magenta")
        {
            out = core::Color::from(255, 0, 255);
        }
        else
        {
            return false;
        }
        return true;
    }

    Widget &build(Widget &host, const ui_node &root)
    {
        // the host is the real container: the root tag (panel/row/column)
        // is documentation; the root's children materialize into it
        if (host.is_flex_container())
        {
            FlexPanel &f = *as_flex(host);
            f.set_spacing(static_cast<int>(prop_of(root, "spacing", 0LL)));
            f.set_padding(static_cast<int>(prop_of(root, "padding", 0LL)));
            f.set_wrap(prop_of(root, "wrap", false));
        }
        else
        {
            Panel &p = *as_panel(host);
            p.set_spacing(static_cast<int>(prop_of(root, "spacing", 0LL)));
            p.set_padding(static_cast<int>(prop_of(root, "padding", 0LL)));
        }
        for (const ui_node &c : root.children)
        {
            materialize(host, host.is_flex_container(), c);
        }
        return host;
    }
}  // namespace zb::ui