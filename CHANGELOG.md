# Changelog

**Author:** Dr. Sohil Momin · Part of the [Aetheris documentation set](docs/USER_GUIDE.md)

All notable Aetheris-specific changes should be documented here. Upstream-derived changes should retain their original provenance and must not be presented as newly authored Aetheris work.

## Unreleased

- **Scrolling.** Wheel, PageUp/PageDown, Space, Home and End move the page; a proportional scrollbar is drawn in the content area; `Viewport` clamps the offset to the document. The offset is applied in the paint pass (`PaintExecutor::execute` takes a `PaintOffset`), so scrolling repaints without relaying out, and commands outside the surface are culled. `--render-window <url> out.ppm --scroll-y <px>` renders a scrolled view for headless inspection. Covered by `AetherisScrolling` and by new cases in `AetherisInputRouter`.
- **Repaints no longer refetch.** `--window` kept no document between frames, so every repaint re-downloaded the page. The loaded `RenderDocument` is now cached until a navigation or reload sets the page dirty.
- **`AetherisHttpClient` flake fixed.** The fixture's accept thread exited on a 10 s idle `select`, so it could vanish between connections on a loaded runner and it made every pass wait out the timeout. It now runs until an explicit stop flag is set: 10.04 s to 0.09 s, and the full suite went from 25.7 s to 5.2 s under `-j 8`.
- **Strict build unblocked.** `src/rendering/png.cpp` declared an unused `kMaxCodes`, which `-Werror` rejects. Removed.
- **Version string corrected.** `--version` and the startup banner reported v0.5.3 while the released tag was v0.6.2.
- **Scrollbar clicks.** A click on the track scrolls to that point (`ScrollTrack`), and the bar is checked before the page so the click cannot fall through to a link underneath. Dragging the thumb is not implemented.
- **Stale pooled connections retried.** `HttpClient` probes a pooled socket before reuse, but a server's FIN can arrive between the probe and the write; the request then went into a dead socket and returned an empty body. `request_once` now retries exactly once on a fresh connection.
- **Three flaky fixtures fixed.** `AetherisHttpClient` (twice) and `AetherisDomHost` used an idle `select` timeout as their shutdown condition, so a loaded runner could pause longer than the timeout between connections. All three now use a stop flag: the suite is stable over four consecutive `-j 8` runs.
- **Scroll state in JavaScript.** `scrollTop`, `scrollLeft`, `scrollHeight`, `scrollWidth`, `clientHeight` and `clientWidth` on `document`, `document.scrollingElement`, `documentElement` and `body`; `window.scrollX`/`scrollY`; `window.scrollTo`/`scrollBy` and the bare globals. All six are backed by a host hook (`Interpreter::viewport_metrics`, `scroll_target`) and read live, so a script that scrolls and then reads `scrollTop` sees its own scroll. `scrollHeight` and friends are read-only. Covered by seven assertions in `AetherisJs` and one end-to-end in `AetherisDomHost`.
- **Scripts run in the window loop.** `--window` called `RenderDocument::create` directly, so no script ever executed in a real window and pages that build their content in JavaScript rendered blank. The page now loads through `create_scripted_document` (parse, run inline scripts, lay out), and the interpreter and DOM host live for the window's lifetime so nodes scripts create stay valid. `page_dom.release()` runs before teardown: freeing the QuickJS runtime with live handles aborts. Covered by `AetherisWindowScripts`, which drives the binary in `--window` mode and asserts both the script's output and a clean exit.
- **Clicks reach page scripts.** The window dropped every `PageClick`, so a button with a handler did nothing. `dispatch_click_at` hit-tests the point, walks up to the nearest node with a JS handle, fires a bubbling `click` and re-lays out. Shared by real clicks and by the new `--click <x> <y>` flag, which is how a click is exercised without a display. Covered by a second scenario in `AetherisWindowScripts`.
- **Timers tick per frame.** `setInterval` either never fired again or, worse, `run_event_loop` drained the queue at load and fired a repeating timer a thousand times before the first frame. `Interpreter::run_timers(budget_ms)` advances virtual time by one frame and fires only the timers due inside it; the window calls it with 16 ms per iteration, relayouts when something ran, and repaints. Load-time now runs `run_timers(0.0)` so only already-due timers fire. A 50 ms interval over 20 frames ticks 6 times. Covered by a third scenario in `AetherisWindowScripts`, which also asserts the count stays under 100.
- **One script-loading path.** `run_render` and the window each had their own copy of the parse / DOM-host / interpreter / script / layout sequence, which is how the window ended up running no scripts at all for months without anyone noticing. `create_scripted_document` is now the only copy: it takes the interpreter, DOM host and viewport state by reference, plus a timer budget (negative drains the queue for a one-shot render, zero ticks per frame for a window). `--dispatch` moved into `run_render` because a window has no use for it. `main.cpp` is 129 lines shorter.
- **Idle timers stop costing frames.** A timer that ran forced a relayout and a repaint whether or not it touched anything. The window now fingerprints the document (hashed node names, text and attributes) before and after the tick and only lays out again if it changed, and reports the count as `relayouts: N` in its summary. An idle `setInterval` over 20 frames: 0 relayouts. A timer that writes text: 6. Both asserted in `AetherisWindowScripts`.
- **One click path.** `run_render` had its own copy of the hit-test-and-dispatch block; it now calls `dispatch_click_at`, the same function the window uses, so a click behaves identically in a render and in a window. `--window` also accepts `--click <x>,<y>` as well as `--click <x> <y>`, because `--render` only ever took the comma form.
- **Keys reach page scripts.** `keydown` now dispatches to the page with `event.key` set, so a keyboard shortcut a page implements works. The router emits a `Key` action for any key the browser does not claim - and that fallback is the *last* branch in `handle_key`, because placing it earlier swallowed Escape and the window stopped quitting. `--key <name>` on `--window` fires one key after load for headless checks.
- **Hover reaches page scripts.** `PageHover` had no case in the window, so `mouseover`/`mouseout` handlers never ran. The window now tracks the element under the pointer and dispatches `mouseout` on the one it left and `mouseover` on the one it entered, repainting only when a handler changed the document. `--move <x> <y>` on `--window` fires one move after load. The hit-test and walk-up-to-a-handle logic is now `node_at`, shared with clicks.
- **CSS `:hover` works.** The cascade already understood `:hover` and `RenderDocument::apply_visual_state` already rebuilt layout for it, but nothing fed `VisualInteractionState` from the window, so hover styling never appeared. The window now sets the hovered node and re-applies the visual state on every pointer change. Verified at the pixel level: a fixture that turns its first div red on hover renders (255,0,0) there and (0,0,255) on the untouched one.
- **CSS `:active` works.** `set_active` existed and the cascade honoured `:active`, but the router had no `MouseUp` case, so nothing ever cleared or set it. A press now sets the element under the pointer as active and a release clears it; `--press <x> <y>` on `--window` leaves it pressed so a frame can show it. Verified at the pixel level: a fixture that turns its first div green on `:active` renders (0,255,0) there and (0,0,255) on the other.
- **CSS `:focus` works, and `relayout()` no longer discards interaction state.** A click focuses the element it hit and `:focus` paints — after two reverted attempts, because the real fault was elsewhere: `RenderDocument::relayout()` rebuilt layout with no `VisualInteractionState`, so the focus a click had just set was dropped the moment `dispatch_click_at` called relayout. `RenderDocument` now remembers the last applied state and hands it to `relayout()`. Verified at the pixel level: a fixture with `#a:focus{background:#ff00ff}` renders (255,0,255) after a click.
- **The focus model, shaped by upstream.** A click focuses the element it hit, `blur` fires on the element losing focus, style is re-resolved, `focus` fires on the element gaining it, and keys dispatch to the focused element before bubbling to the document. The order and the predicate model follow Ladybird's `Document::set_focused_area` and `Element::is_focused()` rather than being invented here. Verified: a fixture logs `focus`, then the element's own `keydown`, then the document's, and `#a:focus` renders (255,0,255).
- **`:focus-within` and `document.activeElement`.** Both ported from `LibWeb`: `:focus-within` is a membership test on the focused element's ancestor chain (upstream's `FocusWithinChain`), and `activeElement` is answered by the host from the interaction state (`Document::active_element()`).
- **Clicks and keys now reach `document`.** `dispatch_bubbling` walks the DOM ancestor chain, which stops at the document element — the JS `document` object is not in it, so a handler registered there never fired. Both dispatch paths now finish with `js.dispatch_event(...)`, as upstream's single bubbling path does.
- **`:focus-visible`.** Ported from `Element::should_indicate_focus()`: a text-entry control always indicates focus, anything else only when focus arrived from the keyboard. The state carries a `FocusTrigger`, which is what Tab will set once focus order exists. Verified at the pixel level: a pointer click on a div matches `:focus` (green) but not `:focus-visible`; a keyboard-triggered focus matches `:focus-visible` (magenta).
- **`keyup`.** Dispatched after `keydown` on the same chain — the focused element, then the document and window — as upstream's `EventHandler::handle_keyup` does through `fire_keyboard_event`. `event.key` was being cleared between the two events, so the first attempt logged `keyup undefined`; both now carry the key.
- **Tab order.** Tab moves focus to the next sequentially focusable element, wrapping, and the focus it sets is keyboard-driven so `:focus-visible` matches. Focusable means `tabindex` parses as an integer, as upstream's `Element::is_focusable()` requires; links and form controls have their own clauses upstream and are not covered yet.
- **Focus changes now repaint.** A focus change alters the cascade but not the DOM, and the window only redrew when the DOM fingerprint moved — so `:focus-visible` never appeared even though the state and layout were right. The Tab path sets `need_render` itself.
- **Tab order is covered by a test.** `AetherisWindowScripts` now drives `--key tab` against a one-element fixture and asserts the frame after it is magenta. The assertion is on the *paint*, not on the focus state: a stale frame with correct state is exactly the bug that survived two sessions.
- **No rebuild when no rule could match.** Upstream invalidates only the elements whose pseudo-class state changed (`invalidate_style_after_pseudo_class_state_change`); the cheap half of that is now in place — if no selector in the sheet mentions `:hover`, `:focus` or `:active`, a state change skips the rebuild entirely. A page with no hover rule now costs 0 relayouts to move the pointer over it (was 1); a page with one still costs 1.
- **SDL2 window backend.** `src/ui/sdl_window.cpp` implements `WindowHost` on SDL2: a resizable window, a streaming ARGB texture blitted from the software surface, and key, text, mouse, wheel, resize and close events translated into the router's vocabulary. Enable with `-DAETHERIS_WITH_SDL2=ON`; without SDL2 the build is unchanged and headless. Compiles clean against SDL 2.30 under `-Wall -Wextra -Werror`; not yet run against a real display.
- **Release pipeline fixed.** Test targets compile with `-UNDEBUG` so assertions stay live in Release, and a Windows packaging job was added.

