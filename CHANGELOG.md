# Changelog

All notable changes to Imprint UI are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this
project adheres to [semantic versioning](https://semver.org/) at the
level its pre-1.0 state allows: `v0.1.x` tags are patch releases of the
0.1 line.

The two released tags are summarized below; the full history (480+
commits) lives in `git log`.

## [Unreleased]

### Added
- **Packaging.** Both shared libraries (`libimcore`, `libzbapi`) now
  carry a `SOVERSION`, so an in-place upgrade cannot silently replace a
  library a linked binary still uses. `cmake --install` also ships
  `LICENSE` under `share/imprint/` and a pkg-config recipe
  (`lib/pkgconfig/imprint.pc`) for non-CMake consumers of the C ABI.
- **A format gate.** `tools/format-check.sh` checks every first-party
  file against `.clang-format` with a pinned clang-format (18.1.3),
  reformats in place with `--write`, and runs as a CI step.
- **Shared shell seams.** `shell/device_overlay.hpp` gains
  `for_each_bar()` (the chrome-bar iteration every desktop shell
  hand-rolled) and `shell/presenter.hpp` gains `painted_region()` (the
  dirty-region → present-region read, which also mechanizes the
  evaluation-order rule that once shipped as a black X11 window).
- **`zb::input::decorate_printable_key`** — the printable-character half
  of the U-1 key rule, now a single home that every C++ input translator
  calls instead of re-deriving the letter/digit classification.
- `CONTRIBUTING.md`, `SECURITY.md`, this changelog, and a CI status
  badge in all three READMEs.

### Changed
- The project version no longer trails the released tags
  (`find_package(imprint 0.1.1)` now resolves against this tree).
- `Event::subscribe()` is `[[nodiscard]]`: discarding the guard
  unsubscribes on the same statement, which is never what a caller
  means.
- `core::basic_color` no longer puns the pixel word through a union. The
  32bpp channel byte view goes through `memcpy` of the object
  representation — the same memory byte order, without the one
  load-bearing implementation-defined construct in the tree. Object
  size is unchanged (still exactly `depth/8` bytes).

### Fixed
- **SIXEL log flood.** The sixel presenter logged one warning per
  *pixel* once a band's 250 palette registers were spent; it now latches
  once per frame. The header and `docs/ARCHITECTURE.md` §5 also state
  the real behavior — a color past the budget has no register and is
  dropped from the band — replacing the old "maps to the nearest
  registered color" promise, which the code never implemented.
- **Python host key codes.** `demo/python/myapp.py` filled only the
  character channel, so its keys carried no platform-independent key
  code; it now applies the U-1 rule the C++ translators use.
- **SVG rasterizer cost.** Every rasterizer walk is now intersected with
  the draw area at the entry, so a design file declaring a huge stroke
  width or radius costs surface-area work instead of its own: one
  `stroke-width:999999999px` line on a 2^20 px canvas used to add ~330 s
  to the HTML suite. `SvgCanvas::walk_pixel_count()` exposes the
  deterministic cost the battery asserts on.

### Documentation
- `docs/ARCHITECTURE.md` §5 states the widget/container gaps (no grid,
  no table/tree/tab/menu container, no accessibility or text-selection
  surface) and the design-file image gap (PNG/JPEG decode exists in
  `imcore`, but the HTML whitelist has no `<img>` and skips `url()`;
  GIF is encode-only) as boundaries rather than omissions.

## [0.1.1] — 2026-09-02

### Fixed
- Touch glitch debounce.
- TTF advance calculation.
- GIF encoder block terminator (strict decoders rejected the output).

### Changed
- Centered button text.

## [0.1.0] — 2026-08-31

First release: the deterministic software-rasterizer UI runtime with the
C ABI, the design-file path, the vendored stb codecs, and the first
platform shells.

[Unreleased]: https://github.com/tyouhyou/imprint/compare/v0.1.1...HEAD
[0.1.1]: https://github.com/tyouhyou/imprint/compare/v0.1...v0.1.1
[0.1.0]: https://github.com/tyouhyou/imprint/releases/tag/v0.1
