# Imprint — Backlog

> Living backlog document. Tracks active, condition-triggered, and unscheduled
> work items across architecture and product layers. Release/promotion status
> is operator state, not roadmap — it lives in the local handoff notes, not
> here.
> Architecture contracts live in [`docs/ARCHITECTURE.md`](ARCHITECTURE.md);
> API contracts live in [`docs/code-contract.md`](code-contract.md).
> Completed items are removed upon completion (A-numbering is stable, gaps
> represent finished work; history lives in `git log`).

## 0. Execution Order (2026-09-10 map; completed steps removed; A-26 added 2026-10-03)

Agreed sequence — a map through the backlog, not a new state machine.
Dependency-driven: each tier unlocks what follows.

1. **A-26. imcore primitive throughput** — the open item is ② (SIMD
   inner loops); its decision point is the fps re-measure (fps first
   migrates to `build(host, node, sink)`).
2. **Explicitly NOT now**: F-1/F-2, I-1, V-4, A-4/A-21, D-*, A-27, and
   Batch W (audit follow-ups, decision-gated). Condition-triggered
   items stay trigger-gated. Batch G's P1–P4 roadmap is landed
   (2026-09-26 through 2026-10-01; history in `git log`).
3. **Batch X** (2026-10-15 independent audit): the audit's recommended
   order is inside the batch. What remains is X-10…X-21 (the
   hardening/bookkeeping tail, plus the three X-8 follow-ups — the
   tree-wide `clang-format` reformat, the `-Wconversion` triage and the
   local Windows-TU compile probe).
   X-7 and X-9 landed (2026-10-19: a real X11 loop under Xvfb in CI, and
   one shared 64-bit geometry helper); **X-8 landed the same day** (a
   per-compiler warning set with `-Werror` in-tree, clean across the
   whole build matrix; `cmake/imprint-warnings.cmake` is its home).
   Nothing in the batch reopens a
   ratified ruling; sequencing stays the operator's call.

### Batch V — Visual Presentation (open remainder only)

Goal: close the "works but looks primitive" gap against other UI
frameworks. Completed rasterizer/showcase/gif/dashboard work: see `git log`.

- **V-4. codec: memory-source image decode** (condition-triggered):
  `Image::read_png*` accepts only file names; embedded targets shipping
  compressed art need `read_png_memory(bytes, n, ...)` (stb offers
  `stbi_load_from_memory`). Gated together with the USE_PNG
  default-OFF question — decide both when the first real
  compressed-asset use case appears; until then the procedural
  generator covers the demo.

### Batch H — HTML/CSS Rendering Path (landed end to end; open remainder only)

An optional declarative smooth-path that renders an HTML/CSS **subset**
(no JS) through the existing widget tree, complementing the `.ui` design
file. Landed end to end — whitelist contract, parser + battery, preview
consumer, `showcase_html` on desktop / NDS / wasm (history in `git log`).

**Hard support boundary — the whitelist is the contract (rewritten 2026-09-12).**
The element/attribute/CSS-property whitelists in `docs/html-path.md` are
the single source of the boundary: **anything not in the table constructs
nothing** (LW + content dropped) — no blacklist to maintain. This doc holds
only the genuinely behavioral decisions, which are layout-engine scope, not
parser scope:

- **`div` block semantics are deliberately simplified**: `div` maps to a
  flex container (content-measuring FlexPanel), not HTML block layout; a
  "block" div does **not** stretch to fill its parent's main axis (flex
  markup drives fill). See **`docs/html-path.md`** for `display: block` /
  `display: flex` (`block` keeps `column`; bare `flex` selects `row` —
  not treated as equivalent).
