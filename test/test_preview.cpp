#include "test.hpp"

#include "ui_preview.hpp"

// the D5 extension routing matrix: ".html"/".htm" select the HTML/CSS
// subset front-end, everything else the .ui design-file front-end.
// Matching is a plain case-sensitive suffix (filesystems are).
int test_preview()
{
    using zb::app::ui_preview::is_html_path;
    using zb::app::ui_preview::resolve_page_size;
    using zb::ui::html_page;

    EXPECT(is_html_path("page.html"));
    EXPECT(is_html_path("page.htm"));
    EXPECT(is_html_path("dir/sub/page.html"));
    EXPECT(is_html_path("dir/sub/page.htm"));

    EXPECT(!is_html_path("menu.ui"));
    EXPECT(!is_html_path("page.HTML"));  // case-sensitive: not routed
    EXPECT(!is_html_path("page.HTM"));
    EXPECT(!is_html_path("page.htmlx"));
    EXPECT(!is_html_path("page.xhtml"));
    EXPECT(!is_html_path("pagehtml"));
    EXPECT(!is_html_path("html"));
    EXPECT(!is_html_path(""));
    EXPECT(!is_html_path(".ui"));

    // B2 resolution: document page, then shell dims, then the app
    // default (warns, unasserted here); per-axis and independent
    {
        const html_page empty;
        EXPECT(resolve_page_size(empty, 800, 600) ==
               std::make_pair(800, 600));  // shell wins, no warning
        EXPECT(resolve_page_size(empty, 0, 0) ==
               std::make_pair(
                   zb::app::ui_preview::kDefaultScreenWidth,
                   zb::app::ui_preview::kDefaultScreenHeight));

        html_page sized;
        sized.has_width = true;
        sized.width = 320;
        sized.has_height = true;
        sized.height = 240;
        EXPECT(resolve_page_size(sized, 800, 600) ==
               std::make_pair(320, 240));  // page beats shell
        EXPECT(resolve_page_size(sized, 0, 0) == std::make_pair(320, 240));

        html_page half;  // one axis set: the other falls through
        half.has_width = true;
        half.width = 320;
        EXPECT(resolve_page_size(half, 800, 600) == std::make_pair(320, 600));
        EXPECT(resolve_page_size(half, 0, 0) ==
               std::make_pair(
                   320, zb::app::ui_preview::kDefaultScreenHeight));

        html_page junk;  // non-positive page dims count as absent
        junk.has_width = true;
        junk.width = 0;
        junk.has_height = true;
        junk.height = -4;
        EXPECT(resolve_page_size(junk, 800, 600) == std::make_pair(800, 600));
    }

    return test::report("preview");
}