## v0.4 — 2026-11-08

The browser became an application: real outline text, a compact chrome, a
window loop and input routing.

- **Text from real font outlines.** A TrueType/OpenType reader (`cmap` formats 4
  and 12, `head`/`hhea`/`maxp`/`hmtx`/`loca`/`glyf`), an anti-aliased scanline
  rasteriser, a glyph cache, and 1,218 kerning pairs. A DejaVu Sans subset
  (~23 KB) is embedded, so text renders identically with no system font, and
  `AETHERIS_FONT` overrides it.
- **Compact browser chrome.** One 34 px strip — tab, navigation, address bar,
  menu — leaving 95.6% of a 768 px window for the page. No menubar. The chrome
  is an HTML document rendered by the engine itself.
- **Window loop and input routing.** `aetheris --window` runs a persistent
  session; `Ctrl+L/T/W/R`, `Alt+Left/Right`, `F11` zen mode, address editing,
  and clicks split between chrome and page. A window host interface with a
  headless backend keeps it runnable without a display.
- **Security.** CSP is enforced for inline scripts, and every fetch — document
  and subresource — goes through `SecureResourceLoader`, so the tested policy is
  the one a real page gets. TLS certificate validation is proven by
  `Tools/check_tls_verification.sh`.
- **CSS.** Media queries, structural pseudo-classes, and `transform`
  (translate/scale).
