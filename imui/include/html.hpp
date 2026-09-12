#pragma once

#include <string>

#include "ui_builder.hpp"

namespace zb::ui
{
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
     * all (mirrors parse_ui_text).
     */
    ui_node parse_html(const char *html, bool *ok);
}  // namespace zb::ui