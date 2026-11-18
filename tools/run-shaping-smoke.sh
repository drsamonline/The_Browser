#!/bin/sh
# Prove that the HarfBuzz we build here actually shapes - a green `nm` only proves it links.
#
# What this checks, and why each line is a real assertion rather than a print:
#   fi  liga on  -> 1 glyph   the GSUB lookup fired
#   fi  liga off -> 2 glyphs  the count follows the feature, so it is shaping and not cmap lookup
#   fx, ab       -> 2 glyphs  text without a ligature is untouched
#   Arabic 4 cps -> fewer than 4 glyphs   joining/ligature happened in a real script
#
# Usage: tools/run-shaping-smoke.sh [font.ttf]
set -e
HERE=$(cd "$(dirname "$0")/.." && pwd)
HB=${HB_DIR:-$HERE/third_party/harfbuzz}
BUILD=${HB_BUILD:-$HERE/build-harfbuzz}
FONT=${1:-}

[ -f "$HB/src/harfbuzz.cc" ] || { echo "run tools/fetch-harfbuzz.sh first" >&2; exit 1; }

if [ ! -f "$BUILD/libharfbuzz.a" ]; then
    cmake -S "$HB" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_C_COMPILER="${CC_ZIG:-zigcc}" -DCMAKE_CXX_COMPILER="${CXX_ZIG:-zigcxx}" \
        -DCMAKE_C_COMPILER_WORKS=1 -DCMAKE_CXX_COMPILER_WORKS=1 \
        -DCMAKE_CXX_FLAGS="-O0 -fno-exceptions" \
        -DHB_BUILD_SUBSET=OFF -DHB_BUILD_RASTER=OFF -DHB_BUILD_VECTOR=OFF -DHB_BUILD_GPU=OFF \
        -DHB_BUILD_UTILS=OFF -DHB_BUILD_TESTS=OFF -DHB_HAVE_FREETYPE=OFF -DHB_HAVE_GLIB=OFF \
        -DHB_HAVE_ICU=OFF -DHB_HAVE_CAIRO=OFF -DHB_HAVE_GRAPHITE2=OFF >/dev/null
    # ~70 translation units: this may need re-running if a command hits a time limit. Ninja resumes.
    ninja -C "$BUILD" harfbuzz -j2
fi

if [ -z "$FONT" ]; then
    FONT=$(python3 -c "import matplotlib, os; print(os.path.join(os.path.dirname(matplotlib.__file__), 'mpl-data/fonts/ttf/DejaVuSans.ttf'))")
fi

"${CXX_ZIG:-zigcxx}" -std=c++17 -O0 -I "$HB/src" "$HERE/tools/shaping-smoke.cpp" "$BUILD/libharfbuzz.a" -o "$BUILD/shaping-smoke"
"$BUILD/shaping-smoke" "$FONT" | tee "$BUILD/shaping-smoke.log"

python3 - "$BUILD/shaping-smoke.log" <<'PY'
import re, sys
text = open(sys.argv[1]).read()
counts = dict(re.findall(r'^(fi   liga on |fi   liga off|fx |ab |Arabic 4 cps )\s*-> (\d+) glyphs', text, re.M))
# the labels carry trailing spaces; normalise
counts = {k.strip(): int(v) for k, v in counts.items()}
fail = []
if counts.get("fi   liga on") != 1: fail.append("fi with liga on should be 1 glyph, got %s" % counts.get("fi   liga on"))
if counts.get("fi   liga off") != 2: fail.append("fi with liga off should be 2 glyphs, got %s" % counts.get("fi   liga off"))
if counts.get("fx") != 2: fail.append("fx should be 2 glyphs")
if counts.get("ab") != 2: fail.append("ab should be 2 glyphs")
arabic = counts.get("Arabic 4 cps")
if arabic is None or arabic >= 4: fail.append("Arabic should shape 4 code points into fewer glyphs, got %s" % arabic)
print("\n".join("FAILED: " + f for f in fail) if fail else "shaping smoke: ligature, feature toggle and Arabic joining all behave")
sys.exit(1 if fail else 0)
PY