- **Forms.** `value`, `checked` and a cancelable `submit`.
- **Fixes.** `display: none` now hides the subtree (it used to hoist children,
  which rendered `<head>` contents as page text); `removeEventListener` and
  listener de-duplication; a writable `dataset`; a keep-alive crash where the
  liveness probe ran after the reconnect decision.

Size profile: **2,630,800 bytes (~2.51 MB)**, +137 KiB over v0.3 — reported on
every build and never enforced.

## v0.3 — 2026-11-08

The engine was rebuilt on two upstream components and the DOM surface was filled
out. Everything below is covered by the 51-test CTest suite.

- **JavaScript: QuickJS replaces the hand-written interpreter.** ES2025 instead
  of a subset — arrow functions, template literals, destructuring, spread/rest,
  optional chaining, `Symbol`/`WeakMap`, real `Date`/`Map`/`Set` and correct
  number formatting.
- **HTML: gumbo replaces the hand-written tokenizer.** The HTML5
  tree-construction algorithm: implied end tags, the stray-`</p>` rule,
  automatic `<tbody>`, character references and error recovery.
- **Full event model.** Capture, target and bubble phases, event objects,
  `preventDefault`, `stopPropagation`, `stopImmediatePropagation`, and
  `removeEventListener` with de-duplicated registrations.
- **Timers.** `setTimeout`, `setInterval`, `clearTimeout`, `clearInterval` and
  `queueMicrotask` on virtual time.
- **DOM surface.** `document.body`/`head`/`documentElement`/`title`,
  `window.innerWidth`/`innerHeight`, `element.tagName`, a writable
  `element.dataset`, `remove()`, `insertBefore()`, live `children`/`parentNode`,
  and `getComputedStyle(el).getPropertyValue(name)`.
- **Networking.** RFC cookies (`Domain`/`Path`/`Expires`/`Max-Age`/`Secure`/
  `HttpOnly`), a keep-alive liveness probe, and `aetheris --dump-dom <url>`.
