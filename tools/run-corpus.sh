#!/bin/sh
# The coverage corpus (docs/CHECKLIST.md row 0.1): fetch a fixed list of real sites, render each with the engine, and
# record what breaks.
#
# Each site is rendered **from its URL**, so the page's relative subresources resolve against the real origin and
# the engine fetches them the way a browser would (row 0.6). That needs the engine to reach the network, which it can
# through the environment's proxy (row 0.3).
#
# The curl-and-render-a-local-file path is kept as a **fallback** for a host the proxy refuses. It is a weaker
# measurement and the summary says so: rendering `file://<path>` resolves every relative URL against `file://`, so the
# page's own stylesheets, images and scripts are never fetched - wikipedia loads 41 subresources from its URL and 0
# from a local copy.
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
printf 'site\tstatus\tbytes\tmode\tresources\tpixels\tresult\terror\n' >> "$OUT/summary.tsv"

index=0
while IFS= read -r url; do
    case "$url" in ''|'#'*) continue ;; esac
    index=$((index + 1))
    name=$(printf '%02d' "$index")
    page="$OUT/$name.html"

    status=$(curl -sSL --compressed --max-time 25 -o "$page" -w '%{http_code}' "$url" 2>"$OUT/$name.curl" || echo "000")
    bytes=$(wc -c < "$page" 2>/dev/null | tr -d ' ')
    [ -z "$bytes" ] && bytes=0

    # Render from the URL. The output line says how many subresources the engine actually loaded, which is the
    # number this row exists to record.
    mode=url
    error=$(cd "$OUT" && timeout 180 "$ENGINE" --render "$url" "$OUT/$name.ppm" 2>&1 | tail -3 | tr '\n' ' ')

    if [ ! -f "$OUT/$name.ppm" ]; then
        # Fallback: fetch with curl and render the local copy. Weaker, and marked as such.
        mode=file
        status=$(curl -sSL --compressed --max-time 25 -o "$page" -w '%{http_code}' "$url" 2>"$OUT/$name.curl" || echo "000")
        bytes=$(wc -c < "$page" 2>/dev/null | tr -d ' ')
        [ -z "$bytes" ] && bytes=0
        # A refused host is not a page. The proxy answers 502 with a short error body, and rendering that body
        # produced a summary line that said "rendered" for a site that never loaded.
        case "$status" in
            2*) ;;
            *)
                printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$url" "$status" "$bytes" "file" "0" "0" "refused" "the proxy did not serve this host" >> "$OUT/summary.tsv"
                printf '%-46s %s %6s bytes  refused by the proxy\n' "$url" "$status" "$bytes"
                continue
                ;;
        esac
        if [ "$bytes" -lt 32 ]; then
            printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$url" "$status" "$bytes" "file" "0" "0" "no-body" "fetch produced nothing" >> "$OUT/summary.tsv"
            printf '%-46s %s %6s bytes  no body\n' "$url" "$status" "$bytes"
            continue
        fi
        error=$(cd "$OUT" && timeout 120 "$ENGINE" --render "file://$page" "$OUT/$name.ppm" 2>&1 | tail -2 | tr '\n' ' ')
    else
        status=200
        bytes=$(wc -c < "$page" 2>/dev/null | tr -d ' ')
        [ -z "$bytes" ] && bytes=0
    fi

    # "… (41 linked resources)" from the engine's own report.
    resources=$(printf '%s' "$error" | sed -n 's/.*(\([0-9][0-9]*\) linked resources).*/\1/p')
    [ -z "$resources" ] && resources=0

    if [ -f "$OUT/$name.ppm" ]; then
        pixels=$(wc -c < "$OUT/$name.ppm" | tr -d ' ')
        printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$url" "$status" "$bytes" "$mode" "$resources" "$pixels" "ok" "" >> "$OUT/summary.tsv"
        printf '%-46s %s %6s bytes  %-4s %3s resources  rendered\n' "$url" "$status" "$bytes" "$mode" "$resources"
    else
        printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$url" "$status" "$bytes" "$mode" "$resources" "0" "failed" "$error" >> "$OUT/summary.tsv"
        printf '%-46s %s %6s bytes  %-4s FAILED: %s\n' "$url" "$status" "$bytes" "$mode" "$error"
    fi
done < "$LIST"

printf '\nsummary: %s\n' "$OUT/summary.tsv"
