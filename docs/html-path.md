# Imprint HTML/CSS Subset (`.html`)

An **optional declarative front-end** that renders a narrow HTML/CSS subset
through the existing widget tree, complementing the `.ui` design file.
It lets authors with HTML/CSS habits describe screens without learning the
`.ui` grammar or the C++ builder API — the parser maps supported elements
onto the existing widget tag table, and the whole result materializes
through the shared `ui_node` design-file layer
(`docs/ARCHITECTURE.md` §4.10; shared design-file semantics in
`docs/design-file.md`).

No JS, no CSS cascade engine: this is a static declarative front-end onto
widgets, deliberately narrow. **The whitelist tables below are the
contract.** Anything not in them constructs nothing — there is no
blacklist to read, the table *is* the boundary.

## Pipeline

```
page.html ─► parse_html(text) ─► ui_node ─► build(host, root) ─► widget tree
                  (imui)         (shared IR)       (shared materializer)
```

`parse_html(const char*, bool *ok)` is the only parsing entry (init path,
may allocate; a parse happens once at document load, never on the frame
path). The produced `ui_node` is byte-identical in shape to what
`parse_ui_text` produces, so every downstream rule — the property table,
materialization tolerance, `find_by_id` wiring, event binding in C++ —
applies unchanged.

## Root and document structure rules

- `<html>` / `<head>` containers and their contents **never construct
  widgets**. `<style>` blocks are consumed for rules; everything else in
  the head (`<title>`, `<meta>`, `<link>`, `<script>`, ...) is ignored.
  There is no resource loading of any kind — no external CSS, fonts,
  images, or scripts.
- Only `<body>`, its text content, `<style>` rules, and the whitelisted
  elements construct.
- Root handling mirrors the `.ui` convention: when the body has exactly
  one top-level container child, that node is returned as the document
  root, so its container properties (`spacing`/`padding`/`wrap`) apply to
  the build host; otherwise a pseudo-root wraps the body's children and
  they materialize into the host one by one.
- Element tags and attribute names are lower-cased (HTML is
  case-insensitive); attribute values and `id` keep their case.
- Text content: the trimmed concatenation of an element's text children
  (whitespace-only text nodes are dropped). Bare text inside a container
  becomes an anonymous `label` (a container's text must go somewhere).
  Entities: `&amp;` `&lt;` `&gt;` `&quot;` `&#39;` — nothing else.
- The whole path is **tolerant and never fails**: `ok=false` only for a
  document nothing can build (mirrors `parse_ui_text`). Violations are
  reported per the Tolerance table below.

## Tolerance (how an off-whitelist construct is handled)

| Construct | Handling |
|---|---|
| Element not in the whitelist | **LW warning + skipped**; its content is dropped. "Not in the table = not built" — the honest signal, so mistyped customs (`<metter>`) or unsupported HTML (`<table>`, `<input>`, `<form>`) never render a wrong structure |
| `<br>` inside an inline element (`<span>`) | the parser warns (no line breaking until H-1) and degrades the break to a word space in the single-line label; the spacer child is dropped by leaf materialization with a warning (the same rule as `.ui` leaf children); place `br` as a child of a container |
| Attribute not in the whitelist | silently tolerated; `class=` is accepted but inert (no selector support until H-5) |
| Style declaration not in the whitelist | **LW warning + ignored** (the element keeps its default presentation) |
| Malformed value (bad color, bad number, bad percent) | silently defaulted (the shared property table's tolerance) |

## Whitelist — elements

| Element | `ui_node` tag | Notes |
|---|---|---|
| `div` | `column` / `row` | default `column` (block reading order); `flex-direction: row` → `row`. `display: block` and `display: flex` are equivalent — the div is a content-measuring flex container, **not** HTML block layout; it never stretches to fill a parent's main axis. `flex:` markup drives fill |
| `p`, `span`, `label` | `label` | single-line labels; no wrapping until H-1 |
| `button` | `button` | `text` = element text content |
| `checkbox` | `checkbox` | `text` = content; `checked` = **attribute presence** (HTML semantics) |
| `radio` | `radio` | `text` = content; `checked` = presence; `group` |
| `br` | an empty label | blank-line spacer, height = one text line; meaningful as a child of a column |
| `toggle` | `toggle` | `checked` = presence |
| `gauge` | `gauge` | `min` / `max` / `value` |
| `knob` | `knob` | `min` / `max` / `step` / `value` |
| `trend` | `trend` | sized with `width` / `height` |
| `meter` | `progress_bar` | degraded stand-in (HTML meter is a horizontal scalar); `min` / `max` / `value` |

## Whitelist — attributes

| Attribute | Applies to | Meaning |
|---|---|---|
| `id` | any element | `find_by_id` handle (unquoted digits accepted, stored as decimal — the `.ui` rule) |
| `style` | any element | inline declaration list, wins over every rule |
| `class` | any element | accepted, inert (no selectors) |
| `min` / `max` / `step` / `value` | gauge / knob / meter | the widget's range/value properties (integers, tolerant) |
| `checked` | checkbox / radio / toggle | boolean, by presence |
| `group` | radio | radio group id (integer) |

## Whitelist — CSS properties (inline `style=` and `<style>` rules)

| Property | Values | Mapping |
|---|---|---|
| `display` | `flex`, `block`, `none` | `none` → `visible=false`; otherwise the element is a flex container (block ≡ flex) |
| `flex-direction` | `row`, `column` | container type of a `div` |
| `width` / `height` | `Npx`, `N%` (1..100), `auto` | `Npx` → existing pixel size; `N%` → the `"N%"` percent form (FlexPanel parent content box); `auto` → absent (measured) |
| `flex` | `N` (integer) | `flex_grow` |
| `gap` | `Npx` (single value) | `spacing` |
| `padding` | `Npx` (single value) | `padding` |
| `flex-wrap` | `wrap` | `wrap=true` |
| `background-color` | `#rgb`, `#rrggbb`, named subset, `transparent` | the shared `background` property (see design-file) |
| `color` | same color forms | the shared `color` property (text color) |
| `font-size` | `Npx` | **parsed and ignored** (no per-widget size seam; deferred with a future `set_font_size`) |

## `<style>` rule matching

- A rule set is a flat list of `selector { declarations }`. Selectors are
  exactly a tag name (`button { … }`) or an id (`#status { … }`) —
  nothing else.
- Application: inline `style=`, then `#id` rules, then tag rules, each
  bucket in document order (the last matching declaration wins). No
  cascade, no inheritance, no specificity — a child never inherits a
  parent's `color`.

## Deliberate deviations from HTML

- `div` is a flex container, not a block box; `display: block` and
  `display: flex` are identical.
- No text flow: `p`/`span` are single-line labels, `br` is a one-line
  spacer — real paragraph reflow waits for H-1.
- Entities are only the five named above.
- `meter` is a `progress_bar`.
- Widgets are the presentation: alignment, focus, and interaction follow
  the widget, not CSS.

## Cross-cutting invariants

- **Static by contract** (the design-file rule): no JS, no dynamic
  behavior, no callbacks in the document. Event wiring happens in C++
  after materialization via `find_by_id`, exactly like `.ui`.
- **Determinism unchanged** (§4.11): the parse runs once at load; nothing
  in the path introduces timers, threads, or background work.

## Adding to the whitelist

A new element or property is a contract change: add the row here (and in
the design-file property table when it is a shared property), implement
the mapping and tag-table entry, lock it with a test — the `ui_builder`
tag-table precedent. Follow-ups H-1..H-7 each grow the whitelist.