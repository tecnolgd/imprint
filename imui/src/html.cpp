#include "html.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "logging.hpp"

namespace zb::ui
{
    namespace
    {
        // -------------------------------------------------------------------
        // the whitelist (docs/html-path.md is the contract). Anything outside
        // these sets constructs nothing. The widget tags emitted below are
        // the shared ui_builder table keys, so build() materializes the
        // result with no HTML knowledge.
        // -------------------------------------------------------------------

        bool is_structural(const std::string &t)
        {
            return t == "html" || t == "head";
        }
        bool is_whitelisted_tag(const std::string &t)
        {
            return t == "div" || t == "p" || t == "span" || t == "label" ||
                   t == "button" || t == "checkbox" || t == "radio" ||
                   t == "br" || t == "toggle" || t == "gauge" ||
                   t == "knob" || t == "trend" || t == "meter";
        }
        // elements whose bare text becomes an anonymous label: the container
        // tags, and body (the document container)
        bool is_container_html(const std::string &t)
        {
            return t == "div" || t == "body";
        }
        bool is_known_css_prop(const std::string &p)
        {
            return p == "display" || p == "flex-direction" || p == "width" ||
                   p == "height" || p == "flex" || p == "gap" ||
                   p == "padding" || p == "flex-wrap" ||
                   p == "background-color" || p == "color" ||
                   p == "font-size";
        }

        // --- text helpers --------------------------------------------------

        // the five entities of the subset; anything else (&#...; included)
        // stays literal -- an explicit contract boundary. On success `p` is
        // advanced past the trailing ';'.
        bool append_entity(const char *&p, const char *end, std::string &out)
        {
            static const struct
            {
                const char *name;
                char repl;
            } kEntities[] = {
                {"amp;", '&'}, {"lt;", '<'}, {"gt;", '>'},
                {"quot;", '"'}, {"#39;", '\''},
            };
            for (const auto &e : kEntities)
            {
                const std::size_t len = std::strlen(e.name);
                if (static_cast<std::size_t>(end - p) >= len + 1 &&
                    std::string(p + 1, len) == e.name)
                {
                    out += e.repl;
                    p += len + 1;
                    return true;
                }
            }
            return false;
        }

        // expands the entity subset in [begin,end) into `out`
        void append_decoded(const char *begin, const char *end, std::string &out)
        {
            const char *z = begin;
            while (z < end)
            {
                if (*z == '&')
                {
                    const char *before = z;
                    if (append_entity(z, end, out))
                    {
                        continue;
                    }
                    z = before;  // not one of the subset: stays literal
                }
                out += *z++;
            }
        }

        // normalizes free text like HTML: trim the edges, collapse runs of
        // whitespace into a single space, decode the entity subset. The
        // pending space is emitted in front of the next token (a literal or
        // an entity), so `A &amp; B` collapses to `A & B`.
        std::string normalize_text(const char *begin, const char *end)
        {
            std::string out;
            bool pending_space = false;
            const char *p = begin;
            while (p != end)
            {
                const char c = *p;
                if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
                {
                    pending_space = true;
                    ++p;
                    continue;
                }
                if (pending_space && !out.empty())
                {
                    out += ' ';
                }
                pending_space = false;
                if (c == '&' && append_entity(p, end, out))
                {
                    continue;  // p advanced past the entity
                }
                out += c;
                ++p;
            }
            return out;
        }

        // -------------------------------------------------------------------
        // element tree (intermediate) + <style> rules
        // -------------------------------------------------------------------

        struct Elem
        {
            std::string tag;
            std::vector<std::pair<std::string, std::string>> attrs;
            std::string text;  // raw accumulation (leaves), normalized at close
            std::vector<std::unique_ptr<Elem>> children;

            const std::string &attr(const std::string &name) const
            {
                for (const auto &a : attrs)
                {
                    if (a.first == name)
                    {
                        return a.second;
                    }
                }
                static const std::string kEmpty;
                return kEmpty;
            }
        };

