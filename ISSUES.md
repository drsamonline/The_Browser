# Issue and bug-reporting guidelines

**Author:** Dr. Sohil Momin · Part of the [Aetheris documentation set](USER_GUIDE.md)

Report problems with the Aetheris-specific code (and reproducible upstream
issues) using the repository's own issue forms:

- Bugs — [`.github/ISSUE_TEMPLATE/bug_report.yml`](../.github/ISSUE_TEMPLATE/bug_report.yml)
- Feature requests — [`.github/ISSUE_TEMPLATE/feature_request.yml`](../.github/ISSUE_TEMPLATE/feature_request.yml)

A clear issue lets a maintainer reproduce the problem without guessing. The
single most important thing you can provide is a **reduced test case**.

## How to write a reduced test case

Start from the HTML/JS/CSS for the page that shows the problem and remove as
much as possible while the problem still reproduces:

1. Save a canned copy of the page (e.g. with the [SingleFile](https://addons.mozilla.org/en-US/firefox/addon/single-file/)
   extension) so external stylesheets and scripts are inlined, then strip
   irrelevant elements in the browser devtools.
2. If you are not using SingleFile, add a `<base href="…">` element so relative
   resources still resolve, and copy any external stylesheet or script locally.
3. Load the reduced document and confirm the same problem occurs.
4. **Script-related problems:** remove `script` elements one at a time and
   reload after each removal. If removing a script fixes the problem, you have
   isolated the cause.
5. **CSS-related problems:** inline the stylesheets into a `style` element, then
   remove rules one at a time, reloading after each removal.
6. **HTML-related problems:** remove elements one at a time, starting with
   `head`, and reload after each removal.

You will end up with a minimal document that still reproduces the problem.
Publish it at a site such as [CodePen](https://codepen.io/pen/),
[JS Bin](https://jsbin.com) or [JSFiddle](https://jsfiddle.net) and link it in
the issue.

*[Credits: the reduction method above is adapted from https://webkit.org/test-case-reduction/.]*

## Running and debugging Aetheris

The `aetheris` executable exposes a small, headless-friendly command surface:

```bash
./aetheris --help          # usage and options
./aetheris --version       # version banner
./aetheris --smoke-test    # initialise every subsystem, run a bounded frame
                           # loop and exit 0 (used by CI; no display needed)
./aetheris --fetch <url>   # fetch a URL over HTTP and print status/size
./aetheris --render <url>  # fetch a URL, render it and write a PPM image
```

The Aetheris rendering pipeline is exercised by the CTest suite. To run just the
Aetheris tests:

```bash
ctest --test-dir build-standalone -R Aetheris --output-on-failure
```

## Known gaps and open work

Aetheris is experimental. The following are known limitations rather than
regressions — see [V0.1_ACCEPTANCE.md](V0.1_ACCEPTANCE.md) for the full
non-goals list.

- **The JavaScript engine is a small subset.** `src/js/` is a self-contained
  tree-walking interpreter (variables, operators, control flow, functions and
  recursion, arrays, objects and a few built-ins). It is not a conforming
  implementation: closures, constructors/`new`/`this`, prototypes and
  `instanceof`, `class` syntax, `JSON` and a regular-expression subset are
  supported, but there is no async and no capture groups/back-references in
  the regex engine. The DOM binding is minimal:
  `document.write`, `getElementById`, `createElement`/`appendChild`,
  `setAttribute`/`getAttribute`, `textContent`, and `window`/`document`
  `addEventListener`/`on<type>` events (`DOMContentLoaded`, `load`).
  The full LibJS engine in the tree needs Rust, a clang GC plugin and ICU and
  is not built in the standalone profile.
- **Prototype runtime cluster** (`src/cache`, `src/network`, `src/ui`,
  `include/aetheris/`): the socket layer resolves hostnames via `getaddrinfo`
  and connects with a bounded timeout, and there is a minimal HTTP/1.1 client
  (`HttpClient`). HTTPS is available when OpenSSL is found at configure time
  (`AETHERIS_HAS_TLS`), with certificate + hostname verification; without it,
  only `http://` URLs are reachable. DNS resolution is still synchronous (it can
  block), and the window manager is a headless/X11 prototype with a stub Cocoa
  (macOS) backend.
- **Image decoding is limited.** PNG (8/16-bit, interlaced and
  non-interlaced), baseline and progressive JPEG, GIF (first frame only), WebP
  (when built with libwebp), a subset of SVG and ASCII PPM are decoded;
  animated GIF/WebP use only the first frame.
- **Text rendering is a single bitmap font.** Text is rasterised from one
  embedded 8x8 bitmap font with 4x4-coverage anti-aliasing; there is no vector
  font, font selection, kerning or shaping.
- **Standards coverage is partial.** Flex and Grid are foundational; many CSS
  properties and HTML behaviours are unimplemented.
- **Networking is not production-ready.** There is no complete TLS/certificate
  validation or full RFC-level cookie behaviour.
- **Provenance.** The repository contains upstream-derived Ladybird/SerenityOS
  infrastructure; see [UPSTREAM_ATTRIBUTION.md](UPSTREAM_ATTRIBUTION.md) before
  making licensing or commercial-use claims.

---

*Aetheris Browser — issue-reporting guidelines. Authored and maintained by **Dr. Sohil Momin**.*
