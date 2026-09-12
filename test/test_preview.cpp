#include "test.hpp"

#include "ui_preview.hpp"

// the D5 extension routing matrix: ".html"/".htm" select the HTML/CSS
// subset front-end, everything else the .ui design-file front-end.
// Matching is a plain case-sensitive suffix (filesystems are).
int test_preview()
{
    using zb::app::ui_preview::is_html_path;

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

    return test::report("preview");
}