        struct Decl
        {
            std::string prop;
            std::string value;
        };
        struct Rule
        {
            bool by_id = false;
            std::string name;
            std::vector<Decl> decls;
        };
        using Rules = std::vector<Rule>;

        // an inline `style="..."` value parses exactly like a rule body
        void parse_declarations(const char *begin, const char *end,
                                std::vector<Decl> &out)
        {
            const char *p = begin;
            while (p < end)
            {
                while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' ||
                                   *p == '\r'))
                {
                    ++p;
                }
                if (p < end && *p == ';')
                {
                    ++p;
                    continue;
                }
                if (p >= end)
                {
                    break;
                }
                const char *prop_b = p;
                while (p < end && *p != ':' && *p != ';')
                {
                    ++p;
                }
                if (p >= end || *p != ':')
                {
                    continue;  // no colon: drop the junk segment
                }
                std::string prop(prop_b, p);
                ++p;  // ':'
                const char *val_b = p;
                while (p < end && *p != ';' && *p != '}')
                {
                    ++p;
                }
                std::string val(val_b, p);
                prop.erase(0, prop.find_first_not_of(" \t\n\r"));
                prop.erase(prop.find_last_not_of(" \t\n\r") + 1);
                val.erase(0, val.find_first_not_of(" \t\n\r"));
                val.erase(val.find_last_not_of(" \t\n\r") + 1);
                for (char &c : prop)
                {
                    if (c >= 'A' && c <= 'Z')
                    {
                        c = static_cast<char>(c - 'A' + 'a');
                    }
                }
                if (!prop.empty())
                {
                    out.push_back({std::move(prop), std::move(val)});
                }
            }
        }

        void skip_css_ws(const char *&p, const char *end)
        {
            for (;;)
            {
                while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' ||
                                   *p == '\r'))
                {
                    ++p;
                }
                if (p + 1 < end && p[0] == '/' && p[1] == '*')
                {
                    p += 2;
                    while (p + 1 < end && !(p[0] == '*' && p[1] == '/'))
                    {
                        ++p;
                    }
                    if (p + 1 < end)
                    {
                        p += 2;
                    }
                    continue;
                }
                return;
            }
        }

        // parses the concatenated <style> text: selectors are exactly a tag
        // name or an id; anything else is inert (never matches)
        void parse_css(const std::string &css, Rules &rules)
        {
            const char *begin = css.data();
            const char *end = begin + css.size();
            const char *p = begin;
            while (p < end)
            {
                skip_css_ws(p, end);
                if (p >= end)
                {
                    break;
                }
                const char *sel_b = p;
                while (p < end && *p != '{')
                {
                    ++p;
                }
                if (p >= end)
                {
                    break;  // unclosed rule: stop
                }
                std::string sel(sel_b, p);
                sel.erase(0, sel.find_first_not_of(" \t\n\r"));
                sel.erase(sel.find_last_not_of(" \t\n\r") + 1);
                ++p;  // '{'
                if (sel.empty())
                {
                    continue;
                }
                Rule r;
                if (sel[0] == '#')
                {
                    r.by_id = true;
                    r.name = sel.substr(1);
                }
                else
                {
                    bool ok = !sel.empty();
                    for (const char c : sel)
                    {
                        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                              (c >= '0' && c <= '9') || c == '-'))
                        {
                            ok = false;
                            break;
                        }
                    }
                    if (!ok)
                    {
                        continue;  // compound/class selectors are inert
                    }
                    r.name = sel;
                    for (char &c : r.name)
                    {
                        if (c >= 'A' && c <= 'Z')
                        {
                            c = static_cast<char>(c - 'A' + 'a');
                        }
                    }
                }
                const char *d_b = p;
                while (p < end && *p != '}')
                {
                    ++p;
                }
                parse_declarations(d_b, p, r.decls);
                if (p < end)
                {
                    ++p;  // '}'
                }
                rules.push_back(std::move(r));
            }
        }

        // style resolution: tag rules first, then #id rules, then inline;
        // each bucket in document order. The last matching declaration of a
        // property wins, which yields the documented precedence
        // (inline > #id > tag; no cascade, no inheritance, no specificity)
        void fold_style(const Elem &e, const Rules &rules,
                        std::vector<Decl> &folded)
        {
            const std::string &id = e.attr("id");
            for (const Rule &r : rules)
            {
                if (!r.by_id && r.name == e.tag)
                {
                    folded.insert(folded.end(), r.decls.begin(), r.decls.end());
                }
            }
            for (const Rule &r : rules)
            {
                if (r.by_id && r.name == id)
                {
                    folded.insert(folded.end(), r.decls.begin(), r.decls.end());
                }
            }
            const std::string &inline_style = e.attr("style");
            if (!inline_style.empty())
            {
                parse_declarations(inline_style.data(),
                                   inline_style.data() + inline_style.size(),
                                   folded);
            }
        }

        // the last occurrence of a property in the ordered decl list wins
        const std::string *fold_lookup(const std::vector<Decl> &folded,
                                       const std::string &prop)
        {
            const std::string *best = nullptr;
            for (const Decl &d : folded)
            {
                if (d.prop == prop)
                {
                    best = &d.value;
                }
            }
            return best;
        }

        // validity-exact integer scan (B3): "-1" is a value, not a
        // malformed marker. Same tolerant grammar as parse_int below.
        bool parse_int_value(const std::string &s, long long &out)
        {
            if (s.empty())
            {
                return false;
            }
            std::size_t i = 0;
            bool neg = false;
            if (s[i] == '-')
            {
                neg = true;
                ++i;
            }
            long long v = 0;
            for (; i < s.size(); ++i)
            {
                const char c = s[i];
                if (c < '0' || c > '9')
                {
                    return false;
                }
                if (v > (9223372036854775807LL - (c - '0')) / 10)
                {
                    return false;
                }
                v = v * 10 + (c - '0');
            }
            out = neg ? -v : v;
            return true;
        }

        long long parse_int(const std::string &s, const long long fallback)
        {
            long long v = 0;
            return parse_int_value(s, v) ? v : fallback;
        }

        // a CSS length: px -> pixels, % -> percent (1..100), "auto"/malformed
        // -> the axis stays measured (tolerance: silently not set)
        void apply_length(ui_node &n, const std::string &prop,
                          const std::string &val)
        {
            if (val == "auto")
            {
                return;
            }
            if (val.size() >= 2 && val.back() == 'x' &&
                val[val.size() - 2] == 'p')
            {
                const long long px =
                    parse_int(val.substr(0, val.size() - 2), -1);
                if (px >= 0)
                {
                    n.prop(prop, px);
                }
                return;
            }
            if (!val.empty() && val.back() == '%')
            {
                const long long pct =
                    parse_int(val.substr(0, val.size() - 1), -1);
                if (pct >= 1 && pct <= 100)
                {
                    n.prop(prop, std::to_string(pct) + "%");
                }
            }
        }

        // -------------------------------------------------------------------
        // element -> shared ui_node (the .ui tag table keys)
        // -------------------------------------------------------------------

        std::string widget_type(const Elem &e, const std::vector<Decl> &folded)
        {
            if (e.tag == "div")
            {
                if (const std::string *dir = fold_lookup(folded, "flex-direction"))
                {
                    if (*dir == "row")
                    {
                        return "row";
                    }
                }
                return "column";
            }
            if (e.tag == "p" || e.tag == "span" || e.tag == "label")
            {
                return "label";
            }
            if (e.tag == "br")
            {
                return "label";
            }
            if (e.tag == "meter")
            {
                return "progress_bar";
            }
            return e.tag;  // button/checkbox/radio/toggle/gauge/knob/trend
        }

        ui_node convert_elem(const Elem &e, const Rules &rules)
        {
            ui_node n;
            std::vector<Decl> folded;
            fold_style(e, rules, folded);
            n.type = widget_type(e, folded);

            // element attributes (the whitelist in docs/html-path.md)
            for (const auto &a : e.attrs)
            {
                const std::string &k = a.first;
                if (k == "id" && n.id.empty())
                {
                    n.id = a.second;
                }
                else if (k == "checked" &&
                         (n.type == "checkbox" || n.type == "radio" ||
                          n.type == "toggle"))
                {
                    n.prop("checked", true);
                }
                else if (k == "group" && n.type == "radio")
                {
                    // B3: a group id is equality-matched (never indexed),
                    // so any sign is a valid id
                    long long g = 0;
                    if (parse_int_value(a.second, g))
                    {
                        n.prop("group", g);
                    }
                }
                else if ((k == "min" || k == "max" || k == "step" ||
                          k == "value") &&
                         (n.type == "gauge" || n.type == "knob" ||
                          n.type == "progress_bar"))
                {
                    // B3: the full integer grammar (negatives included)
                    // reaches the widgets, which clamp value into
                    // [min, max] and collapse a reversed range to a point
                    // themselves (locked by their suites); step stays a
                    // non-negative magnitude
                    long long v = 0;
                    if (parse_int_value(a.second, v) &&
                        (k != "step" || v >= 0))
                    {
                        n.prop(k, v);
                    }
                }
            }

            if (n.type == "label" || n.type == "button" ||
                n.type == "checkbox" || n.type == "radio")
            {
                if (!e.text.empty())
                {
                    n.prop("text", e.text);
                }
            }

            // the CSS subset (known properties from the whitelist)
            if (const std::string *d = fold_lookup(folded, "display"))
            {
                if (*d == "none")
                {
                    n.prop("visible", false);
                }
            }
            if (const std::string *w = fold_lookup(folded, "width"))
            {
                apply_length(n, "width", *w);
            }
            if (const std::string *h = fold_lookup(folded, "height"))
            {
                apply_length(n, "height", *h);
            }
            if (const std::string *fx = fold_lookup(folded, "flex"))
            {
                const long long g = parse_int(*fx, -1);
                if (g >= 0)
                {
                    n.flex_grow = static_cast<int>(g);
                }
            }
            const bool is_container = n.type == "column" || n.type == "row";
            if (is_container)
            {
                if (const std::string *gv = fold_lookup(folded, "gap"))
                {
                    const long long s = parse_int(*gv, -1);
                    if (s >= 0)
                    {
                        n.prop("spacing", s);
                    }
                }
                if (const std::string *pv = fold_lookup(folded, "padding"))
                {
                    const long long p = parse_int(*pv, -1);
                    if (p >= 0)
                    {
                        n.prop("padding", p);
                    }
                }
                if (const std::string *wv = fold_lookup(folded, "flex-wrap"))
                {
                    if (*wv == "wrap")
                    {
                        n.prop("wrap", true);
                    }
                }
            }
            if (const std::string *bg = fold_lookup(folded, "background-color"))
            {
                n.prop("background", *bg);
            }
            if (const std::string *fg = fold_lookup(folded, "color"))
            {
                n.prop("color", *fg);
            }
            // font-size: parsed and ignored (no per-widget size seam yet)

            for (const Decl &d : folded)
            {
                if (!is_known_css_prop(d.prop))
                {
                    LW << "html: style property '" << d.prop
                       << "' is not in the whitelist and was ignored";
                }
            }

            if (e.tag == "br")
            {
                // a blank-line spacer: an empty label one text line tall
                n.prop("height", 7LL);
            }

            for (const auto &c : e.children)
            {
                n.children.push_back(convert_elem(*c, rules));
            }
            return n;
        }

        // -------------------------------------------------------------------
        // the streaming parser
        // -------------------------------------------------------------------

        enum frame_mode : int
        {
            k_build = 0,  // children construct widgets (body/whitelisted)
            k_no_build,   // html/head: children never construct
            k_skip,       // an off-whitelist subtree is dropped
            k_style,      // collecting <style> rule text
        };

        struct Frame
        {
            std::string tag;
            int mode = k_build;
            bool pushed = false;  // the elem already sits in the tree
            Elem *elem = nullptr; // the elem being filled (k_build only)
            std::unique_ptr<Elem> owned;  // a held inline elem (merged at close)
        };

        struct Parser
        {
            const char *p = nullptr;
            const char *end = nullptr;
            Rules rules;
            std::string css;  // accumulated raw <style> text
            std::string text_buf;
            std::unique_ptr<Elem> root;  // the body/document container
            std::vector<Frame> frames;

            bool is_leaf(const Elem &e) const
            {
                return !is_container_html(e.tag);
            }

            // opens a new element into the current context. Returns the elem
            // and whether it was pushed into the tree (a false `pushed` means
            // the caller adopts `owned` -- text is merged into the leaf parent
            // at close).
            void open_elem(const std::string &tag, Elem *&out_elem,
                           bool &out_pushed)
            {
                Frame &parent = frames.back();
                if (parent.mode != k_build || parent.elem == nullptr)
                {
                    out_elem = nullptr;
                    out_pushed = true;
                    return;
                }
                Elem *p = parent.elem;
                auto e = std::make_unique<Elem>();
                e->tag = tag;
                if (is_leaf(*p))
                {
                    // inline context: br is pushed (the materializer drops it
                    // with a warning); nested inline tags are merged into the
                    // leaf at close
                    if (tag == "br" || is_container_html(tag))
                    {
                        if (tag == "br")
                        {
                            // no line breaking until H-1: the break degrades
                            // to a word space in the single-line label (the
                            // closer trims the edges, runs collapse), while
                            // the spacer child is dropped by the materializer
                            LW << "html: <br> inside a text element has no "
                                  "line-break effect until H-1; degraded to "
                                  "a space";
                            p->text += ' ';
                        }
                        p->children.push_back(std::move(e));
                        out_elem = p->children.back().get();
                        out_pushed = true;
                        return;
                    }
                    out_elem = e.get();
                    out_pushed = false;
                    parent.owned = std::move(e);
                    return;
                }
                p->children.push_back(std::move(e));
                out_elem = p->children.back().get();
                out_pushed = true;
            }

            void flush_text()
            {
                if (text_buf.empty())
                {
                    return;
                }
                Frame &f = frames.back();
                switch (f.mode)
                {
                case k_style:
                    css += text_buf;
                    break;
                case k_build:
                    if (f.elem != nullptr)
                    {
                        if (is_leaf(*f.elem))
                        {
                            f.elem->text += text_buf;  // raw; normalized at close
                        }
                        else
                        {
                            const std::string norm = normalize_text(
                                text_buf.data(),
                                text_buf.data() + text_buf.size());
                            if (!norm.empty())
                            {
                                auto anon = std::make_unique<Elem>();
                                anon->tag = "span";
                                anon->text = norm;
                                f.elem->children.push_back(std::move(anon));
                            }
                        }
                    }
                    break;
                default:
                    break;  // k_no_build / k_skip: drop
                }
                text_buf.clear();
            }

            // finalizes a build-mode elem just before its frame is closed:
            // normalize leaf text, then merge a held inline elem into the
            // enclosing leaf
            void finalize(Frame &f)
            {
                if (f.mode != k_build || f.elem == nullptr)
                {
                    return;
                }
                if (is_leaf(*f.elem))
                {
                    f.elem->text = normalize_text(
                        f.elem->text.data(),
                        f.elem->text.data() + f.elem->text.size());
                }
                if (!f.pushed && frames.size() > 1)
                {
                    Frame &par = frames[frames.size() - 2];
                    if (par.mode == k_build && par.elem != nullptr &&
                        is_leaf(*par.elem))
                    {
                        par.elem->text += f.elem->text;
                    }
                }
            }
        };
    }  // namespace

    ui_node parse_html(const char *html, bool *ok)
    {
        if (ok != nullptr)
        {
            *ok = false;
        }
        Parser ps;
        ps.root = std::make_unique<Elem>();
        ps.root->tag = "body";  // the document container (container_html)
        ps.p = html ? html : "";
        ps.end = ps.p + (html ? std::strlen(html) : 0);
        ps.frames.push_back(
            {std::string{}, k_build, true, ps.root.get(), nullptr});

        while (ps.p < ps.end)
        {
            const char c = *ps.p;
            if (c != '<')
            {
                ps.text_buf += c;
                ++ps.p;
                continue;
            }
            // a tag, comment, or doctype begins at '<'
            ps.flush_text();

            const char *q = ps.p + 1;
            if (q < ps.end && *q == '!')
            {
                // comment <!-- ... -->  or  <!DOCTYPE ...>
                if (q + 2 < ps.end && q[1] == '-' && q[2] == '-')
                {
                    const char *close = q + 3;
                    while (close + 2 < ps.end &&
                           !(close[0] == '-' && close[1] == '-' && close[2] == '>'))
                    {
                        ++close;
                    }
                    ps.p = (close + 2 < ps.end) ? close + 3 : ps.end;
                }
                else
                {
                    while (ps.p < ps.end && *ps.p != '>')
                    {
                        ++ps.p;
                    }
                    if (ps.p < ps.end)
                    {
                        ++ps.p;
                    }
                }
                continue;
            }

            const bool closing = (q < ps.end && *q == '/');
            if (closing)
            {
                ++q;
            }
            const char *name_b = q;
            while (q < ps.end && ((*q >= 'a' && *q <= 'z') ||
                                  (*q >= 'A' && *q <= 'Z') ||
                                  (*q >= '0' && *q <= '9') || *q == '-'))
            {
                ++q;
            }
            std::string name(name_b, q);
            for (char &ch : name)
            {
                if (ch >= 'A' && ch <= 'Z')
                {
                    ch = static_cast<char>(ch - 'A' + 'a');
                }
            }

            // the token end: '>' (with '/' before it marking self-closing),
            // honoring quoted attribute values
            const char *scan = q;
            bool in_quote = false;
            while (scan < ps.end &&
                   !(!in_quote && *scan == '>'))
            {
                if (*scan == '"')
                {
                    in_quote = !in_quote;
                }
                ++scan;
            }
            const bool self_closing =
                (scan < ps.end) && (scan > q) && (scan[-1] == '/');
            const char *attr_end = (scan < ps.end ? scan : ps.end);

            if (closing)
            {
                if (!name.empty())
                {
                    // pop to the matching frame; unbalanced intermediates
                    // are finalized along the way (tolerant)
                    while (ps.frames.size() > 1 &&
                           ps.frames.back().tag != name)
                    {
                        ps.finalize(ps.frames.back());
                        ps.frames.pop_back();
                    }
                    if (ps.frames.size() > 1)
                    {
                        Frame &f = ps.frames.back();
                        ps.finalize(f);
                        ps.frames.pop_back();
                    }
                }
                ps.p = std::min(ps.end, (scan < ps.end ? scan + 1 : ps.end));
                continue;
            }

            if (name.empty())
            {
                ps.p = std::min(ps.end, (scan < ps.end ? scan + 1 : ps.end));
                continue;  // a lone '<' or a malformed token: skip it
            }

            // decide the frame mode before creating anything (whitelist
            // knowledge lives in the contract doc)
            int mode = k_build;
            if (is_structural(name) || name == "body")
            {
                mode = k_no_build;  // html/head never build
            }
            else if (name == "style")
            {
                mode = k_style;
            }
            else if (!is_whitelisted_tag(name))
            {
                const int innermost = ps.frames.back().mode;
                if (innermost == k_no_build || innermost == k_skip)
                {
                    mode = k_skip;  // head boilerplate / dropped subtree: silent
                }
                else
                {
                    LW << "html: element <" << name
                       << "> is not in the whitelist; skipped";
                    mode = k_skip;
                }
            }

            Elem *elem = nullptr;
            bool pushed = false;
            if (mode == k_build)
            {
                ps.open_elem(name, elem, pushed);
            }
            else if (name == "body" && !is_structural(name))
            {
                // body is the document container: its children land on the
                // root, so a single top-level container still becomes the
                // document root (the .ui convention)
                mode = k_build;
                elem = ps.root.get();
                pushed = true;
            }

            if (elem != nullptr && mode == k_build)
            {
                // attributes (unknown ones are silently tolerated)
                const char *attr_p = q;
                while (attr_p < attr_end)
                {
                    while (attr_p < attr_end &&
                           (*attr_p == ' ' || *attr_p == '\t' ||
                            *attr_p == '\n' || *attr_p == '\r'))
                    {
                        ++attr_p;
                    }
                    if (attr_p >= attr_end)
                    {
                        break;
                    }
                    const char *ka = attr_p;
                    while (attr_p < attr_end &&
                           ((*attr_p >= 'a' && *attr_p <= 'z') ||
                            (*attr_p >= 'A' && *attr_p <= 'Z') ||
                            (*attr_p >= '0' && *attr_p <= '9') ||
                            *attr_p == '-' || *attr_p == '_' || *attr_p == ':'))
                    {
                        ++attr_p;
                    }
                    std::string key(ka, attr_p);
                    for (char &ch : key)
                    {
                        if (ch >= 'A' && ch <= 'Z')
                        {
                            ch = static_cast<char>(ch - 'A' + 'a');
                        }
                    }
                    if (key.empty())
                    {
                        ++attr_p;  // junk: move on
                        continue;
                    }
                    while (attr_p < attr_end &&
                           (*attr_p == ' ' || *attr_p == '\t' || *attr_p == '\n' ||
                            *attr_p == '\r'))
                    {
                        ++attr_p;
                    }
                    std::string value;
                    if (attr_p < attr_end && *attr_p == '=')
                    {
                        ++attr_p;
                        while (attr_p < attr_end && (*attr_p == ' ' || *attr_p == '\t'))
                        {
                            ++attr_p;
                        }
                        if (attr_p < attr_end && *attr_p == '"')
                        {
                            ++attr_p;
                            const char *vb = attr_p;
                            while (attr_p < attr_end && *attr_p != '"')
                            {
                                ++attr_p;
                            }
                            const char *ve = attr_p;
                            if (attr_p < attr_end)
                            {
                                ++attr_p;  // the closing quote
                            }
                            append_decoded(vb, ve, value);
                        }
                        else
                        {
                            const char *vb = attr_p;
                            while (attr_p < attr_end && *attr_p != ' ' &&
                                   *attr_p != '\t' && *attr_p != '\n' &&
                                   *attr_p != '\r')
                            {
                                ++attr_p;
                            }
                            append_decoded(vb, attr_p, value);
                        }
                    }
                    else
                    {
                        value = key;  // valueless boolean attribute
                    }
                    elem->attrs.emplace_back(std::move(key), std::move(value));
                }
            }

            if (self_closing)
            {
                // no frame is pushed for a self-closing tag
                if (mode == k_build && !pushed)
                {
                    ps.frames.back().owned.reset();  // held temp: discard
                }
            }
            else
            {
                ps.frames.push_back({std::move(name), mode, pushed, elem,
                                     std::move(ps.frames.back().owned)});
            }

            ps.p = std::min(ps.end, (scan < ps.end ? scan + 1 : ps.end));
        }

        // trailing text at EOF
        ps.flush_text();

        // clean up any unclosed frames
        while (ps.frames.size() > 1)
        {
            ps.finalize(ps.frames.back());
            ps.frames.pop_back();
        }

        // parse the collected <style> text (rules are global)
        parse_css(ps.css, ps.rules);

        // convert: the root's children are the top-level widgets
        ui_node doc;
        doc.type = "root";
        for (const auto &c : ps.root->children)
        {
            doc.children.push_back(convert_elem(*c, ps.rules));
        }

        if (ok != nullptr)
        {
            *ok = !doc.children.empty();
        }
        // a single top-level container becomes the document root itself,
        // so its spacing/padding/wrap apply to the build() host (the .ui
        // convention)
        if (doc.children.size() == 1 &&
            (doc.children[0].type == "panel" || doc.children[0].type == "column" ||
             doc.children[0].type == "row"))
        {
            return std::move(doc.children[0]);
        }
        return doc;
    }
}  // namespace zb::ui