- **Persistent browser session.** `src/ui/browser_session.*` holds the tabs, the active tab, loading and error state, with URL normalisation (a bare host gets `https://`, `localhost` gets `http://`). `aetheris --window` keeps one session alive, so navigating re-fetches and re-renders instead of starting over, and the tab strip shows the page's own title. Tab open/close/activate are implemented and tested.
- **Input routing.** `src/ui/input_router.*` turns window events into actions: `Ctrl+L/T/W/R`, `Alt+Left/Right`, `F11` zen mode, address-bar editing with Enter to navigate and Escape to blur, clicks split at the 34 px strip boundary into chrome or page, and page-only hover. It is a pure function of (event, focus state), so all bindings are tested without a display, and the button positions come from the same constants the chrome is drawn with.
- **Window host interface and headless backend.** `src/ui/window_host.hpp` defines the five-method window contract (open, present, poll, set_title, close) plus the event types, so chrome and input routing are platform-independent. `HeadlessWindowHost` accepts a fixed frame budget and optionally writes frames, and `aetheris --window <url> [frames] [dir]` runs the present-and-pump loop. The SDL2 backend is not written yet: this environment has no SDL2 headers or display, so it could not be compiled or verified.
- **Compact browser chrome.** `aetheris --render-window <url> [out.ppm]` renders the page composited under a 34 px chrome strip (tab, navigation, address bar, menu) built as an HTML document by the engine itself. `docs/UI_SPEC.md` fixes the proportions: no menubar, one strip, ~95% of the window for content.
- **Fixed `display: none` hiding only the element.** The layout builder hoisted a hidden element's descendants into its parent instead of hiding the subtree, so `<head>` contents such as `<title>` rendered as page text. The whole subtree is now hidden, which is what CSS specifies.
- **Kerning.** 1,218 kerning pairs are extracted from the embedded font's GPOS table at build time (fontTools) and compiled in as `embedded_kern.inc`; `kerning_adjustment` binary-searches them and both `Font::measure` and `SoftwareSurface::draw_text` apply the pair adjustment, so measured and painted text stay in step. `AV` tightens as it should. Shaping (ligatures, Arabic/Indic scripts) is not implemented.
- **A font is embedded in the binary.** A DejaVu Sans subset (~23 KB, Basic Latin + Latin-1, no hinting) is compiled in, so text renders identically on a machine with no fonts installed. Precedence is `AETHERIS_FONT` (explicit path) > embedded > system font, with `AETHERIS_FONT=none` forcing the bitmap fallback. Licence recorded in CREDITS.md.
- **Glyph rasterisation is cached** by (glyph, pixel size). Without it a small page took 2.17 s to render because every glyph was re-rasterised on every draw call; with the cache it takes 0.52 s.
- **Text is rendered from real font outlines.** `font_library` loads a TrueType font (from `AETHERIS_FONT` or a system path; `AETHERIS_FONT=none` forces the bitmap fallback), `FontMetrics::advance` measures with the font's advance widths, and `SoftwareSurface::draw_text` rasterises each glyph and blends it with coverage alpha. Measurement and painting use the same metrics, so text lines up. The embedded bitmap font remains the fallback when no font is found.
- **Glyph rasteriser.** `src/rendering/glyph_rasterizer.*` flattens quadratic outlines (including implied on-curve points between consecutive off-curve points) and scan-converts them with four vertical sub-samples and exact horizontal coverage, producing anti-aliased coverage bitmaps with correct bearings. Not yet wired into text layout or paint, so rendering is still the bitmap font.
- **TrueType/OpenType font parsing.** `src/rendering/ttf_font.*` reads the table directory, `head`/`hhea`/`maxp`/`hmtx`/`loca`/`glyf` and `cmap` formats 4 and 12, and extracts glyph outlines in font units. Parsing only: nothing is rasterised or wired into text layout yet, so rendering is unchanged.
- **Content-Security-Policy is enforced for scripts.** A meta tag with http-equiv=Content-Security-Policy whose script-src (or default-src fallback) lacks 'unsafe-inline' - including 'none' - stops inline scripts from running, and the block is reported on stderr.
- **TLS certificate validation is verified, not just implemented.** The TLS layer already set `SSL_VERIFY_PEER`, matched the host with `SSL_set1_host` and checked `SSL_get_verify_result`; `Tools/check_tls_verification.sh` now proves it end to end: an untrusted self-signed certificate is refused, the same certificate trusted via `AETHERIS_CA_FILE` is accepted, and a trusted certificate for the wrong host is refused.
- **`performance.now()` and `requestAnimationFrame`.** `now()` returns virtual milliseconds since start, and `requestAnimationFrame` schedules a callback one 16 ms frame ahead on the existing timer queue (`cancelAnimationFrame` cancels it). There is still no compositor or real clock.
- **CSS transforms.** `transform: translate(...)`, `translateX`/`translateY` and `scale(...)` are applied to painted geometry, composed down the tree so a child inherits its ancestors' transforms. `rotate`/`skew`/`matrix` are not supported yet and are ignored rather than misapplied.
- **CSS: media queries and structural pseudo-classes.** `@media` blocks are parsed and filtered against the renderer's screen viewport (1024x768), so `@media screen` and `(min-width: ...)` apply while `print` and non-matching widths are dropped. `:first-child`, `:last-child`, `:only-child` and `:nth-child(n)` now match.
- **Form controls.** `value` (read/write) on `input`, `textarea`, `select` and `option`, `checked` on checkboxes and radios, and `form.submit()`, which fires a cancelable `submit` event so `preventDefault()` stops submission. The DOM is the source of truth, so a script's change is what the renderer paints.
- **The security policy is on the real fetch path.** The document and every subresource now go through `SecureResourceLoader` (backed by `HttpResourceBackend`) instead of calling the HTTP client directly, so unsupported schemes, mixed content and file-document network access are actually refused for a rendered page.
- **Fixed a crash introduced with the keep-alive probe:** the liveness check ran after the reconnect decision, so a pooled connection closed by the server was reset to null and the next request aborted. The probe now runs before the decision, with a regression test (a server that closes a pooled connection) verified to fail without the fix.
- **Data tables** taken from upstream: the full HTML5 character-reference table
  and the 148 CSS named colours.
