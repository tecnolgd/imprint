# Security Policy

## Supported versions

Imprint UI is a rendering runtime: it draws into a caller-owned pixel
buffer and has no network surface, no file I/O of its own, and no
privileged operations. Security-relevant defects are therefore almost
always about **malformed input reaching the rasterizer or a parser**,
not about remote exploitation.

| Version | Supported |
|---|---|
| latest release tag | yes |
| `main` | yes |
| older tags | no |

## Reporting a vulnerability

**Please do not report security issues through public GitHub issues,
discussions, or pull requests.**

Instead, use GitHub's private vulnerability reporting:
**Security → Report a vulnerability** on
[`tyouhyou/imprint`](https://github.com/tyouhyou/imprint/security/advisories/new).
If that is unavailable to you, open a discussion and ask for a private
channel — do not include details in public text.

Please include:

- the affected version or commit,
- the input that triggers it (a design file, HTML fragment, image, or
  the exact input-event sequence),
- the observed effect (crash, hang, out-of-bounds write, unexpected
  allocation), and
- anything you have already ruled out.

## What counts as in scope

- **Parser/rasterizer robustness.** The HTML subset, the `.ui` design
  file, SVG path data, the image codecs (PNG/JPEG via the vendored stb,
  the GIF writer) and the terminal input parser all consume
  attacker-influenced bytes. Depth caps, magnitude caps and budget caps
  are load-bearing; a missing one is a defect.
- **Untrusted-input handling through the C ABI** (`binding/`): the ABI is
  the boundary a foreign host crosses, so its string handling, length
  checks and exception containment are in scope.
- **Memory safety** in `imcore` (the software rasterizer and codecs) and
  `imui` (the widget tree), including 16bpp/32bpp build variants and the
  non-atomic-`SharedPtr` branch.
- **Determinism breaks that mask other bugs** — a frame that depends on
  uninitialized memory or on the environment is a defect even when it
  renders "fine".

## Out of scope

- The demo apps under `apps/` and the host examples under `demo/`: they
  are demonstrations, not attack surface, though a crash caused by a
  framework bug they exercise is in scope.
- A host's own handling of its framebuffer or event queue.
- Declared non-goals (see `docs/ARCHITECTURE.md` §5): no GPU
  acceleration, no animation system, no runtime backend switching, no
  multi-threaded rendering, no IME, no RTL/bidi.

## Response

This is a single-maintainer project, so expect a first response within a
few days rather than hours. You will get an acknowledgement, an
assessment, and — once a fix lands on `main` — credit in the release
notes unless you prefer otherwise.
