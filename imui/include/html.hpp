#pragma once

#include <string>

#include "imcore.hpp"
#include "ui_builder.hpp"

namespace zb::ui
{
    /*
     * Shared color-value resolution for the background/color props and
     * the HTML page box ("#rgb" / "#rrggbb" / named subset /
     * "transparent", names case-insensitive; false = malformed,
     * transparent, or absent). Declared here (not in ui_builder.hpp) so
     * that header stays free of the imcore include: the standalone
     * ui_embed host tool compiles ui_file.cpp with a minimal closure.
     */
    bool parse_color(const std::string &s, core::Color &out);

    /*
     * Page box (B2): the document-level size/background from the <body>
     * style. It travels *beside* the widget tree, never inside it: the
     * consumer (preview/app) resolves the initial screen/buffer size
     * from it before materializing, so supporting it never touches the
     * "buffer never resizes" presentation contract (creation-time only).
     *
     * Sizes are pixels per axis and independent: a missing/invalid axis
     * stays absent and falls back to the shell size, then the app
     * default (the consumer warns on the default). Only Npx values > 0
     * land here (% has no parent box to resolve against, auto means
     * "ask the shell").
     */
    struct html_page
    {
        bool has_width = false;
        int width = 0;
        bool has_height = false;
        int height = 0;
        bool has_background = false;
        core::Color background{};  // valid iff has_background
    };

    /*
     * Parses an HTML/CSS subset document into the shared ui_node graph.
     * docs/html-path.md is the whitelist contract: the elements, element
     * attributes, and CSS properties listed there construct; everything
     * else is tolerated away (off-whitelist elements warn and drop their
     * content, unknown style declarations warn, unknown attributes are
     * silent). <html>/<head> containers never construct.
     *
     * Root handling mirrors parse_ui_text: a body with exactly one
     * top-level container is returned as the document root itself (its
     * spacing/padding/wrap apply to the build() host); otherwise the
     * root is a pseudo node (type "root") whose children are the
     * top-level widgets.
     *
     * Static declarative front-end only (the design-file boundary): no
     * dynamic behavior enters the tree. Init path only — a parse happens
     * once at document load, never on the frame path.
     *
     * `ok` is set to false only when the document yields no widget at
     * all (mirrors parse_ui_text). `page` (may be null) receives the
     * page box when provided.
     */
    ui_node parse_html(const char *html, bool *ok, html_page *page = nullptr);
}  // namespace zb::ui