- **Build.** Vendored gumbo and QuickJS build with `-w` so their warnings do not
  break the project's `-Werror` set; CI's full-tree job is opt-in and releases
  publish from the standalone size profile.

Size profile: 2,515,056 bytes (~2.40 MB), up from ~1.10 MB at v0.2 — the cost of
a real JavaScript engine and a real HTML5 parser.

## v0.2 — 2026-11-07

### Added

- GitHub community health files: pull request template (`.github/PULL_REQUEST_TEMPLATE.md`) and Aetheris feature-request issue form (`.github/ISSUE_TEMPLATE/feature_request.yml`).
- Browser application, tab, input, navigation, and browser-chrome foundations.
- Resource loading, HTTP transport abstraction, cookie-state foundation, and dynamic runtime infrastructure.
- Security-policy and secure-resource-loading foundations.
- Browser chrome state and basic navigation feedback.
- Regression coverage for application, runtime, security, and browser-chrome behavior.
- Final v0.1 acceptance criteria, release checklist, and release smoke coverage.
- Documentation set completed in GitHub style: `docs/USER_GUIDE.md`, `docs/ARCHITECTURE.md`, root `AUTHORS` file, author watermark (Dr. Sohil Momin) across project documents, `.mailmap` identity mapping, and Aetheris CODEOWNERS entries.
- `Standalone` and `StandaloneSize` CMake presets, plus the `AETHERIS_SIZE_OPTIMIZED` profile (`-Os`, dead-section elimination, LTO, stripped) for minimum-footprint builds of the Aetheris targets.
- A "known gaps and open work" section in `ISSUES.md`.
- A minimal HTTP/1.1 client (`HttpClient`, `include/aetheris/network/http_client.hpp`) with DNS resolution via `getaddrinfo`, a bounded (non-blocking) connect timeout, and Content-Length, chunked and read-until-close body handling.
- Headless `aetheris --fetch <url>` and `aetheris --render <url> [out.ppm]` modes; the executable now links `AetherisRendering` and can fetch a URL and render it to a PPM end to end.
- `TestAetherisHttpClient`: a loopback-server regression test covering DNS + HTTP round-tripping.
- A dependency-free 8x8 bitmap font (`bitmap_font.{hpp,cpp}`, public-domain font8x8 data) with real glyph rasterisation: `DrawText` now paints actual glyphs instead of a solid cell, and text measurement and rasterisation share one advance so glyphs line up with layout.
- `TestAetherisBitmapFont`: regression coverage for glyph lookup, metrics and rasterisation.
- A dependency-free PNG decoder (`png.{hpp,cpp}`, colour types 0/2/3/4/6, 8/16-bit, non-interlaced) with its own DEFLATE inflater; the image cache now decodes PNG as well as PPM, and relative `src`/`href` references are resolved against the document URL so fetched pages' images load. `TestAetherisPng` covers RGB/RGBA/grey/palette inputs.
- CSS `border` (width/style/colour, any order) and `background` (colour + `url()` image) shorthands, with `TestAetherisCssShorthands`.
- A baseline JPEG decoder (`jpeg.{hpp,cpp}`, SOF0, 8-bit, 1/3 components, restart intervals); the image cache decodes JPEG as well as PNG/PPM. `TestAetherisJpeg` covers 4:4:4 colour and grayscale.
- A GIF decoder (`gif.{hpp,cpp}`: LZW, global/local colour tables, interlacing, transparency); the image cache decodes GIF too. `TestAetherisGif` covers a palette fixture.
- Anti-aliased text: `SoftwareSurface::draw_text` now supersamples each glyph cell (4x4 coverage) and blends by coverage, giving grayscale edges instead of hard pixels.
- Optional HTTPS/TLS (`tls.{hpp,cpp}`, `AETHERIS_HAS_TLS`): a real OpenSSL handshake with certificate and hostname verification against the system trust store (or `AETHERIS_CA_FILE`); `HttpClient` now serves `https://`. `TestAetherisTls` performs a hermetic handshake against an in-process OpenSSL server (trusted connect + self-signed rejection).
- Interlaced (Adam7) PNG support in the decoder; `TestAetherisPng` now checks an interlaced image decodes identically to the same non-interlaced image.
- Progressive JPEG (SOF2) support: multiple scans with spectral selection and successive approximation, sharing one coefficient buffer and reconstructing after the last scan. `TestAetherisJpeg` decodes a progressive fixture.
- A minimal SVG rasteriser (`svg.{hpp,cpp}`): rect, circle, ellipse, line, polyline, polygon and path (Béziers flattened), fill + stroke, viewBox and translate/scale/matrix transforms, rendered with 2x supersampling; the image cache renders SVG. `TestAetherisSvg` covers shapes, paths and paints.
- WebP decoding via the system libwebp (lossy VP8 and lossless VP8L), wired into the image cache and compiled in only when libwebp is found (`AETHERIS_HAS_WEBP`). `TestAetherisWebp` decodes lossless (exact) and lossy (approximate) fixtures.
- A self-contained mini JavaScript engine (`src/js/`, the `AetherisJS` library): lexer, recursive-descent parser and tree-walking interpreter covering variables, operators, control flow, functions/recursion, arrays, objects and built-ins (`console.log`, `Math`, `parseInt`/`parseFloat`, string and array methods, `document.write`). `aetheris --eval <script>` runs a snippet and `--render` executes inline `<script>` blocks, injecting `document.write` output before layout. `TestAetherisJs` covers the core. (The full LibJS engine in the tree is a bytecode VM needing Rust + a clang GC plugin + ICU and is not built in the standalone profile.)
- Expanded the mini JavaScript engine with String methods (`split`, `replace`, `substring`, `repeat`, `trim`, `includes`, `startsWith`, `endsWith`), Array methods (`forEach`, `map`, `filter`, `reduce`, `reverse`, `sort`, `concat`), numeric indexing on strings and arrays, `Object.keys` and `Array.isArray`, and a `CREDITS.md` documenting upstream attribution.
- Added closures, the `new` operator with constructor functions and `this` binding for methods, `JSON.stringify`/`JSON.parse`, and `Math.trunc`/`Math.sign` to the mini JavaScript engine.
- Added a prototype chain to the mini JavaScript engine: functions get a `prototype` object, `new` links instances to it, property lookup walks the chain, `Object.prototype` is extensible, and the `instanceof` operator is supported.
- Minimal DOM bindings: `document.getElementById(id)` returns an element handle whose `textContent` can be read and written, and `--render` parses the document, runs inline scripts, then applies `textContent` changes (and `document.write` output) before layout. `RenderDocument::create_from_document` builds from an already-parsed document so a host can mutate it first.
- Added `class` syntax to the mini JavaScript engine: `constructor` and methods are desugared onto a prototype object, so `new`/`instanceof`/inheritance work with classes as well as constructor functions.
- Added the `for...of` loop (over arrays and strings) and the `switch`/`case`/`default` statement to the mini JavaScript engine.
- Extended the DOM bindings: `document.createElement`, `element.appendChild`, `element.setAttribute` and `element.getAttribute`, so scripts can build and insert new elements before layout.
- Added events to the mini JavaScript engine: `window`/`document` `addEventListener` and `on<type>` handlers, dispatched by the host (`DOMContentLoaded`, `load`). Fixed a lifetime bug where function bodies aliased a parsed program that was freed when `run()` returned, so handlers invoked after the script (e.g. at load) could dangle; parsed programs are now retained.
- Added a compact backtracking regular-expression engine (`src/js/regex.{hpp,cpp}`) with literals, character classes, anchors, alternation, groups and the *, +, ?, {n,m} quantifiers and the `g`/`i` flags; wired to regex literals, `RegExp`, `test`/`exec`, and `String.match`/`search`/`replace`/`split`. Capture groups, back-references and look-around are not supported. Fixed `new` precedence so `new X(a).y()` parses as `(new X(a)).y()`.
- Added async support to the mini JavaScript engine: a microtask queue, `Promise` (`then`/`catch`/`finally`, `resolve`/`reject`/`all`), `async` functions and `await`. There is no event loop, so `await` drains the microtask queue synchronously; the queue is also drained after each script and after events.
- Added `Map` (get/set/has/delete/keys/values/size), `Set` (add/has/delete/size), `Date` (`Date.now`, `new Date(ms)`, getters, `toISOString`/`toString`, UTC) and `Array.prototype.includes` to the mini JavaScript engine.
- Added element-level events: element handles expose `addEventListener` and `on<type>` handlers, and `--render ... --dispatch <id> <type>` fires an event on a named element after load (with `this` bound to the element).
- Added geometry-based event dispatch: `--render ... --click <x>,<y>` hit-tests the point (reusing `DocumentInteraction::hit_test`) and fires a click on the element there, then re-layouts. Added `RenderDocument::relayout()`. Also applied the inline `style` attribute in the cascade (previously ignored), so inline styles now affect layout and painting.
- Networking completeness: `HttpClient` now follows redirects (301/302/303/307/308, up to five hops, resolving relative `Location` values) and keeps a host-scoped cookie jar (parses `Set-Cookie`, sends `Cookie` on matching requests).
- Inline `<style>` blocks are extracted and applied in the cascade, so pages styled only by an inline block now render correctly.
- DOM quick wins: `classList` (add/remove/contains/toggle), `parentNode` and `children`, `document.querySelector`/`querySelectorAll` (tag, `.class`, `#id`, compound and descendant selectors), and event bubbling from the target up its ancestor chain.
- Property accessors in the engine (getter/setter pairs), used for DOM properties. `innerHTML` (get serialises the subtree, set re-parses and grafts the fragment, registering handles for the new nodes) and `element.style` (whole-string set plus `setProperty`/`removeProperty`) are now supported, and `textContent` became a live accessor.
- Added `TestAetherisDomHost`: a regression test that drives the real `aetheris` binary against a loopback page and asserts the JavaScript DOM host end to end (`innerHTML`, `style`, `classList`, selectors, `textContent`, event bubbling) plus a clean stderr, so allocator corruption fails the suite instead of passing silently. Suite is now 50 tests.
- HTTP keep-alive: `HttpClient` reuses idle connections per origin (up to four) when a response is fully delimited and the server does not send `Connection: close`; `clear_connections()` resets the pool.
- Regular expressions: back-references (`\\1`-`\\9`) and look-around (`(?=)`, `(?!)`, `(?<=)`, `(?<!)`). Named groups remain unsupported.
- HTML character references are decoded in text and attribute values (`&amp;`, `&nbsp;`, `&#65;`, `&#x42;`, ...), using the full 2,125-entry table generated from upstream `LibWeb/HTML/Parser/Entities.json`; the CSS named-colour table was replaced with the full 148-entry set from upstream `LibGfx/Color.cpp`. Both are generated files and are credited in `CREDITS.md`.
- Vendored QuickJS (quickjs-ng, MIT) as `third_party/quickjs/` with an `AetherisQuickJS` build target and an in-tree test. It builds with a C compiler only, so the standalone profile can have a real ES2025 engine without Rust/ICU/clang. Measured cost: **~724 KB** stripped (744,536 bytes for the embedding probe vs 3,216 for a bare binary). Not yet wired into `aetheris`; the hand-written interpreter still runs.
- **Replaced the hand-written JavaScript interpreter with QuickJS.** `src/js/js.cpp` is now a thin embedding (console/document/window bindings, event dispatch, promise-job draining) over the vendored engine, and `main.cpp`'s DOM host builds real QuickJS objects (opaque `DomNode*`, accessors via `JS_DefinePropertyGetSet`, event bubbling up the ancestor chain). The `src/js/regex.*` files are gone. Scripts now get ES2025 — arrow functions, template literals, destructuring, spread/rest, optional chaining, `Symbol`/`WeakMap`, real `Date`/`Map`/`Set`, correct number formatting — instead of a subset. Size profile: **2,129,464 bytes (~2.03 MB)**, up from ~1.16 MB.
- **HTML is now parsed by gumbo** (the HTML5 algorithm, vendored, Apache-2.0) instead of the hand-written tokenizer. Real tree construction means implied end tags (`<ul><li>one<li>two</ul>` gives sibling `<li>`s), the spec's empty `<p>` for a stray `</p>`, automatic `<tbody>`, character references, and proper error recovery. `Document::parse_html` converts gumbo's tree into the internal `DomNode` tree, splicing out the implicit `<html>`/`<head>`/`<body>` wrappers when the document did not author them so the renderer still sees content as authored. Cost: **360,952 bytes (~352 KB)**, taking the size profile to **2,490,416 bytes (~2.38 MB)**.
- Evaluated litehtml (BSD-3, CSS2/CSS3 layout) as a replacement for the hand-written layout: it builds with this toolchain and produces real CSS geometry, but costs ~1.1 MB incremental (projected ~3.4 MB binary) and would require re-binding the DOM/JS layer onto its own element tree. Not adopted; measurements and reasoning are in `docs/LITEHTML_EXPERIMENT.md`.
- **DOM surface.** `document.body`, `document.head`, `document.documentElement`, `document.title` (get/set), `window.innerWidth`/`innerHeight`, `element.tagName`, `element.dataset` (data-* attributes as camelCase keys), `element.remove()` and `element.insertBefore()`. `children` and `parentNode` are now live getters instead of a snapshot taken at load, so DOM mutations are reflected immediately.
- **`getComputedStyle(el).getPropertyValue(name)`** resolves a property through the cascade (matching rules plus inline style).
- **Full event model.** Dispatch now runs the three phases — capture (root to target), target, then bubble (target to root) — with `addEventListener(type, fn, {capture})`, event objects (`type`, `target`, `currentTarget`, `defaultPrevented`) and `preventDefault`/`stopPropagation`/`stopImmediatePropagation`. `dispatch_event` treats document and window as one chain, so `stopPropagation` stops the whole dispatch.
- **RFC cookies.** The jar honours `Domain`, `Path`, `Expires`, `Max-Age` (including deletion), `Secure` and `HttpOnly`, and only sends a cookie when host, path and scheme match.
- **Keep-alive liveness probe.** A pooled connection is checked with `select()` before a request is written to it, so a connection the server closed while idle is replaced instead of failing the request.
- **`aetheris --dump-dom <url>`** prints the parsed DOM, which makes DOM and layout bugs inspectable without rendering an image.
- **Timers and an event loop (virtual time).** `setTimeout`, `setInterval`, `clearTimeout`, `clearInterval` and `queueMicrotask` are available as globals and on `window`; `Interpreter::run_event_loop()` fires them in due order without real waiting and drains promise jobs between callbacks. The browser runs the loop after scripts and page events, so a `setTimeout` that mutates the DOM now takes effect before layout.
- **Fixed the failing CI/release build.** Vendored gumbo tripped `-Werror` with `-Wcast-qual` (`attribute.c: cast from 'const char *' to 'void *' drops const qualifier`), which failed every build. Vendored targets (`AetherisGumbo`, `AetherisQuickJS`) now compile with `-w` (`/W0` on MSVC) and their include dirs are `SYSTEM`. The full-tree Windows CI job is now opt-in (it builds the unsupported upstream tree and failed on every push), and tagged releases publish from the standalone size profile instead.
- Fixed a use-after-free: replacing `innerHTML`/`textContent` freed nodes whose handles were still registered, and a post-script reconciliation pass compared a stale `textContent` snapshot and rewrote elements. Writes now go straight to the DOM through the accessor, and stale registry entries are purged before a subtree is freed.
- HTTP: responses carrying `Cache-Control: max-age` are cached and re-served without a network request (`HttpClient::clear_cache()`).
- Regular expressions gain capturing groups: `match`/`exec` expose them and `replace` expands `$1`–`$9` and `$&`; `(?:...)` is non-capturing.

