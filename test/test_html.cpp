#include "test.hpp"

#include "html.hpp"
#include "imui.hpp"

using namespace zb::ui;

namespace
{
    // index of the first prop named `name`, or -1
    int find_prop(const ui_node &n, const char *name)
    {
        for (std::size_t i = 0; i < n.props.size(); ++i)
        {
            if (n.props[i].first == name)
            {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    // the value of the first prop named `name`; a missing prop records a
    // failure and returns a static monostate instead of indexing [-1]
    const prop_value &node_prop_v(const ui_node &n, const char *name)
    {
        static const prop_value kNone{};
        const int i = find_prop(n, name);
        if (i >= 0)
        {
            return n.props[i].second;
        }
        ++test::failures;
        std::printf("FAIL (node_prop_v): missing prop '%s'\n", name);
        return kNone;
    }
}  // namespace

int test_html()
{
    // single top-level container becomes the document root; the CSS gap
    // lands as the .ui spacing prop on the root
    {
        bool ok = false;
        ui_node root = parse_html(
            "<body>\n"
            "  <div style=\"gap:4\">\n"
            "    <button id=\"go\">GO</button>\n"
            "    <label>Status</label>\n"
            "  </div>\n"
            "</body>\n",
            &ok);
        EXPECT(ok);
        EXPECT(root.type == "column");
        EXPECT(root.children.size() == 2);
        EXPECT(test::vget<long long>(node_prop_v(root, "spacing")) == 4);
        EXPECT(root.children[0].type == "button");
        EXPECT(root.children[0].id == "go");
        EXPECT(test::vget<std::string>(node_prop_v(root.children[0], "text")) == "GO");
        EXPECT(root.children[1].type == "label");
        EXPECT(test::vget<std::string>(node_prop_v(root.children[1], "text")) == "Status");
    }

    // multiple top-level widgets -> pseudo root
    {
        ui_node r = parse_html("<body><label>a</label><button>b</button></body>", nullptr);
        EXPECT(r.type == "root");
        EXPECT(r.children.size() == 2);
        EXPECT(r.children[0].type == "label");
        EXPECT(r.children[1].type == "button");

        // a single non-container top-level widget stays under the root
        ui_node r2 = parse_html("<body><button>x</button></body>", nullptr);
        EXPECT(r2.type == "root");
        EXPECT(r2.children.size() == 1);
        EXPECT(r2.children[0].type == "button");
    }

    // text normalization: whitespace collapse + entity subset; unknown
    // entities stay literal
    {
        ui_node r = parse_html(
            "<label>  A &amp;&lt;B&gt;&quot;C&#39;  </label>", nullptr);
        EXPECT(r.children.size() == 1);
        EXPECT(test::vget<std::string>(node_prop_v(r.children[0], "text")) ==
               "A &<B>\"C'");

        ui_node r2 = parse_html("<label>R &copy; C</label>", nullptr);
        EXPECT(test::vget<std::string>(node_prop_v(r2.children[0], "text")) ==
               "R &copy; C");
    }

    // an off-whitelist element is skipped and drops its whole subtree;
    // checked + text on buttons/labels parse
    {
        ui_node r = parse_html(
            "<label>a</label>\n"
            "<widget><button>x</button></widget>\n"
            "<checkbox checked>On</checkbox>\n",
            nullptr);
        EXPECT(r.type == "root");
        EXPECT(r.children.size() == 2);
        EXPECT(r.children[0].type == "label");
        EXPECT(r.children[1].type == "checkbox");
        EXPECT(test::vget<bool>(node_prop_v(r.children[1], "checked")) == true);
        EXPECT(test::vget<std::string>(node_prop_v(r.children[1], "text")) == "On");
    }

    // C2: br is void -- bare, slashed, and spaced forms are equivalent;
    // a stray </br> is ignored (and, having no open frame, follows the
    // documented stray-close recovery)
    {
        const char *spellings[] = {
            "<div>a<br>b</div>",
            "<div>a<br/>b</div>",
            "<div>a<br />b</div>",
        };
        for (const char *doc : spellings)
        {
            ui_node r = parse_html(doc, nullptr);
            EXPECT(r.type == "column");
            EXPECT(r.children.size() == 3);
            EXPECT(r.children[0].type == "label");
            EXPECT(test::vget<std::string>(
                       node_prop_v(r.children[0], "text")) == "a");
            EXPECT(r.children[1].type == "label");
            EXPECT(test::vget<long long>(
                       node_prop_v(r.children[1], "height")) == 7);
            EXPECT(find_prop(r.children[1], "text") < 0);  // empty spacer
            EXPECT(test::vget<std::string>(
                       node_prop_v(r.children[2], "text")) == "b");
        }

        // <br></br>: the close is stray (void br opens no frame), so it
        // closes the div early per the recovery rule; "b" lands at root
        {
            ui_node r = parse_html("<div>a<br></br>b</div>\n", nullptr);
            EXPECT(r.type == "root");
            EXPECT(r.children.size() == 2);
            EXPECT(r.children[0].type == "column");
            EXPECT(r.children[0].children.size() == 2);
            EXPECT(test::vget<std::string>(
                       node_prop_v(r.children[0].children[0], "text")) == "a");
            EXPECT(test::vget<long long>(
                       node_prop_v(r.children[0].children[1], "height")) == 7);
            EXPECT(test::vget<std::string>(
                       node_prop_v(r.children[1], "text")) == "b");
        }

        ui_node r = parse_html("<div>a</br>b</div>\n", nullptr);
        EXPECT(r.type == "root");  // the stray close ends the div early
        EXPECT(r.children.size() == 2);
        EXPECT(r.children[0].type == "column");
        EXPECT(test::vget<std::string>(
                   node_prop_v(r.children[0].children[0], "text")) == "a");
        EXPECT(test::vget<std::string>(
                   node_prop_v(r.children[1], "text")) == "b");
    }

    // style rules: #id beats tag, inline beats #id, document order within
    // a bucket, unknown properties are ignored
    {
        bool ok = false;
        ui_node r = parse_html(
            "<style>\n"
            "  label { color: red; border-right: 2px; }\n"
            "  #hot { color: green; background-color: #0000ff; }\n"
            "  #hot2 { color: green; }\n"
            "</style>\n"
            "<label id=\"hot\" style=\"color:yellow\">H</label>\n"
            "<label id=\"hot2\">N</label>\n"
            "<label>P</label>\n",
            &ok);
        EXPECT(ok);
        EXPECT(r.children.size() == 3);

        const auto &hot = r.children[0];
        EXPECT(test::vget<std::string>(node_prop_v(hot, "color")) == "yellow");
        EXPECT(test::vget<std::string>(node_prop_v(hot, "background")) == "#0000ff");
        EXPECT(find_prop(hot, "border-right") < 0);  // ignored, not stored

        const auto &hot2 = r.children[1];
        EXPECT(test::vget<std::string>(node_prop_v(hot2, "color")) == "green");

        const auto &plain = r.children[2];
        EXPECT(test::vget<std::string>(node_prop_v(plain, "color")) == "red");

        // same-property document order within the id bucket: last wins
        ui_node r2 = parse_html(
            "<style>#x { height:10px; } #x { height:20px; }</style>\n"
            "<label id=\"x\">X</label>\n",
            nullptr);
        EXPECT(test::vget<long long>(node_prop_v(r2.children[0], "height")) == 20);
    }

    // B5: !important lifts above every normal declaration; the bucket
    // order still applies inside the important tier
    {
        ui_node r = parse_html(
            "<style>\n"
            "  label { color: red !important; background-color: #111111; }\n"
            "  #h { color: green; }\n"
            "</style>\n"
            "<label id=\"h\" style=\"color: yellow\">A</label>\n"
            "<label id=\"h\" style=\"color: yellow !IMPORTANT\">B</label>\n"
            "<label>C</label>\n"
            "<label style=\"border: 1px !important\">D</label>\n"
            "<label style=\"color: blue ! important\">E</label>\n",
            nullptr);
        EXPECT(r.children.size() == 5);

        const auto &a = r.children[0];  // important tag > normal inline + id
        EXPECT(test::vget<std::string>(node_prop_v(a, "color")) == "red");
        EXPECT(test::vget<std::string>(node_prop_v(a, "background")) == "#111111");

        const auto &b = r.children[1];  // important inline > important tag
        EXPECT(test::vget<std::string>(node_prop_v(b, "color")) == "yellow");

        const auto &c = r.children[2];  // important tag applies plainly
        EXPECT(test::vget<std::string>(node_prop_v(c, "color")) == "red");

        const auto &d = r.children[3];  // unknown stays ignored, flag or not
        EXPECT(find_prop(d, "border") < 0);

        const auto &e = r.children[4];  // spaced marker tolerated
        EXPECT(test::vget<std::string>(node_prop_v(e, "color")) == "blue");
    }
    // container props and flex direction; display:none hides a column
    {
        ui_node r = parse_html(
            "<div style=\"flex-direction: row; gap: 3; padding: 2; flex-wrap: wrap\">\n"
            "  <div style=\"flex: 1\"><label>x</label></div>\n"
            "</div>\n",
            nullptr);
        EXPECT(r.type == "row");
        EXPECT(test::vget<long long>(node_prop_v(r, "spacing")) == 3);
        EXPECT(test::vget<long long>(node_prop_v(r, "padding")) == 2);
        EXPECT(test::vget<bool>(node_prop_v(r, "wrap")) == true);
        EXPECT(r.children[0].flex_grow == 1);

        ui_node r2 = parse_html(
            "<div style=\"display: none\"><label>x</label></div>\n", nullptr);
        EXPECT(r2.type == "column");  // single container unwraps to the root
        EXPECT(r2.children.size() == 1);
        EXPECT(r2.children[0].type == "label");
        EXPECT(test::vget<bool>(node_prop_v(r2, "visible")) == false);
    }

    // B4: CSS keyword values are ASCII case-insensitive; ids are not
    {
        ui_node r = parse_html(
            "<div style=\"flex-direction: ROW; gap: 3; flex-wrap: WRAP\">\n"
            "  <label>x</label>\n"
            "</div>\n",
            nullptr);
        EXPECT(r.type == "row");
        EXPECT(test::vget<bool>(node_prop_v(r, "wrap")) == true);

        ui_node r2 = parse_html(
            "<div style=\"display: NONE\"><label>x</label></div>\n", nullptr);
        EXPECT(r2.type == "column");
        EXPECT(test::vget<bool>(node_prop_v(r2, "visible")) == false);

        ui_node r3 = parse_html(
            "<label style=\"width: 120PX; height: AUTO\">w</label>\n", nullptr);
        EXPECT(test::vget<long long>(node_prop_v(r3.children[0], "width")) == 120);
        EXPECT(find_prop(r3.children[0], "height") < 0);
    }

    // B4 end-to-end: named colors resolve case-insensitively through the
    // shared color parser (uppercase TRANSPARENT stays a no-op). Note:
    // label text is element *content* in HTML, and size comes from the
    // style (width=/height= are not element attributes).
    {
        ui_node root = parse_html(
            "<label id=\"c\" style=\"width: 60px; height: 20px; color: RED; "
            "background-color: BLUE\">Hi</label>\n"
            "<label id=\"t\" style=\"width: 60px; height: 20px; "
            "background-color: TRANSPARENT\">x</label>\n",
            nullptr);
        FlexPanel host;
        host.set_size(200, 60);
        build(host, root);
        host.layout();
        auto *c = static_cast<Label *>(host.find_by_id("c"));
        auto *t = static_cast<Label *>(host.find_by_id("t"));
        EXPECT(c != nullptr && t != nullptr);
        EXPECT(c->has_background());
        EXPECT(!t->has_background());

        core::Graphics g(200, 60, nullptr);
        host.draw(g);
        const auto cp = c->get_position();
        const auto cs = c->get_size();
        // background face sampled away from the top-left 5x7 text
        EXPECT(test::pixel_at(g, cp.x + cs.width - 1, cp.y + cs.height - 1) ==
               core::Color::from(0, 0, 255).pixel);  // BLUE face
        bool saw_red = false;  // RED text somewhere in the box
        for (int y = 0; y < cs.height && !saw_red; ++y)
        {
            for (int x = 0; x < cs.width; ++x)
            {
                if (test::pixel_at(g, cp.x + x, cp.y + y) ==
                    core::Color::from(255, 0, 0).pixel)
                {
                    saw_red = true;
                    break;
                }
            }
        }
        EXPECT(saw_red);
    }

    // lengths: px and percent; bad values silently unset
    {
        ui_node r = parse_html(
            "<label style=\"width:120px; height:40px\">w</label>\n"
            "<label style=\"width: 50%\">p</label>\n"
            "<label style=\"height: auto\">a</label>\n",
            nullptr);
        EXPECT(test::vget<long long>(node_prop_v(r.children[0], "width")) == 120);
        EXPECT(test::vget<long long>(node_prop_v(r.children[0], "height")) == 40);
        EXPECT(test::vget<std::string>(node_prop_v(r.children[1], "width")) == "50%");
        EXPECT(find_prop(r.children[2], "height") < 0);  // auto -> unset
        EXPECT(find_prop(r.children[2], "width") < 0);
    }

    // meter degrades to progress_bar with its numeric attributes
    {
        ui_node r = parse_html(
            "<meter id=\"m\" min=\"0\" max=\"200\" value=\"50\"></meter>\n", nullptr);
        EXPECT(r.children[0].type == "progress_bar");
        EXPECT(r.children[0].id == "m");
        EXPECT(test::vget<long long>(node_prop_v(r.children[0], "min")) == 0);
        EXPECT(test::vget<long long>(node_prop_v(r.children[0], "max")) == 200);
        EXPECT(test::vget<long long>(node_prop_v(r.children[0], "value")) == 50);
    }

    // B3: full integer grammar for range/value/group (negatives reach
    // the widgets, which clamp); step stays non-negative; malformed
    // values are still dropped
    {
        ui_node r = parse_html(
            "<meter id=\"m\" min=\"-5\" max=\"5\" value=\"-3\"></meter>\n"
            "<meter id=\"o\" min=\"0\" max=\"100\" value=\"150\"></meter>\n"
            "<radio id=\"g\" group=\"-2\">r</radio>\n"
            "<knob id=\"k\" step=\"-2\" min=\"0\" max=\"10\" value=\"3\"></knob>\n"
            "<meter id=\"bad\" value=\"abc\"></meter>\n",
            nullptr);
        EXPECT(r.children.size() == 5);
        EXPECT(test::vget<long long>(node_prop_v(r.children[0], "min")) == -5);
        EXPECT(test::vget<long long>(node_prop_v(r.children[0], "max")) == 5);
        EXPECT(test::vget<long long>(node_prop_v(r.children[0], "value")) == -3);
        EXPECT(test::vget<long long>(node_prop_v(r.children[1], "value")) == 150);
        EXPECT(test::vget<long long>(node_prop_v(r.children[2], "group")) == -2);
        EXPECT(find_prop(r.children[3], "step") < 0);  // negative step dropped
        EXPECT(test::vget<long long>(node_prop_v(r.children[3], "value")) == 3);
        EXPECT(find_prop(r.children[4], "value") < 0);  // malformed dropped
    }

    // B3 end-to-end: the widgets clamp what the parser passes through
    {
        ui_node root = parse_html(
            "<meter id=\"p\" min=\"-5\" max=\"5\" value=\"10\"/>\n"
            "<meter id=\"q\" min=\"10\" max=\"0\" value=\"3\"/>\n"
            "<radio id=\"r\" group=\"-2\">x</radio>\n",
            nullptr);
        FlexPanel host;
        host.set_size(200, 60);
        build(host, root);
        host.layout();
        auto *p = static_cast<ProgressBar *>(host.find_by_id("p"));
        auto *q = static_cast<ProgressBar *>(host.find_by_id("q"));
        auto *rb = static_cast<RadioButton *>(host.find_by_id("r"));
        EXPECT(p != nullptr && q != nullptr && rb != nullptr);
        EXPECT(p->get_min() == -5 && p->get_max() == 5);
        EXPECT(p->get_value() == 5);  // clamped into range
        EXPECT(q->get_min() == 10 && q->get_max() == 10);  // reversed collapses
        EXPECT(q->get_value() == 10);
        EXPECT(rb->get_group() == -2);
    }

    // <html>/<head> never construct but <style> still collects; <title>
    // is silently ignored; attribute entities decode
    {
        ui_node r = parse_html(
            "<html>\n"
            "  <head>\n"
            "    <title>ignored</title>\n"
            "    <style>button { color: red; }</style>\n"
            "  </head>\n"
            "  <body>\n"
            "    <button id=\"a&amp;b\">X</button>\n"
            "  </body>\n"
            "</html>\n",
            nullptr);
        EXPECT(r.children.size() == 1);
        EXPECT(r.children[0].type == "button");
        EXPECT(r.children[0].id == "a&b");
        EXPECT(test::vget<std::string>(node_prop_v(r.children[0], "color")) == "red");
    }

    // nested inline tags merge their text into the enclosing leaf
    {
        ui_node r = parse_html("<span>a<span>b</span>c</span>\n", nullptr);
        EXPECT(r.children.size() == 1);
        EXPECT(r.children[0].type == "label");
        EXPECT(test::vget<std::string>(node_prop_v(r.children[0], "text")) == "abc");
    }

    // B1: br inside a leaf warns (no line breaking until H-1) and
    // degrades to a word space in the single-line label; edges trim
    // clean and runs collapse
    {
        ui_node r = parse_html("<label>a<br/>b</label>\n", nullptr);
        EXPECT(r.children.size() == 1);
        EXPECT(test::vget<std::string>(node_prop_v(r.children[0], "text")) == "a b");

        ui_node r2 = parse_html("<label><br/>b</label>\n", nullptr);
        EXPECT(test::vget<std::string>(node_prop_v(r2.children[0], "text")) == "b");

        ui_node r3 = parse_html("<label>a<br/></label>\n", nullptr);
        EXPECT(test::vget<std::string>(node_prop_v(r3.children[0], "text")) == "a");

        ui_node r4 = parse_html("<label>a<br/><br/>b</label>\n", nullptr);
        EXPECT(test::vget<std::string>(node_prop_v(r4.children[0], "text")) == "a b");
    }

    // C1/C5: rejected selectors are inert AND consume their bodies, so
    // following rules still apply; a bare '#' matches nothing
    {
        ui_node r = parse_html(
            "<style>\n"
            "  label, button { color: red; }\n"
            "  label:hover { color: green; }\n"
            "  @media x { label { color: blue; } }\n"
            "  # { color: yellow; }\n"
            "  { color: magenta; }\n"
            "  span { color: cyan; }\n"
            "</style>\n"
            "<label>L</label><span>S</span>\n",
            nullptr);
        EXPECT(r.children.size() == 2);
        EXPECT(find_prop(r.children[0], "color") < 0);  // label: no rule hit
        EXPECT(test::vget<std::string>(node_prop_v(r.children[1], "color")) ==
               "cyan");  // valid rule after the junk survives
    }

    // C4: an unquoted value ends at whitespace or '/': value=30/> is
    // value "30", self-closed
    {
        ui_node r = parse_html(
            "<meter id=\"m\" min=\"0\" max=\"100\" value=30/>\n", nullptr);
        EXPECT(r.children.size() == 1);
        EXPECT(r.children[0].type == "progress_bar");
        EXPECT(test::vget<long long>(node_prop_v(r.children[0], "value")) == 30);

        ui_node r2 = parse_html("<div id=x><label>y</label></div>\n", nullptr);
        EXPECT(r2.type == "column");
        EXPECT(r2.id == "x");
    }

    // C6: single-quoted values mirror double-quoted ones (entities
    // included); the other quote stays literal inside
    {
        ui_node r = parse_html(
            "<div id='box' style='gap: 2'><label>x</label></div>\n", nullptr);
        EXPECT(r.type == "column");
        EXPECT(r.id == "box");
        EXPECT(test::vget<long long>(node_prop_v(r, "spacing")) == 2);

        ui_node r2 = parse_html(
            "<label id=\"a'b\">x</label><label id='c\"d'>y</label>\n", nullptr);
        EXPECT(r2.children[0].id == "a'b");
        EXPECT(r2.children[1].id == "c\"d");

        ui_node r3 = parse_html("<label title='a>b'>x</label>\n", nullptr);
        EXPECT(r3.children.size() == 1);  // '>' inside quotes ends nothing
        EXPECT(test::vget<std::string>(
                   node_prop_v(r3.children[0], "text")) == "x");
    }

    // C3: gap/padding take the contracted Npx form (bare integers keep
    // working as the legacy tolerance)
    {
        ui_node r = parse_html(
            "<div style=\"gap: 8px; padding: 4px\"><label>x</label></div>\n",
            nullptr);
        EXPECT(r.type == "column");
        EXPECT(test::vget<long long>(node_prop_v(r, "spacing")) == 8);
        EXPECT(test::vget<long long>(node_prop_v(r, "padding")) == 4);

        ui_node r2 = parse_html(
            "<div style=\"gap: 6PX\"><label>x</label></div>\n", nullptr);
        EXPECT(test::vget<long long>(node_prop_v(r2, "spacing")) == 6);
    }

    // top-level bare text becomes an anonymous label
    {
        ui_node r = parse_html("<body>hello <button>b</button></body>\n", nullptr);
        EXPECT(r.type == "root");
        EXPECT(r.children.size() == 2);
        EXPECT(r.children[0].type == "label");
        EXPECT(test::vget<std::string>(node_prop_v(r.children[0], "text")) == "hello");
    }

    // B2: the <body> style feeds the page box (pixels only, per axis);
    // %/auto/malformed stay absent; a null page pointer is tolerated
    {
        html_page pg;
        bool ok = false;
        ui_node r = parse_html(
            "<body style=\"width: 320px; height: 240px; "
            "background-color: #112233\"><label>x</label></body>\n",
            &ok, &pg);
        EXPECT(ok);
        EXPECT(pg.has_width && pg.width == 320);
        EXPECT(pg.has_height && pg.height == 240);
        EXPECT(pg.has_background);
        EXPECT(pg.background.pixel ==
               core::Color::from(0x11, 0x22, 0x33).pixel);
        EXPECT(r.children.size() == 1);

        html_page pg2;
        parse_html("<div><label>x</label></div>\n", nullptr, &pg2);
        EXPECT(!pg2.has_width && !pg2.has_height && !pg2.has_background);

        html_page pg3;
        parse_html(
            "<body style=\"width: 50%; height: auto\"><label>x</label></body>\n",
            nullptr, &pg3);
        EXPECT(!pg3.has_width && !pg3.has_height);

        html_page pg4;
        parse_html(
            "<body style=\"width: 0px; height: -5px; background-color: nope\">"
            "<label>x</label></body>\n",
            nullptr, &pg4);
        EXPECT(!pg4.has_width && !pg4.has_height && !pg4.has_background);

        // last declaration wins within the inline list, like everywhere
        html_page pg5;
        parse_html(
            "<body style=\"width: 100px; width: 200px\"><label>x</label></body>\n",
            nullptr, &pg5);
        EXPECT(pg5.has_width && pg5.width == 200);

        ui_node r6 = parse_html("<label>x</label>\n", nullptr, nullptr);
        EXPECT(r6.children.size() == 1);
    }

    // comments and doctype are skipped; an empty document yields ok=false
    {
        bool ok = true;
        ui_node r = parse_html(
            "<!-- a comment -->\n"
            "<!DOCTYPE html>\n",
            &ok);
        EXPECT(!ok);
        EXPECT(r.children.empty());

        bool ok2 = true;
        ui_node r2 = parse_html("", &ok2);
        EXPECT(!ok2);
        EXPECT(r2.children.empty());
    }

    // end-to-end: parse and materialize into a live tree
    {
        bool ok = false;
        ui_node root = parse_html(
            "<body>\n"
            "  <div style=\"gap:4\">\n"
            "    <checkbox id=\"cb\" checked>On</checkbox>\n"
            "    <meter id=\"p\" min=\"0\" max=\"100\" value=\"30\"/>\n"
            "  </div>\n"
            "</body>\n",
            &ok);
        EXPECT(ok);
        EXPECT(root.type == "column");

        FlexPanel host;
        host.set_size(200, 60);
        build(host, root);
        host.layout();
        auto *cb = static_cast<Checkbox *>(host.find_by_id("cb"));
        auto *p = static_cast<ProgressBar *>(host.find_by_id("p"));
        EXPECT(cb != nullptr && p != nullptr);
        EXPECT(cb->is_checked());
        EXPECT(p->get_value() == 30);
    }

    return test::report("html");
}