- **No second layout engine**: no `position: absolute/fixed`
  (no offset/z-index/overlap — abs/relative landed as P-3 for the HTML
  path's own model), no grid /
  multi-column / RTL / bidi — each is a container/scroller enhancement,
  not parser work.
- **CSS flex features**: `justify-content`, `align-items`/`align-self`,
  `flex-basis`, `flex-shrink`, and `flex` min/max constraints map to
  FlexPanel enhancements — landed (H-7, D-1; history in `git log`).
- **`border-bottom` is not whitelisted** (only `border-top` is). A header
  underline therefore has to be drawn as `border-top` on the *following*
  element. Adding the real property needs a second band slot in the
  widget ext struct (`bord_t` today holds one) plus the draw path and its
  16bpp/32bpp variants — widget-side work, not parser work. Found
  2026-10-10 when the build-time gate went live; the showcase design file
  works around it.
- **`flex` grow must be an integer** (`parse_int_value`): `flex:1.2` warns
  and leaves the node's default, which *silently starves the panel*
  rather than approximating. The design file shipped two of these
  (`flex:1.2`, `flex:1.3`) unnoticed. Decide: keep integer-only but make
  the fallback the inherited value (or `flex:1`) instead of the node
  default, or accept fractional grow.

**Placement note (decided 2026-09-12):** no `IMPRINT_WITH_*` switch needed —
per the A-23 precedent the `html` translation unit stays in imui (STATIC);
unreferenced on targets that don't use it, dropped by the static linker at
image build. Consumer-side, not a new C-ABI surface at this stage.

**Open / condition-triggered sub-items:**
- P-2. Paint remainder (gated by a real page): follow-ups that still
  matter: document-width roots are not centered by UiPreview (amp runs
  full-bleed); body gradient backgrounds have no `html_page` carrier
  (H-8 holds colors only); GIF review captures band smooth ramps (216-cube) —
  review from the raw framebuffer.

### Batch I — Tooling & Inspection (Unscheduled)

- **I-1. Hot reload for design file previewer (`apps/ui_preview`)**:
  - Watch `.ui` file changes on disk and reload in-place without restarting the previewer.

### Batch W — Full-repo Audit Deferred Items (added 2026-10-03; decision-gated, not scheduled)

Follow-ups from the 2026-10-03 full-repo audit (`reports/review-1.md`,
local-only). Verified real, deferred by operator ruling; each entry states
the decision it is waiting on. **A missing number is a fixed item** —
history in `git log`.

- **W-1 (IM-RAS-002). Arc shape depends on alpha**: a translucent arc
  (0<a<0xFF) takes the exact-chord octant scan, so its silhouette changes
  with alpha. Fixing means one rasterizer for all alphas — a rendering
  change that re-baselines every arc golden. Waiting on: accept as
  documented deviation vs. unify.
- **W-2 (IM-IN-002). `ZB_INPUT_MOUSE_*_CLICK` are advertised but the
  dispatcher silently drops them**. Waiting on: document as inert host-side
  synonyms vs. remove from `zbapi.h` (C-ABI surface semantics).
- **W-3 (IM-IN-005). Terminal translator routes Space only through `ch`,
  so Space can never activate the focused widget on the SIXEL target**.
  The fix is a translator behavior change needing a fresh NDS/SIXEL
  verification pass; bundle with the next melonDS re-verify.
- **W-7 (IM-BLD-007). `demo/wasm/build.sh` hand-maintains its source
  list** (stale vs. `imui/CMakeLists.txt`, different font-subset inputs).
  Currently sufficient (the smoke passes); regenerating the list needs a
  maintain-vs-generate decision.
- **W-8 (IM-BLD-008). No symbol-visibility control** —
  `CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS` exports the whole imcore surface,
  not just the `zbapi.h` stable ABI. Waiting on: a visibility policy
  (export macros vs. `--exclude-libs` vs. accept).
- **W-9 (IM-BLD-009). No `CMAKE_MSVC_RUNTIME_LIBRARY` pin; the
  installed-package smoke is Linux-only** — the Windows install path
  (classic LNK2038) is unverified. Needs a Windows CI runner decision.
- **W-10 (IM-BLD-011). The ASan job does not instrument the
  shared-library link line.** Waiting on: a CI touch to add the
  shared-link sanitizer flags.
- **W-11 (IM-BLD-013). `asset_gen` is built by every configuration and
  consumed by nothing; committed PNG/GIF assets have no documented
  regeneration path.** Waiting on: wire asset_gen, drop it, or document
  the regen recipe.
- **W-12 (IM-DOC-004). §8's alloc exemption is premised on "the battery
  runs Debug"** — no Linux CI job sets a build type, so `DEBUG` is
  undefined there. Waiting on: add `-DCMAKE_BUILD_TYPE=Debug` to the
  Linux jobs vs. amend §8's premise.
- **W-14 (IM-HOST-003 remainder). Shell/ABI hardening, five sub-items**:
  `run_win` assigns
  global `g_app` before a throwing `CreateWindowEx`; the C-ABI
  `catch(...)` handlers log via the allocating `LE` path (OOM can throw
  out of `extern "C"`); no DPI
  awareness on win/mac; `maps_pointer()` omits `touch_*`; no C-ABI
  re-entrancy guard. Each is small; the set needs a hardening-vs-accept
  ruling (some, like allocating loggers, are the documented design).
  (The sixth — "shell `main`s catch only `zb::ui::error`" — landed with
  X-5, 2026-10-17: every framework-mode `main` now runs through
  `zb::shell::guarded_main`.)

### Batch X — Independent Audit (2026-10-15; audit-ordered, mostly decision-free)

Origin: a full-repo independent audit at `efe684a`, verified by one clean
`desktop-test` run of its own (60/60 suites, 361 s) — so every item below
sits outside what the current battery covers or exercises. Detail ledger:
`reports/audit-2026-10-15.md` (local-only, the same convention as Batch W),
which also carries the audit's strengths list, the positioning/demo
proposals and one *withdrawn* finding. Ordered by the audit's recommended
priority; no item reopens a ratified ruling.

**Build and test coverage gaps:**

- **X-19. Tree-wide `clang-format` reformat.** `.clang-format` landed
  with X-8 (2026-10-19) as the style reference for new and edited code;
  the measured reformat of the whole tree (clang-format 18.1.3: 137 of
  240 first-party files drift, ~6.3k diff lines) was deliberately
  deferred so it does not bury unrelated history. Do it as one mechanical
  commit on a pinned clang-format version, then add a version-pinned
  format gate so the drift cannot come back.
- **X-20. `-Wconversion` / `-Wsign-conversion` triage.** Measured
  2026-10-19 against the X-8 baseline: 308 findings, dominated by benign
  sign/index conversions (`imui/include/widget.hpp` clamp `:703`,
  `e->text_flags &= ~1` `:966`). Not adopted with X-8 — the two flags
  would have to be clean first or the real findings drown. Triage by
  fixing (or proving) the sites, then add both to the X-8 set.
- **X-21. Windows-only TUs have no local verifier.** `imshell/src/win/`
  (`run_win.cpp`, `win_input.cpp`, `main.cpp`) is compiled by no local
  target, so the warning set (shadow, sign-compare, unused, syntax)
  reaches the tree only through the `windows-desktop` job: the C4456 at
  `run_win.cpp:183` (2026-10-19 — a chrome-bar `RECT r` shadowing the
  presented rect) was that class's first catch. Either remedy is enough:
  a `windows.h` stub + clang syntax probe (the ObjC++
  `build/objcpp_probe/` precedent) or a compile-only mingw-w64 target
  (`g++-mingw-w64-x86-64` is packaged). Not a CI-correctness
  prerequisite: the job already rebuilds with `-DIMPRINT_WERROR=OFF` and
  lists every diagnostic after a failed gate.

**Hardening (latent, evidence-backed) and bookkeeping:**

- **X-10. 16bpp presenter enforcement is asymmetric**: X11
  (`run_x11.cpp:28-31`) and mac (`run_mac.mm:356-359`) throw, Windows
  carries a NOTE only (`run_win.cpp:190-197`) → abgr1555 blitted into a
  555 DIB with no diagnostic; separately the fb 32bpp fast path
  (`fb.cpp:156-159`) validates byte count but not channel masks, while
  the 16bpp branch below it validates both.
- **X-11. The Python host decodes 16bpp as RGB565** (`myapp.py:124-135`)
  while the contract is abgr1555 (`zbapi.h:19`,
  `pixel_traits.hpp:71-82`) → red and blue are swapped in that host, and
  `python-host` is Tier-3 and never covers 16bpp. Decide: fix the host or
  add the 16bpp leg to the CI job.
- **X-12. Three untrusted-input caps**: CSS `var()` expansion is
  depth-capped at 8 with no output-length cap (`html.cpp:1189-1300`,
  O(k^8) from a ~2 KB sheet); path data has no command/point cap
  (`html.cpp:3410-3670`) and flattens up to 4096 points per curve
  (`svg_canvas.cpp:85/128-162`) → ~2000× input amplification; terminal
  CSI accumulation is uncapped (`term_input.cpp:235`). The existing depth
  caps (`html.cpp:5012`, `ui_file.cpp:137`) are the model to extend.
- **X-13. SIXEL log flood and a doc/code mismatch.** `LW` fires inside
  the per-pixel loop once the register budget is spent
  (`sixel.cpp:92-104`) → one line per dropped pixel on a photo-like
  frame; `imshell/include/shell/sixel.hpp:17-18` promises nearest-colour
  mapping while the code drops the colour from the band. Needs a
  warn-once latch plus a wording (or behavior) fix.
- **X-14. Release/packaging hygiene**: no `SOVERSION` although imcore is
  deliberately SHARED (A-4.2) → an in-place upgrade silently replaces a
  library other binaries linked against; `project(VERSION 0.1.0)`
  (`CMakeLists.txt:3`) vs released `v0.1`/`v0.1.1` →
  `find_package(imprint 0.1.1)` fails; no pkg-config/vcpkg/Conan recipe;
  LICENSE not installed.
- **X-15. Shell duplication vs. the A-2 promise**: the chrome-bar fill is
  written three times (`run_win.cpp:178-186`, `run_x11.cpp:223-231`,
  `run_mac.mm:291-300`) and the painted→present handler about five times,
  so ARCHITECTURE §3.1's "~100-200 lines of glue per new target" stays
  aspirational; the U-1 key-decoration rule has six copies, three of them
  untested (mac, `demo/wasm/main.js`, `demo/python/myapp.py`).
- **X-16. Feature-gap orientation — pointers, not a restatement.** The
  declared non-goals are normative elsewhere (ARCHITECTURE §5 for the
  runtime limits, Batch G for the widget-armament race, Batch H for the
  layout-engine boundary); what is in none of those lists and a scanning
  user will miss: no grid layout, no table/tree/tab/menu-style containers,
  no accessibility, no text selection or horizontal scroll, **no `<img>`
  and `url()` skipped** (so a design file cannot reference an image
  although PNG/JPEG decode exists — `docs/html-path.md`), GIF encode-only.
  Decision if any of it enters the roadmap: grid + `<img>` + an ABI
  clipboard channel.
- **X-17. Hygiene, cheap and high-signal**: README has no CI badge; no
  CONTRIBUTING/SECURITY/CHANGELOG; the local dev container directory
  `docker/` (devkitARM/emsdk image, the toolchain the cross-target builds
  were developed in) is gitignored (`.gitignore:70`), so a clone cannot
  reproduce the development environment; `pixel_traits.hpp:95-106` puns
  the pixel word through a union (the one load-bearing
  implementation-defined construct — `std::bit_cast`/`memcpy` would make
  it standard-clean); `event.hpp:131` `subscribe()` returns an RAII
  registration guard without `[[nodiscard]]`, so discarding it unsubscribes
  on the same statement.
- **X-18. The SVG rasterizer walks the DECLARED geometry, not the draw
  area** (found by review while landing X-6; not a regression, not in
  X-4/X-5/X-6's acceptance). X-6 bounds every coordinate at ±16000 px,
  but the walks are still sized before any clip: `draw_line_stroke` steps
  the segment's bbox expanded by `dev_w` (`svg_canvas.cpp:602-615`), the
  shape fill the box its Q10 radii span (`:1047-1059`), and
  `stroke_points` / the path fill do the same (`:849`, `:1134`) — a
  16000 px stroke band or radius on a 64 px surface is ~1e9 plot calls
  per shape. Measured: one `stroke-width:999999999px`
  line on a 2^20 px canvas (a 1x1 viewBox multiplies the width into the
  coordinate bound) added ~330 s to the html suite. Bounded, but a
  stalled frame from a hostile design file. Fix shape: intersect each
  walk with the draw area (surface-clamped by construction, the X-5
  inset-ring precedent) at the rasterizer entry; needs a cost mechanism
  the battery can assert on (wall-clock tests are flaky), so it is not a
  silent edit.

### Batch F — Event Loop Extension & Frame Automation (Long-term)

- **F-1. Cross-thread message posting `zb::ui::post(closure)`**:
  - Contract-first design before implementation.
  - **Invariants to preserve** (normative in [`docs/ARCHITECTURE.md`](ARCHITECTURE.md) §4.11):
    - Single-threaded driving semantics at the core.
    - Deterministic frame ordering for automation and headless runners.
    - Observable `painted` synchronization signal.

- **F-2. Frame automation helper (`Tween` / pacer)** (added 2026-09-05;
  ruling refined the same day: app-side frame automation is allowed,
  widget-built-in tween / a framework animation scheduler remain
  non-goals):
  - `Tween` is a pure value interpolation (from / to / duration /
    easing), a pure function of the frame index — no threads, no core
    state; the app advances it in its painted callback and invalidates.
  - Pacing belongs to the host (desktop invalidate loop / WASM rAF /
    NDS vblank); the helper owns only the math and invalidation
    requests, keeping the A-2 seams untouched. Single-threaded by
    contract; F-1 remains the separate cross-thread track.
  - Contract-first design before implementation; first lands as
    showcase demo glue (V-2 hero), promoted to an optional helper
    beside `imapp_canvas` when a second user appears (§2 tool-placement
    rule).

### Batch G — Positioning & Go-to-Market (active roadmap; unfrozen 2026-09-26)

Origin: a third-party commercial review plus the maintainer assessment
of 2026-09-06 (deliberately deferred then — technical validation first).
Unfrozen 2026-09-26 after a positioning brainstorm (third-party advisory
fragments + a full-code re-read); the rulings below supersede/extend the
09-06 record, whose discussion history lives in `git log`.

**Standing rulings (2026-09-06, unchanged):**

- Lead with the already-delivered differentiator — a deterministic,
  pixel-testable UI runtime — not "another cross-platform C++ GUI
  framework"; do not lead with "embedded" (NDS is entertainment-class;
  the embedded claim stays partially earned until a real MCU-tier
  footprint exists). Executed at README level 2026-09-22 (three
  languages).
- First revenue path: per-target port engagements (display / input /
  font glue for a customer's board); SDK/enterprise licensing and any
  designer product are later-stage (a designer would reverse the
  "no drag-drop designer" ruling).
- Rejected directions (with reasons): Figma import (free-form canvas →
  constraint-layout mapping is unmaintainable; the viable variant is
  LLM-generated `.ui` files, which the grammar already supports);
  "AI-agent-friendly runtime" as a *commercial wedge* (circular —
  agents drive pre-existing GUIs); hardware-SDK-vendor sales at the
  validation stage.
- Standing constraint: single-maintainer bus factor outweighs star
  count; the first external committer matters more than stars.

**2026-09-26 rulings (positioning brainstorm, user-ratified):**

- Positioning confirmed: "embeddable deterministic UI runtime". Moat
  order (hardest to copy first): determinism-as-contract → zero-
  dependency + C ABI → HTML-subset design file (designer-friendly and
  inherently sandboxed) → any-target portability (NDS as the proof) →
  compile-time pixel model. Do not enter the widget-armament race
  (the LVGL / Slint / Qt dimensions: more widgets, faster, prettier).
- Roadmap P1–P4 — landed 2026-09-26 through 2026-10-01 (external
  consumability incl. P1.5 install/export, deterministic-test story,
  declarative plugin protocol, SIXEL terminal target; contracts in
  ARCHITECTURE §3.2/§4.8/§4.10/§11.3 and code-contract §4/§12; history
  in `git log`). Open remainders:
  - *P3.1 (condition-triggered): typed state read/write through the
    ABI when a real host needs it — the native per-widget events
    already carry the payloads for C++ consumers.*
  - *P4 extension (unscheduled): the Kitty graphics protocol as a
    second presenter for the same input source.*
- Business stance reconfirmed: port engagements first; external
  consumability is itself the revenue enabler. The 09-06 landing page +
  "tell us about your device" intake is closed: the portal at
  `tyouhyou.github.io/imprint/` is the landing page, and lead handling
  is operator state, not roadmap.

**2026-10-15 audit proposals (unratified — operator decides).** The
independent audit's shareability findings, product-side only; promotion and
distribution stay operator state per this doc's header. Evidence and
rationale in `reports/audit-2026-10-15.md` §5.

- *G-a.* An `imprint-agent` MCP server over the existing design-file path
  (render → frame hash/PNG → pixel assert → baseline diff) plus a recording
  of an agent fixing its own layout from the render — turns the README's
  "Made for AI agents" claim into a runnable artifact.
- *G-b.* A six-target byte-identical CI montage plus a determinism badge:
  one action renders the same design file on Linux/FB/mac/Win/WASM/SIXEL/NDS
  and byte-compares — the shortest path from the slogan to visible evidence.
  Adjacent to "one seed, three screens" and the SIXEL recording in §1's
  deferred list.
- *G-c.* README first screen: a GIF in the first three lines (the current
  static hero reads as a mockup until two paragraphs in) and a 30-second
  quickstart that does not need libx11-dev — sequence after X-3.
- *G-d.* A ~200-line port template plus one real board shot end to end —
  the missing proof for the "embedded" tag this batch itself marks as
  partially earned.

## 1. Architecture Backlog

### A-4. Smaller Items

- **A-4.1 Singleton lifetime**:
  - `Widget` shares one process-wide `BitmapProvider`, intentionally leaked so widgets outliving `main` never touch a dead provider.
  - Revisit if a target ever needs an explicit teardown/shutdown lifecycle (must stay stateless while shared — see `widget.hpp`).
- **A-4.2 Shared/static duality of `imcore`**:
  - Kernel is built as CMake SHARED library (for dynamic host loading on Linux) yet statically linked into embedded ROMs and tests.
  - A packaging abstraction would simplify new target integration.

### A-21. Retire the non-atomic `SharedPtr` branch (Condition-triggered)

- Today every non-embedded build uses `std::shared_ptr` (`zb::SharedPtr` is an alias); the ~150-line non-atomic implementation exists only for targets without atomics (NDS ARM9: devkitARM ships no libatomic). Its semantics are locked by `test_ptr.cpp` (compiled against the custom branch on the host) and the CI non-atomic matrix job runs the whole battery against it.
- **What is deferred:** Collapsing the duality — either `std::shared_ptr` on the NDS too (needs a toolchain decision: `__atomic` support on arm926ej-s / shipping a libatomic) or an intrusive refcount owned by the objects themselves. Both are ABI-adjacent changes with no current payoff.
- **Trigger:** Act when the custom branch needs a real fix again, or when a second non-atomic target appears; until then the tests keep it cheap to carry.

### A-25. Host-shaped glyph feed (text "Level 2", condition-triggered)

- The `GlyphProvider` contract is per-`char16_t` (no cluster/advance
  model), so a host cannot feed pre-shaped text (HarfBuzz-grade
  shaping, dynamic complex-script text, emoji) through any seam, and
  the C ABI has no glyph exports at all. Static label text is covered
  by the build-time subset (`tools/font_subset.py` + `ttf_subset`);
  desktop dynamic text by `USE_TTF_RUNTIME`.
- **What is deferred:** extending the glyph contract with a
  cluster/advance (or host glyph-feed) shape — contract-first, touches
  code-contract §2.4 and possibly ARCHITECTURE §4.8.
- **Trigger:** a real embedding needs dynamic complex-script text the
  subset + runtime-TTF paths cannot serve. IME and RTL themselves stay
  declared non-goals (the host owns them).

### A-26. imcore primitive throughput (② SIMD; gated on the fps re-measure)

- **What remains for ② SIMD:** the no-clip remainder is the per-pixel
  blend/modulate arithmetic itself (fill 5.2 ms, `draw_image` 6.9 ms,
  tinted 13.1 ms per half/full-screen unit) — exactly the SIMD
  territory. wasm simd128 first (where 1080p consumers run), then
  desktop SSE2/NEON. **Trigger:** the fps re-measure (fps must first
  migrate off the deleted `bind_actions` API to
  `build(host, node, sink)`) shows the 1080p frame budget still missed.
- **Deferred within ①:** diagonal `draw_line` gate hoisting — the
  Bresenham walk is inherently per-pixel (15 ms per 1920 lines locally)
  and not the fps bottleneck; revisit only if fps evidence says
  otherwise.
- **Hard constraints (unchanged for ②):** byte-identical determinism —
  every step keeps `zb::snap` `framebuffer_hash` / CI byte-compare
  green **and** passes the 16bpp battery (`.ai/bin/verify.sh
  desktop-test-16`; SIMD rewrites are the classic way to break the
  `plot_aa` quantization rule). Hot paths stay integer
  (8-bit-normalized accessors, no floating point).
- Tile-threaded rasterization stays a declared non-goal (README,
  ARCHITECTURE §5, CONTEXT ruling): internal workers would not break
  determinism (disjoint tiles, integer math) but would re-open three
  ruling texts, need SharedArrayBuffer + COOP/COEP on wasm and a
  compile-out on NDS. Re-evaluate only if ② still misses the native
  1080p frame budget.

### A-27. C-ABI present-region export (`zb_present_region`) (condition-triggered)

- `zb_buffer` (`binding/include/zbapi.h`) exposes the whole frame only,
  so every zbapi host presents full-buffer; the A-2 seams
  (`region_to_present`, `dirty_coalescer` in
  `imshell/include/shell/presenter.hpp`) already compute the present
  rect internally — only the ABI hop is missing.
- **Shape:** contract-first (code-contract §9 + ARCHITECTURE §4.8
  first, then `zbapi.h`): export the current frame's present rect so
  hosts copy/upload only the changed region — the same seam that would
  carry a persistent GPU-texture upload ("GPU-accelerated
  presentation", the permitted form per the ARCHITECTURE §5 ruling).
- Zero benefit for fps (full-frame game repaint; source-level
  composition that does not use zbapi).
- **Trigger:** a zbapi host's present path becomes a measured
  bottleneck (wasm story demo, Python, a future Kitty presenter), or
  the wasm demo wants region-wise upload.

### Deferred this round (recorded, not blocking)

- **B10-style constant C-ABI exports** (`zb_buffer_bpp` etc.): optional
  `try/catch` for file-wide no-exception-crossing style consistency;
  low priority, no throw path today.
- **"One seed, three screens"** (from the 2026-10-03 gh-pages portal
  round): the seedmap page's map rendered side by side from the same
  seed on wasm + NDS + desktop, as a living determinism proof. The
  generator is already byte-deterministic (locked by the seedmap smoke);
  what remains is the NDS capture and the portal wiring.
- **SIXEL terminal live recording**: a GIF of a P4-terminal shell
  session recorded via `script(1)` pty, hosted on the portal next to
  the wasm pages — shows the Linux SIXEL backend without a terminal
  emulator dependency. Needs a recording pass, no framework change.

### Unscheduled Design Debt (Batch K Triage)

- **D-3**: Focus navigation history.
- **D-4**: `Event` once-handler and priority handlers.
- **D-9**: Centralized resource management.
