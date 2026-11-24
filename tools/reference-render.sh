#!/bin/sh
# Compare this engine's render of a page against a real browser's, and report how far apart they are.
#
# The engine has no reference to be right against: every defect found in the week of 2026-11-23 was found
# because a number contradicted an expectation, and the numbers were running out of expectations. This
# supplies one. Chromium is installed in the sandbox, so the reference is a real render of the same
# document at the same viewport.
#
# Both sides render a **self-contained** copy - the page's stylesheets inlined and its scripts removed -
# so the comparison needs no network and measures layout rather than JavaScript.
#
# Usage:
#   tools/reference-render.sh page.html page.css [output-dir] [width] [height]
#
# Outputs, in the output directory (default /scratch/work/reference):
#   page-compare.html   the self-contained document both sides render
#   reference.png       chromium's render
#   engine.ppm          the engine's render
#   agreement.txt       sampled pixel agreement, and where the rows of text land in each
set -u

PAGE=${1:?usage: tools/reference-render.sh page.html page.css [output-dir] [width] [height]}
CSS=${2:?usage: tools/reference-render.sh page.html page.css [output-dir] [width] [height]}
OUT=${3:-/scratch/work/reference}
WIDTH=${4:-1024}
HEIGHT=${5:-768}
HERE=$(cd "$(dirname "$0")/.." && pwd)
ENGINE=$HERE/build/bin/aetheris

mkdir -p "$OUT" || exit 1
[ -f "$ENGINE" ] || { echo "no $ENGINE - run tools/setup.sh" >&2; exit 1; }
command -v chromium >/dev/null 2>&1 || { echo "no chromium on PATH - no reference to compare against" >&2; exit 1; }

# 1. One document both engines can render: stylesheets inlined, scripts dropped.
python3 - "$PAGE" "$CSS" "$OUT/page-compare.html" <<'PY'
import re, sys
page, css_path, out = sys.argv[1], sys.argv[2], sys.argv[3]
html = open(page, encoding='utf-8', errors='replace').read()
css = open(css_path, encoding='utf-8', errors='replace').read()
# A linked stylesheet cannot be fetched from file://, so every <link> goes and the CSS is inlined once.
html = re.sub(r'(?s)<link[^>]*>', '', html)
html = re.sub(r'(?s)<script.*?</script>', '', html)
html = html.replace('</head>', '<style>' + css + '</style></head>', 1)
open(out, 'w', encoding='utf-8').write(html)
print(f"compare page: {len(html)} bytes ({len(css)} bytes of CSS inlined)")
PY

# 2. The reference, from chromium at the same viewport.
chromium --headless --no-sandbox --disable-gpu --hide-scrollbars \
    --window-size="$WIDTH,$HEIGHT" --virtual-time-budget=15000 \
    --screenshot="$OUT/reference.png" "file://$OUT/page-compare.html" >/dev/null 2>&1 \
    || { echo "chromium could not render the page" >&2; exit 1; }

# 3. The engine, same document, same viewport.
"$ENGINE" --render "file://$OUT/page-compare.html" "$OUT/engine.ppm" >"$OUT/engine.log" 2>&1 \
    || { echo "the engine could not render the page" >&2; exit 1; }

# 4. How far apart they are.
python3 - "$OUT" "$WIDTH" "$HEIGHT" <<'PY' | tee "$OUT/agreement.txt"
import sys
from PIL import Image

out, width, height = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
reference = Image.open(f'{out}/reference.png').convert('RGB')
engine = Image.open(f'{out}/engine.ppm').convert('RGB')
if reference.size != engine.size:
    print(f'sizes differ: reference {reference.size}, engine {engine.size}')
    sys.exit(1)

# Agreement, sampled on a grid so a 1024x768 page is 49k comparisons and not 786k.
same = total = 0
for y in range(0, height, 4):
    for x in range(0, width, 4):
        total += 1
        if reference.getpixel((x, y)) == engine.getpixel((x, y)):
            same += 1
print(f'pixel agreement: {same}/{total} = {100.0 * same / total:.1f}%')

# Where the rows of link-coloured text land: the cheapest way to see a spacing difference as a number.
def link_rows(image):
    rows = []
    for y in range(height):
        hits = 0
        for x in range(0, width, 2):
            r, g, b = image.getpixel((x, y))
            if b > 120 and b - r > 40 and b - g > 40:
                hits += 1
        if hits > 3:
            rows.append(y)
    clusters = []
    for y in rows:
        if clusters and y - clusters[-1][-1] <= 2:
            clusters[-1].append(y)
        else:
            clusters.append([y])
    return [c[0] for c in clusters]

for name, image in (('reference', reference), ('engine', engine)):
    rows = link_rows(image)
    spacing = [b - a for a, b in zip(rows, rows[1:])]
    print(f'{name:10s} text rows at {rows[:10]}')
    print(f'{name:10s} spacing between them {spacing[:8]}')
PY

echo "images: $OUT/reference.png $OUT/engine.ppm"
