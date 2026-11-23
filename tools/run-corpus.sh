#!/bin/sh
# The coverage corpus (docs/CHECKLIST.md row 0.1): fetch a fixed list of real sites, render each with the engine, and
# record what breaks.
#
# The fetch is done by curl rather than by the engine because the sandbox reaches the internet through an HTTP proxy
# and the engine's client connects directly. That is a real gap in the engine, recorded in the corpus report - but it
# is a networking gap, and this row is about rendering, so the documents are fetched here and rendered by the engine
# from local files.
#
# Usage: tools/run-corpus.sh [output-directory] [list-file]
#   output-directory  default /scratch/work/corpus
#   list-file         one URL per line; default tools/corpus-urls.txt
#
# Prints one line per site and writes a summary the docs quote. Exits 0 even when sites fail: the point is to record
# what breaks, not to pass.
set -u

HERE=$(cd "$(dirname "$0")/.." && pwd)
OUT=${1:-/scratch/work/corpus}
LIST=${2:-$HERE/tools/corpus-urls.txt}
ENGINE=$HERE/build/bin/aetheris

mkdir -p "$OUT"
: > "$OUT/summary.tsv"
printf 'site\tstatus\tbytes\trender\tpixels\terror\n' >> "$OUT/summary.tsv"

index=0
while IFS= read -r url; do
    case "$url" in ''|'#'*) continue ;; esac
    index=$((index + 1))
    name=$(printf '%02d' "$index")
    page="$OUT/$name.html"

    status=$(curl -sSL --compressed --max-time 25 -o "$page" -w '%{http_code}' "$url" 2>"$OUT/$name.curl" || echo "000")
    bytes=$(wc -c < "$page" 2>/dev/null | tr -d ' ')
    [ -z "$bytes" ] && bytes=0

    if [ "$bytes" -lt 32 ]; then
        printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$url" "$status" "$bytes" "no-body" "0" "fetch produced nothing" >> "$OUT/summary.tsv"
        printf '%-46s %s %6s bytes  no body\n' "$url" "$status" "$bytes"
        continue
    fi

    error=$(cd "$OUT" && timeout 120 "$ENGINE" --render "file://$page" "$OUT/$name.ppm" 2>&1 | tail -2 | tr '\n' ' ')
    if [ -f "$OUT/$name.ppm" ]; then
        pixels=$(wc -c < "$OUT/$name.ppm" | tr -d ' ')
        printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$url" "$status" "$bytes" "ok" "$pixels" "" >> "$OUT/summary.tsv"
        printf '%-46s %s %6s bytes  rendered %s\n' "$url" "$status" "$bytes" "$pixels"
    else
        printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$url" "$status" "$bytes" "failed" "0" "$error" >> "$OUT/summary.tsv"
        printf '%-46s %s %6s bytes  FAILED: %s\n' "$url" "$status" "$bytes" "$error"
    fi
done < "$LIST"

printf '\nsummary: %s\n' "$OUT/summary.tsv"