### Fixed

- `parse_http_url` no longer overrides an explicit port on `https://` URLs (it previously forced 443).

- Browser chrome navigation now constructs `NavigationRequest` using the current rendering/navigation API.
- Browser chrome navigation uses the active session viewport width when available.
- HTML tokenizer no longer truncates tag names at the first non-letter character, so `h1`–`h6` and custom elements (e.g. `my-element`) keep their full name.
- CSS selector matching now supports descendant (` `) and child (`>`) combinators; previously only the right-most compound was matched, so rules such as `#id .class` or `section > p` never applied. Specificity is computed as CSS (a,b,c) without double-counting class or element identifiers.
- Inherited properties (notably `color` and `text-decoration`) now reach descendants: initial values are applied after inheritance instead of before, so text nodes render with their ancestor's colour.
- Absolutely positioned elements resolve percentage widths against the containing block rather than against the already-resolved width.
- `overflow: hidden`/`clip` now clips descendants only; an element's own background and border are no longer clipped away by its own overflow.
- Offscreen surfaces (`SoftwareSurface`, `RenderDocument::render_to_surface`) default to a transparent clear colour instead of opaque white.

### Performance

- The CSS cascade takes a fast path for selectors without combinators and no longer copies the selector string per (node, selector) pair.
- The paint tree skips the per-node child sort when no child sets a `z-index`.
- The release optimization profile is now portable: LTO and dead-strip flags are chosen per toolchain and probed before use (previously `-flto=auto -ffat-lto-objects` and `-Wl,--gc-sections` were applied to every Clang/GNU build, which broke non-GCC toolchains and Apple's linker). `-Ofast` was dropped because it enables fast-math and contradicts the project's `-ffp-contract=off` determinism.

### Changed

- The HTML tree builder no longer synthesises implicit `<html>`/`<body>` wrappers around fragment input; parsed trees match the document as authored, consistent with the `AetherisRendering` regression suite.
- Removed committed build output (`build-standalone/`, `build-docs/`) from the repository and taught `.gitignore` to ignore build trees.
- Removed dead code: unused `StyleResolver::is_inherited_property`, `DomNode::last_child`, `VisualInteractionState::mark_paint_dirty`, `FormRuntime::clear_focus`, `CookieJar::clear_origin`, `Viewport::to_viewport_coordinates` and `DocumentRuntime::set_text`, and merged split namespace blocks in `software_surface.cpp`, `navigation.cpp` and `browser_session.cpp`.
- Rewrote `ISSUES.md` (previously Ladybird boilerplate with stale project references) and refreshed `README.md`, `docs/ARCHITECTURE.md` and the landing page for the new build profiles and the footprint story.
- Project documentation uses explicit Ladybird/SerenityOS-derived provenance and avoids unsupported fixed performance claims.

### Known limitations

Aetheris remains experimental and does not claim complete web-platform compliance, a complete production browser sandbox, or production-ready browser networking. Only `http://` URLs are reachable: TLS is not implemented, and DNS resolution is synchronous. Text rendering uses a single embedded 8x8 bitmap font: there is no vector font, font selection, kerning or shaping.

---

*Aetheris Browser — changelog. Authored and maintained by **Dr. Sohil Momin**.*
