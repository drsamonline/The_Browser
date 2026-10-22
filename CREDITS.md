# Credits and attribution

Aetheris Browser is an experimental browser engine built on top of, and derived
from, the work of the **Ladybird** project and its predecessor **SerenityOS**.
We are grateful to those projects and their contributors. This file records what
came from where, so the provenance of every part of the tree is clear.

## Upstream projects

### Ladybird

- Project: Ladybird — https://github.com/LadybirdBrowser/ladybird
- Licence: BSD-2-Clause
- What we use: the browser architecture and the libraries that ship in this tree
  under `AK/`, `Libraries/`, `Base/`, `Meta/`, `Tests/`, and the `Services/`,
  `UI/` and `Utilities/` directories. This includes `LibJS` (the full JavaScript
  engine), `LibWeb`, `LibGfx` (including the image codecs), `LibCore`,
  `LibUnicode`, `LibCompress`, `LibRegex` and the supporting build system.
- Individual file headers retain their original `SPDX-License-Identifier` and
  copyright notices; those notices must not be removed.

### SerenityOS

- Project: SerenityOS — https://github.com/SerenityOS/serenity
- Licence: BSD-2-Clause
- What we use: Ladybird grew out of SerenityOS, so a large part of `AK/`,
  `Libraries/` and `Meta/` originates there. The same provenance rules apply.

## Aetheris additions

The Aetheris-specific work — the standalone rendering foundation in
`src/rendering/`, the prototype runtime in `src/`, the mini JavaScript engine in
`src/js/`, the `Tests/AetherisRendering/` suite, and the build glue — is
maintained by Dr. Sohil Momin and is covered by the repository `LICENSE`.

### Reference use of LibJS

The self-contained JavaScript engine in `src/js/` (the `AetherisJS` library) is
an independent, dependency-free implementation. Its language semantics —
operator behaviour, type conversions, and the string/array/object built-ins —
were written to match **ECMAScript** and were cross-checked against the **LibJS**
implementation that ships in this tree (`Libraries/LibJS`). LibJS itself is a
bytecode VM that requires a Rust bytecode generator, a clang GC plugin and ICU,
so it is not built in the standalone Aetheris profile; where its algorithms
informed `src/js/`, credit is due to the Ladybird/LibJS authors.

## Bundled data and third-party code

| Component | Origin | Licence |
| --- | --- | --- |
| 8x8 bitmap font (`src/rendering/bitmap_font.cpp`) | `font8x8` by Daniel Hepper, based on the IBM VGA public-domain fonts | Public domain |
| DEFLATE inflater (`src/rendering/png.cpp`) | adapted from the public-domain `puff` reference decoder by Mark Adler | Public domain |

## Optional external libraries

These are used only when found at configure time; the build degrades gracefully
without them.

| Library | Used for | Licence |
| --- | --- | --- |
| OpenSSL (`libssl`, `libcrypto`) | HTTPS/TLS transport (`AETHERIS_HAS_TLS`) | Apache-2.0 |
| libwebp | WebP decoding (`AETHERIS_HAS_WEBP`) | BSD-3-Clause |
| LZ4 | Ghost-cache compression (optional) | BSD-2-Clause |
| X11 | Window-manager backend (optional) | MIT |

## Reporting attribution gaps

If any file in this tree is missing an upstream notice, or is credited
incorrectly here, please open an issue so it can be corrected.
