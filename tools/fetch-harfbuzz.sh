#!/bin/sh
# Fetch the HarfBuzz source this engine shapes with, without needing GitHub, apt, meson or autotools.
#
# The route was chosen by measurement, not preference. Upstream's own CMake builds HarfBuzz as ~70
# separate translation units, which matters here: a command may not exceed 300 s, and 70 units at
# -j2 resume across that limit (a killed build loses only the in-flight objects). The single-TU
# amalgamation src/harfbuzz.cc is all-or-nothing against the same cap.
#
# The uharfbuzz sdist is used only as a carrier: it vendors the whole harfbuzz/ tree, including
# upstream's CMakeLists.txt, meson.build, hb-features.h.in and the .pc.in files that CMake reads at
# configure time. Nothing from the Python package is used or built.
set -e
VERSION=0.57.0
DEST=${1:-third_party/harfbuzz}
# Not mktemp -d: /tmp is a 64 MB tmpfs on this box and the sdist is 38.9 MB before extraction.
WORK=$(mktemp -d "${TMPDIR:-$(dirname "$(pwd)")}/hb-fetch.XXXXXX")

python3 - "$VERSION" "$WORK" <<'PY'
import json, sys, urllib.request
version, work = sys.argv[1], sys.argv[2]
meta = json.load(urllib.request.urlopen("https://pypi.org/pypi/uharfbuzz/json"))
sdist = next(u for u in meta["urls"] if u["packagetype"] == "sdist")
urllib.request.urlretrieve(sdist["url"], work + "/uharfbuzz.tar.gz")
print("fetched", sdist["filename"], sdist["size"] // 1024, "KB")
PY

tar xf "$WORK/uharfbuzz.tar.gz" -C "$WORK"
mkdir -p "$(dirname "$DEST")"
rm -rf "$DEST"
cp -r "$WORK"/uharfbuzz-*/harfbuzz "$DEST"
rm -rf "$WORK"

test -f "$DEST/src/hb-version.h" || { echo "no hb-version.h: not the tree we expect" >&2; exit 1; }
test -f "$DEST/CMakeLists.txt" || { echo "no CMakeLists.txt: upstream CMake route unavailable" >&2; exit 1; }
echo "harfbuzz $(sed -n 's/.*HB_VERSION_STRING \"\(.*\)\"/\1/p' "$DEST/src/hb-version.h") in $DEST"
