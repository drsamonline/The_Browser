# Handover — Aetheris Browser

**For:** any session or agent picking this up
**Repo:** `drsamonline/The_Browser`
**Owner:** Dr Sohil Momin
**Date:** 16 November 2026
**Latest tag:** v0.6.2

Read this before changing anything. It records what works, what does not, and
the mistakes that cost the most time.

---

## 1. What this is

A minimal, from-scratch browser engine in C++20 — HTML5 parsing, CSS cascade and
layout, JavaScript, its own rasteriser, and a compact SDL2 window. It is **not**
a Chromium/Gecko/WebKit wrapper.

| Property | Value |
| --- | --- |
| Licence | MIT |
| Language | C++20 |
| Size profile | ~2.5 MB stripped (report-only, never enforced) |
| Tests | 57 CTest suites, all passing |
| Platforms | Linux (verified), Windows (verified at runtime 16 Nov) |

Vendored: `third_party/gumbo` (Apache-2.0, HTML5 parsing),
`third_party/quickjs` (MIT, JS engine). Embedded DejaVu Sans subset for text.
All credited in `CREDITS.md`.

---

## 2. Current state

### Works

- HTML5 parsing (gumbo) → DOM, with scripts, styles and implied tags
- CSS: cascade, specificity, media queries, structural pseudo-classes,
  `transform` (translate/scale), user-agent stylesheet, length units
  (px/em/rem/%/pt), **shorthand properties** (`margin`, `padding`), `margin: auto`
- JavaScript: QuickJS, events (capture/bubble), timers, `requestAnimationFrame`,
  DOM API (`innerHTML`, `style`, `dataset`, `value`, `checked`, `submit`)
- Text: TrueType parsing, anti-aliased rasteriser, glyph cache, embedded font,
  1,218 kerning pairs
- Networking: keep-alive, cookies, TLS with **bundled CA file**, CSP for inline
  scripts, `SecureResourceLoader` on every fetch
- UI: 34 px chrome strip, tab session, input router, SDL2 window
- Releases: Linux tarball + Windows zip with all runtime DLLs

### Known broken / missing

| Item | Detail |
| --- | --- |
| Scrolling | No scrollbars; long pages unreachable |
| Shaping | No Arabic/Indic/ligatures; kerning only |
| `position: sticky` | Deferred — needs scroll state |
| `::before`/`::after` | Needs layout-level synthetic boxes |
| HTTP/2 | Deferred |
| Debug overlay newlines | `draw_debug_overlay` ignores `\n` |
| Tab click/close | Router emits actions; not wired to the session |
| `AetherisWindowHost` on Windows | Excluded from the release job — hangs in `SDL_Init` on a headless runner, unexplained |
| `AetherisHttpClient` | Flaky in the LTO size profile; excluded from the release gate only |

---

## 3. Build

The build needs a compiler. There is **no system compiler** in the sandbox, so
the zig wrappers are used:

```bash
pip install cmake ninja ziglang
# wrappers at /scratch/work/toolchain/zigcc and zigcxx (strip -Werror)
#             /scratch/work/tc_strict/zigcc and zigcxx (keep -Werror)

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=<zigcc> -DCMAKE_CXX_COMPILER=<zigcxx> \
  -DCMAKE_C_COMPILER_WORKS=1 -DCMAKE_CXX_COMPILER_WORKS=1 \
  -DAETHERIS_STANDALONE_BUILD=ON -DAETHERIS_ENABLE_TESTING=ON -DBUILD_TESTING=ON \
  -DOPENSSL_INCLUDE_DIR=/usr/include/node \
  -DOPENSSL_SSL_LIBRARY=/usr/lib/x86_64-linux-gnu/libssl.so.3 \
  -DOPENSSL_CRYPTO_LIBRARY=/usr/lib/x86_64-linux-gnu/libcrypto.so.3 \
  -DAETHERIS_WEBP_LIBRARY=/usr/lib/x86_64-linux-gnu/libwebp.so.7

cmake --build build --parallel
ctest --test-dir build --no-tests=error
```

Size profile: add `-DAETHERIS_SIZE_OPTIMIZED=ON`. The option is **not**
`AETHERIS_SIZE_PROFILE` — getting it wrong measures a different binary.

SDL2 backend: `-DAETHERIS_WITH_SDL2=ON` plus SDL2 installed.

### Verifying a change

- Check the **exit code**, never grep the output for "error". A missing compiler
  and a broken build both produce no matching lines.
- Compile with `-Werror` (the `tc_strict` wrapper). CI uses it; a plain build
  does not.
- Render a page and look at it: `aetheris --render-window <url> out.ppm`.

---

## 4. Releasing

