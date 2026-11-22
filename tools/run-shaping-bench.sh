#!/bin/sh
# Price one shaped run, in the build type you name, so "shaping is slow" is a number with a place to
# look. Row 2d4 was answered with this: the cost was the font-file hash the face cache took on every
# call, not HarfBuzz.
#
# Usage: tools/run-shaping-bench.sh [build-dir] [font.ttf] [iterations]
#   build-dir   build (Debug, -O0) or build-rel (Release, -O2). Default build-rel.
#   font.ttf    default: the DejaVu Sans that matplotlib ships, so the run is repeatable on a box with
#               no system fonts. AETHERIS_FONT is set for you: without it Font::measure falls back to
#               the bitmap font and looks faster than shape_run, which is a trap this box has sprung.
set -e
HERE=$(cd "$(dirname "$0")/.." && pwd)
BUILD_DIR=${1:-build-rel}
FONT=${2:-}
ITER=${3:-2000}

BUILD="$HERE/$BUILD_DIR"
[ -f "$BUILD/lib/libAetherisRendering.a" ] || {
    echo "no $BUILD_DIR/lib/libAetherisRendering.a - build that directory first" >&2
    exit 1
}

if [ -z "$FONT" ]; then
    FONT=$(python3 -c "import matplotlib, os; print(os.path.join(os.path.dirname(matplotlib.__file__), 'mpl-data/fonts/ttf/DejaVuSans.ttf'))")
fi

case "$BUILD_DIR" in
    *rel*|*Rel*) OPT=-O2 ;;
    *) OPT=-O0 ;;
esac

CXX=${CXX_ZIG:-zigcxx}
# The bench is compiled at the same optimisation as the library it measures: at -O0 the hash loop in
# the bench would otherwise be 20x faster than the library's copy of the same loop, which is a number
# about the compiler, not about the engine.
"$CXX" -std=c++23 "$OPT" -I "$HERE/src/rendering" -I "$HERE/src" -I "$HERE/third_party/harfbuzz/src" \
    "$HERE/tools/shaping-bench.cpp" "$BUILD/lib/libAetherisRendering.a" \
    -L "$BUILD/lib" -lharfbuzz -Wl,-rpath,"$BUILD/lib" -o "$BUILD/shaping-bench"
"$BUILD/shaping-bench" "$FONT" "$ITER"
