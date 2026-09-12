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

        void ascii_lower_inplace(std::string &s)
        {
            for (char &c : s)
            {
                if (c >= 'A' && c <= 'Z')
                {
                    c = static_cast<char>(c - 'A' + 'a');
                }
            }
        }

        // -------------------------------------------------------------------
        // the tokenizer (B6): raw input -> token stream. It owns every
        // lexical rule (comments/doctype skipping, tag-name casing,
        // quote-aware token ends, attribute values with entity decoding)
        // and carries 1-based line numbers for located warnings. It knows
        // nothing about the whitelist or the tree.
        // -------------------------------------------------------------------

        struct Token
        {
            enum class Kind
            {
                eof,
                text,  // raw span (normalization is the tree layer's job)
                open,
                close,
            };
            Kind kind = Kind::eof;
            std::string name;  // lower-cased tag name (open/close)
            std::vector<std::pair<std::string, std::string>> attrs;  // open
            bool self_closing = false;  // open only
            std::string text;           // text only
            int line = 1;               // 1-based line where it starts
        };

        class Tokenizer
        {
          public:
            Tokenizer(const char *begin, const char *end)
                : p_(begin), end_(end)
            {
            }

            Token next()
            {
                for (;;)
                {
                    if (p_ >= end_)
                    {
                        return Token{};
                    }
                    if (*p_ != '<')
                    {
                        return read_text();
                    }
                    // a tag, comment, or doctype begins at '<'
                    const char *q = p_ + 1;
                    if (q < end_ && *q == '!')
                    {
                        skip_bang();  // comments/doctypes are silent
                        continue;
                    }
                    Token t;
                    if (read_tag(t))
                    {
                        return t;
                    }
                    // a lone '<' or malformed token: consumed, move on
                }
            }

          private:
            const char *p_;
            const char *end_;
            int line_ = 1;

            void count_lines(const char *from, const char *to)
            {
                for (const char *z = from; z < to; ++z)
                {
                    if (*z == '\n')
                    {
                        ++line_;
                    }
                }
            }

            Token read_text()
            {
                Token t;
                t.kind = Token::Kind::text;
                t.line = line_;
                const char *start = p_;
                while (p_ < end_ && *p_ != '<')
                {
                    ++p_;
                }
                t.text.assign(start, p_);
                count_lines(start, p_);
                return t;
            }

            // comment <!-- ... --> or <!DOCTYPE ...>: consumed silently
            void skip_bang()
            {
                const char *start = p_;
                const char *q = p_ + 1;
                if (q + 2 < end_ && q[1] == '-' && q[2] == '-')
                {
                    const char *close = q + 3;
                    while (close + 2 < end_ &&
                           !(close[0] == '-' && close[1] == '-' && close[2] == '>'))
                    {
                        ++close;
                    }
                    p_ = (close + 2 < end_) ? close + 3 : end_;
                }
                else
                {
                    while (p_ < end_ && *p_ != '>')
                    {
                        ++p_;
                    }
                    if (p_ < end_)
                    {
                        ++p_;
                    }
                }
                count_lines(start, p_);
            }

            static bool is_name_char(const char c)
            {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                       (c >= '0' && c <= '9') || c == '-';
            }

            static bool is_attr_char(const char c)
            {
                return is_name_char(c) || c == '_' || c == ':';
            }

            static bool is_space(const char c)
            {
                return c == ' ' || c == '\t' || c == '\n' || c == '\r';
            }

            // Reads one tag token into `t`; returns false for a lone '<'
            // or malformed token (consumed, no token produced).
            bool read_tag(Token &t)
            {
                t.line = line_;
                const char *start = p_;
                const char *q = p_ + 1;
                const bool closing = (q < end_ && *q == '/');
                if (closing)
                {
                    ++q;
                }
                const char *name_b = q;
                while (q < end_ && is_name_char(*q))
                {
                    ++q;
                }
                t.name.assign(name_b, q);
                ascii_lower_inplace(t.name);

                // the token end: '>' (with '/' before it marking
                // self-closing), honoring quoted attribute values. C6:
                // both quote styles open a quoted span; only the matching
                // quote closes it (a '"' inside '...' stays literal).
                const char *scan = q;
                char quote = 0;
                while (scan < end_ && !(quote == 0 && *scan == '>'))
                {
                    if (quote == 0 && (*scan == '"' || *scan == '\''))
                    {
                        quote = *scan;
                    }
                    else if (*scan == quote)
                    {
                        quote = 0;
                    }
                    ++scan;
                }
                const bool self_closing =
                    (scan < end_) && (scan > q) && (scan[-1] == '/');
                const char *attr_end = (scan < end_ ? scan : end_);
                const char *token_end =
                    std::min(end_, (scan < end_ ? scan + 1 : end_));

                if (closing || t.name.empty())
                {
                    // a closing tag (possibly nameless: ignored downstream)
                    // or a lone '<' / malformed token: consume and move on
                    t.kind = Token::Kind::close;
                    p_ = token_end;
                    count_lines(start, p_);
                    return !t.name.empty();
                }

                t.kind = Token::Kind::open;
                t.self_closing = self_closing;

                // attributes (unknown ones are kept; the tree layer drops
                // what is off-whitelist silently)
                const char *attr_p = q;
                while (attr_p < attr_end)
                {
                    while (attr_p < attr_end && is_space(*attr_p))
                    {
                        ++attr_p;
                    }
                    if (attr_p >= attr_end)
                    {
                        break;
                    }
                    const char *ka = attr_p;
                    while (attr_p < attr_end && is_attr_char(*attr_p))
                    {
                        ++attr_p;
                    }
                    std::string key(ka, attr_p);
                    ascii_lower_inplace(key);
                    if (key.empty())
                    {
                        ++attr_p;  // junk: move on
                        continue;
                    }
                    while (attr_p < attr_end && is_space(*attr_p))
                    {
                        ++attr_p;
                    }
                    std::string value;
                    if (attr_p < attr_end && *attr_p == '=')
                    {
                        ++attr_p;
                        while (attr_p < attr_end &&
                               (*attr_p == ' ' || *attr_p == '\t'))
                        {
                            ++attr_p;
                        }
                        // C6: single-quoted values mirror double-quoted
                        // ones (entity decoding included)
                        if (attr_p < attr_end &&
                            (*attr_p == '"' || *attr_p == '\''))
                        {
                            const char qc = *attr_p;
                            ++attr_p;
                            const char *vb = attr_p;
                            while (attr_p < attr_end && *attr_p != qc)
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
                            // C4: an unquoted value ends at whitespace or
                            // '/': `<meter value=30/>` means value "30",
                            // self-closed (author intent over the letter of
                            // HTML5, where the '/' would join the value)
                            const char *vb = attr_p;
                            while (attr_p < attr_end && !is_space(*attr_p) &&
                                   *attr_p != '/')
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
                    t.attrs.emplace_back(std::move(key), std::move(value));
                }

                p_ = token_end;
                count_lines(start, p_);
                return true;
            }
        };

        // -------------------------------------------------------------------
        // element tree (intermediate) + <style> rules
        // -------------------------------------------------------------------

        struct Elem
        {
            std::string tag;
            std::vector<std::pair<std::string, std::string>> attrs;
            std::string text;  // raw accumulation (leaves), normalized at close
            std::vector<std::unique_ptr<Elem>> children;
            int line = 1;  // 1-based source line of the open tag (B6)

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
            bool important = false;  // B5: trailing "!important" tier
        };
        struct Rule
        {
            bool by_id = false;
            std::string name;
            std::vector<Decl> decls;
        };
        using Rules = std::vector<Rule>;

        // B5: a trailing "!important" (ASCII case-insensitive,
        // whitespace tolerated around it) lifts the declaration into the
        // important tier instead of the value. "red!" or "a!b" are not
        // markers -- the value keeps them and fails parsing as usual.
        bool take_important(std::string &val)
        {
            const std::size_t bang = val.find_last_of('!');
            if (bang == std::string::npos)
            {
                return false;
            }
            std::string rest = val.substr(bang + 1);
            rest.erase(0, rest.find_first_not_of(" \t\n\r"));
            rest.erase(rest.find_last_not_of(" \t\n\r") + 1);
            for (char &c : rest)
            {
                if (c >= 'A' && c <= 'Z')
                {
                    c = static_cast<char>(c - 'A' + 'a');
                }
            }
            if (rest != "important")
            {
                return false;
            }
            val.erase(bang);
            val.erase(val.find_last_not_of(" \t\n\r") + 1);
            return true;
        }

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
                    bool important = false;
                    if (!val.empty())
                    {
                        important = take_important(val);
                    }
                    out.push_back({std::move(prop), std::move(val), important});
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

        // consumes a rule body starting just past '{': nested blocks
        // (at-rules), /* comments */, and "..." strings are honored so a
        // rejected rule cannot swallow the rules after it (C1). Returns
        // just past the matching '}' (or end on unterminated input).
        const char *skip_rule_body(const char *p, const char *end)
        {
            int depth = 1;
            while (p < end && depth > 0)
            {
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
                if (*p == '"')
                {
                    ++p;
                    while (p < end && *p != '"')
                    {
                        if (*p == '\\' && p + 1 < end)
                        {
                            p += 2;
                        }
                        else
                        {
                            ++p;
                        }
                    }
                    if (p < end)
                    {
                        ++p;
                    }
                    continue;
                }
                if (*p == '{')
                {
                    ++depth;
                }
                else if (*p == '}')
                {
                    --depth;
                }
                ++p;
            }
            return p;
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
                    p = skip_rule_body(p, end);
                    continue;
                }
                Rule r;
                if (sel[0] == '#')
                {
                    r.by_id = true;
                    r.name = sel.substr(1);
                    if (r.name.empty())
                    {
                        p = skip_rule_body(p, end);
                        continue;  // C5: a bare '#' is inert, not match-all
                    }
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
                        // compound/class selectors are inert -- and their
                        // bodies are consumed so following rules survive
                        p = skip_rule_body(p, end);
                        continue;
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

        // the last occurrence of a property in the ordered decl list wins,
        // with the B5 tier rule: the last !important match beats every
        // normal one; inside a tier the buckets keep tag -> #id -> inline
        // document order (no cascade, no specificity)
        const std::string *fold_lookup(const std::vector<Decl> &folded,
                                       const std::string &prop)
        {
            const std::string *best = nullptr;
            const std::string *best_important = nullptr;
            for (const Decl &d : folded)
            {
                if (d.prop == prop)
                {
                    best = &d.value;
                    if (d.important)
                    {
                        best_important = &d.value;
                    }
                }
            }
            return (best_important != nullptr) ? best_important : best;
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

        // B4: CSS keyword values are ASCII case-insensitive (HTML
        // semantics); ids and all other values keep their case
        std::string ascii_lower(std::string s)
        {
            ascii_lower_inplace(s);
            return s;
        }

        // a CSS length: px -> pixels, % -> percent (1..100), "auto"/malformed
        // -> the axis stays measured (tolerance: silently not set).
        // B4: units and "auto" are ASCII case-insensitive (digits and %
        // are unaffected by the fold)
        void apply_length(ui_node &n, const std::string &prop,
                          const std::string &val)
        {
            const std::string v = ascii_lower(val);
            if (v == "auto")
            {
                return;
            }
            if (v.size() >= 2 && v.back() == 'x' &&
                v[v.size() - 2] == 'p')
            {
                const long long px =
                    parse_int(v.substr(0, v.size() - 2), -1);
                if (px >= 0)
                {
                    n.prop(prop, px);
                }
                return;
            }
            if (!v.empty() && v.back() == '%')
            {
                const long long pct =
                    parse_int(v.substr(0, v.size() - 1), -1);
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
                    if (ascii_lower(*dir) == "row")
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
                if (ascii_lower(*d) == "none")
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
                    if (ascii_lower(*wv) == "wrap")
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
        // the page box (B2): the <body> style feeds document-level
        // size/background. Sizes are pixels per axis and independent: %
        // has no parent box to resolve against and auto means "ask the
        // shell", so only Npx values > 0 land in the page (everything
        // else stays absent and falls back downstream, silently).
        // -------------------------------------------------------------------

        void extract_page(const Elem &body, html_page &page)
        {
            const std::string &bs = body.attr("style");
            if (bs.empty())
            {
                return;
            }
            std::vector<Decl> decls;
            parse_declarations(bs.data(), bs.data() + bs.size(), decls);
            for (const Decl &d : decls)
            {
                if (d.prop == "width" || d.prop == "height")
                {
                    const std::string v = ascii_lower(d.value);
                    if (v.size() >= 2 && v.back() == 'x' &&
                        v[v.size() - 2] == 'p')
                    {
                        long long px = 0;
                        if (parse_int_value(
                                v.substr(0, v.size() - 2), px) &&
                            px > 0)
                        {
                            const int clamped = static_cast<int>(
                                std::min<long long>(px, 2147483647LL));
                            if (d.prop == "width")
                            {
                                page.has_width = true;
                                page.width = clamped;
                            }
                            else
                            {
                                page.has_height = true;
                                page.height = clamped;
                            }
                        }
                    }
                }
                else if (d.prop == "background-color")
                {
                    core::Color c{};
                    if (parse_color(d.value, c))
                    {
                        page.has_background = true;
                        page.background = c;
                    }
                }
            }
        }

        // -------------------------------------------------------------------
        // the tree builder (B6): tokens -> element tree. It owns the frame
        // stack, inline merging, and the whitelist modes, and knows nothing
        // about characters, quotes, or comments anymore.
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
            Rules rules;
            std::string css;  // accumulated raw <style> text
            std::string text_buf;
            int text_line = 1;  // source line of the buffered text (B6)
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
                           bool &out_pushed, const int line)
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
                e->line = line;
                if (is_leaf(*p))
                {
                    // inline context: br is pushed (the materializer drops it
                    // with a warning); nested inline tags are merged into the
                    // leaf at close
                    if (tag == "br" || is_container_html(tag))
                    {
                        if (tag == "br")
                        {
                            // B1: no line breaking until H-1: the break
                            // degrades to a word space in the single-line
                            // label (the closer trims the edges, runs
                            // collapse), while the spacer child is dropped
                            // by the materializer
                            LW << "html: line " << line
                               << ": <br> inside a text element has no "
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
                                anon->line = text_line;
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

            void close_to(const std::string &name)
            {
                // pop to the matching frame; unbalanced intermediates
                // are finalized along the way (tolerant)
                while (frames.size() > 1 && frames.back().tag != name)
                {
                    finalize(frames.back());
                    frames.pop_back();
                }
                if (frames.size() > 1)
                {
                    Frame &f = frames.back();
                    finalize(f);
                    frames.pop_back();
                }
                // a closing tag without an open frame is ignored
            }

            void handle_open(const Token &t)
            {
                // decide the frame mode before creating anything (whitelist
                // knowledge lives in the contract doc)
                const std::string &name = t.name;
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
                    const int innermost = frames.back().mode;
                    if (innermost == k_no_build || innermost == k_skip)
                    {
                        mode = k_skip;  // head boilerplate / dropped subtree
                    }
                    else
                    {
                        LW << "html: line " << t.line << ": element <" << name
                           << "> is not in the whitelist; skipped";
                        mode = k_skip;
                    }
                }

                Elem *elem = nullptr;
                bool pushed = false;
                if (mode == k_build)
                {
                    open_elem(name, elem, pushed, t.line);
                    if (elem != nullptr)
                    {
                        for (const auto &a : t.attrs)
                        {
                            elem->attrs.emplace_back(a.first, a.second);
                        }
                    }
                }
                else if (name == "body")
                {
                    // body is the document container: its children land on
                    // the root, so a single top-level container still
                    // becomes the document root (the .ui convention)
                    mode = k_build;
                    elem = root.get();
                    pushed = true;
                    if (elem != nullptr)
                    {
                        for (const auto &a : t.attrs)
                        {
                            elem->attrs.emplace_back(a.first, a.second);
                        }
                    }
                }

                // C2: br is void (HTML semantics): <br>, <br/>, <br />
                // are equivalent and it never takes a close; a stray
                // </br> then finds no frame and is ignored
                const bool closed = t.self_closing || name == "br";
                if (closed)
                {
                    // no frame is pushed for a self-closing tag
                    if (mode == k_build && !pushed)
                    {
                        frames.back().owned.reset();  // held temp: discard
                    }
                    return;
                }
                frames.push_back({name, mode, pushed, elem,
                                  std::move(frames.back().owned)});
            }

            void feed(const Token &t)
            {
                switch (t.kind)
                {
                case Token::Kind::text:
                    text_buf += t.text;
                    text_line = t.line;
                    flush_text();
                    break;
                case Token::Kind::close:
                    if (!t.name.empty())
                    {
                        close_to(t.name);
                    }
                    break;
                case Token::Kind::open:
                    handle_open(t);
                    break;
                case Token::Kind::eof:
                    break;
                }
            }
        };
    }  // namespace

    ui_node parse_html(const char *html, bool *ok, html_page *page)
    {
        if (ok != nullptr)
        {
            *ok = false;
        }
        Parser ps;
        ps.root = std::make_unique<Elem>();
        ps.root->tag = "body";  // the document container (container_html)
        ps.frames.push_back(
            {std::string{}, k_build, true, ps.root.get(), nullptr});

        const char *begin = html ? html : "";
        Tokenizer tz(begin, begin + (html ? std::strlen(html) : 0));
        for (;;)
        {
            const Token t = tz.next();
            if (t.kind == Token::Kind::eof)
            {
                break;
            }
            ps.feed(t);
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

        // the page box travels beside the tree (B2); the root Elem is
        // the body container, so its style is the page style
        if (page != nullptr)
        {
            *page = html_page{};
            extract_page(*ps.root, *page);
        }

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