Push a `v*` tag. CI (`.github/workflows/build-aetheris.yml`) builds and publishes:

| Job | Purpose |
| --- | --- |
| `test-standalone` | Linux build + CTest |
| `test-windows-standalone` | Windows build + CTest |
| `test-sdl2-backend` | Builds with SDL2, runs the window loop under `SDL_VIDEODRIVER=dummy` |
| `release-standalone` | Linux tarball + `cacert.pem` |
| `release-windows-standalone` | Windows zip + all DLLs + `cacert.pem` |
| `build-windows` | Full-tree, opt-in only |

**Verify the artifact, not the job colour.** Download the zip and list its
contents. Several releases went green with a broken or missing package.

---

## 5. Gotchas that cost the most time

1. **SDL pixel format.** Our surfaces are `0xRRGGBBAA`; on little-endian that is
   memory `A,B,G,R`. SDL names formats by the 32-bit *word*, so the correct
   constant is `SDL_PIXELFORMAT_RGBA8888`. `ABGR8888` reads the alpha byte as
   red — the chrome rendered bright red for three releases.
2. **`assert()` in tests.** Release defines `NDEBUG`, which compiles assertions
   away: tests passed silently and then failed to build under `-Werror`. Test
   targets now compile with `-UNDEBUG`.
3. **Windows packaging.** The exe links SDL2, OpenSSL and the MSVC runtime
   dynamically. Every one must be in the zip or the user gets `0xC0000135` with
   no explanation. Also: the runner's PATH has MySQL's OpenSSL before the real
   one — search explicit directories first.
4. **`shell: pwsh` exit codes.** GitHub's pwsh shell exits with `$LASTEXITCODE`;
   a `where.exe` miss leaves it at 1, so a successful step reports failure.
5. **Windows TLS.** `SSL_CTX_set_default_verify_paths` finds nothing. Ship
   `cacert.pem` beside the exe; that is the reliable fix.
6. **CSS shorthands and units.** `margin: 5em auto` was ignored entirely (only
   longhands were read) and `2em` was parsed as 2 pixels. Both fixed — but this
   class of gap is common: assume a property is unimplemented until proven.
7. **Sandbox recycles.** Uncommitted work is lost. Commit early. Re-clone and
   reinstall the toolchain after a recycle.

---

## 6. Where things live

| Area | Files |
| --- | --- |
| Entry point, modes, chrome document | `src/main.cpp` |
| HTML → DOM | `src/rendering/document.cpp` |
| CSS parse | `src/rendering/css_parser.cpp` |
| Cascade, matching | `src/rendering/style.cpp` |
| Layout, units, shorthands | `src/rendering/layout.cpp` |
| Paint commands | `src/rendering/render_tree.cpp` |
| Raster surface | `src/rendering/software_surface.cpp` |
| Fonts | `src/rendering/ttf_font.*`, `glyph_rasterizer.*`, `font_library.*` |
| Default styles | `src/rendering/user_agent_stylesheet.cpp` |
| Units | `src/rendering/css_units.hpp` |
| Networking, TLS | `src/network/` |
| UI | `src/ui/` — `window_host`, `sdl_window`, `input_router`, `browser_session` |

Docs: `docs/ROADMAP.md` (status), `docs/PHASES.md` (order), `docs/UI_SPEC.md`
(chrome), `docs/AUDIT.md` (findings), `docs/ARCHITECTURE.md`.

---

## 7. Next work, in priority order

1. **Scrolling** — wheel + scrollbar + `scrollTop`. Unblocks long pages and
   `position: sticky`.
2. **Wire tab click/close** to the session; address-bar focus styling.
3. **Shaping** for non-Latin scripts.
4. **Debug overlay newlines** — small, visible.
5. **Understand the Windows `SDL_Init` hang** and re-enable that test.
6. **Fix `AetherisHttpClient`** properly rather than excluding it.

---

## 8. Working in a shared environment

This repo lives beside other browser projects worked on by other people.

- **Only touch `drsamonline/The_Browser`.** Do not modify, delete or "clean up"
  other repositories, shared caches or another agent's build directories.
- **Commit and push to `main` in small, self-contained commits.** Do not rewrite
  history, force-push, or move an existing tag.
- **Never delete a release or tag** without explicit instruction.
- **Do not reintroduce a failing size budget.** Size is reported, never enforced.
- **Attribute upstream work.** gumbo, QuickJS and the DejaVu subset are credited
  in `CREDITS.md`; keep it current.
- **Leave the tree building and green.** If you cannot finish a change, revert it
  rather than leaving `main` red.

---

*Aetheris Browser — handover. Maintained by **Dr Sohil Momin**.*
