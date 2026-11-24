#!/bin/sh
# One command to make this sandbox able to build, after a pod recycle.
#
# The pod is a fresh container most days. /scratch/work survives with the repo and both build trees,
# but everything pip installed does not, so toolchain/zigcc points at a zig that is gone and every
# build fails with `exec: .../zig: not found`. The CMake caches do not survive either, so the first
# ninja after a recycle rebuilds all 300 steps.
#
# Usage:
#   tools/setup.sh            # toolchain + both build dirs, configured and built
#   tools/setup.sh --check    # report what is missing and change nothing
#   tools/setup.sh --no-build # configure only
set -u

HERE=$(cd "$(dirname "$0")/.." && pwd)
TOOLCHAIN=${AETHERIS_TOOLCHAIN:-/scratch/work/toolchain}
MODE=full
for arg in "$@"; do
    case "$arg" in
        --check) MODE=check ;;
        --no-build) MODE=configure ;;
        *) echo "unknown argument: $arg" >&2; exit 2 ;;
    esac
done

say() { printf '%s\n' "$*"; }
fail() { printf 'setup: %s\n' "$*" >&2; exit 1; }

# 1. The compiler. `ziglang` ships a whole clang; the wrappers are what CMake was configured with.
if [ "$MODE" = check ]; then
    if [ -x "$TOOLCHAIN/zigcc" ] && "$TOOLCHAIN/zigcc" --version >/dev/null 2>&1; then
        say "toolchain: ok ($("$TOOLCHAIN/zigcc" --version | head -1))"
    else
        say "toolchain: MISSING - run tools/setup.sh"
    fi
else
    say "toolchain: installing ziglang, cmake, ninja"
    pip install -q ziglang cmake ninja || fail "pip install failed"
    ZIG=$(python3 -c 'import ziglang,os;print(os.path.join(os.path.dirname(ziglang.__file__),"zig"))') || fail "cannot find the zig binary"
    mkdir -p "$TOOLCHAIN"
    printf '#!/bin/sh\nexec %s cc "$@"\n' "$ZIG" > "$TOOLCHAIN/zigcc"
    printf '#!/bin/sh\nexec %s c++ "$@"\n' "$ZIG" > "$TOOLCHAIN/zigcxx"
    chmod +x "$TOOLCHAIN/zigcc" "$TOOLCHAIN/zigcxx"
    say "toolchain: ok ($("$TOOLCHAIN/zigcc" --version | head -1))"
fi
export PATH="$TOOLCHAIN:/scratch/.local/bin:$PATH"

# 2. HarfBuzz, which is fetched rather than vendored.
if [ ! -f "$HERE/third_party/harfbuzz/src/hb-version.h" ]; then
    if [ "$MODE" = check ]; then
        say "harfbuzz: MISSING - run tools/setup.sh"
    else
        say "harfbuzz: fetching"
        (cd "$HERE" && sh tools/fetch-harfbuzz.sh) || fail "harfbuzz fetch failed"
    fi
else
    say "harfbuzz: ok"
fi

# 3. The two build directories. A recycle invalidates the CMake cache, which is why a tree full of
#    binaries can still report 300 pending steps.
configure() {
    dir=$1; shift
    if [ -f "$HERE/$dir/CMakeCache.txt" ] && [ "$MODE" != configure ]; then
        say "$dir: cache present"
        return 0
    fi
    say "$dir: configuring"
    (cd "$HERE" && cmake -S . -B "$dir" -G Ninja \
        -DCMAKE_C_COMPILER="$TOOLCHAIN/zigcc" -DCMAKE_CXX_COMPILER="$TOOLCHAIN/zigcxx" \
        -DCMAKE_C_COMPILER_WORKS=1 -DCMAKE_CXX_COMPILER_WORKS=1 \
        -DAETHERIS_STANDALONE_BUILD=ON \
        -DOPENSSL_INCLUDE_DIR=/usr/include/node \
        -DOPENSSL_SSL_LIBRARY=/usr/lib/x86_64-linux-gnu/libssl.so.3 \
        -DOPENSSL_CRYPTO_LIBRARY=/usr/lib/x86_64-linux-gnu/libcrypto.so.3 \
        -DAETHERIS_WEBP_LIBRARY=/usr/lib/x86_64-linux-gnu/libwebp.so.7 \
        '-DCMAKE_CXX_FLAGS=-Wno-error=#warnings' "$@" >/dev/null) || fail "$dir: configure failed"
}

configure build -DCMAKE_BUILD_TYPE=Debug -DAETHERIS_ENABLE_TESTING=ON -DBUILD_TESTING=ON
configure build-rel -DCMAKE_BUILD_TYPE=Release -DAETHERIS_ENABLE_TESTING=OFF -DBUILD_TESTING=OFF

# 4. Build. Serial-ish on purpose: this box has two cores and a hard thread limit, and `-j4` has
#    produced failures that looked like test bugs.
if [ "$MODE" = full ]; then
    say "building (this is the long part: ~300 steps on two cores)"
    ninja -C "$HERE/build" -j2 || fail "build failed"
    ninja -C "$HERE/build-rel" -j2 aetheris || fail "release build failed"
    python3 "$HERE/tools/check-build.py"
elif [ "$MODE" = check ]; then
    python3 "$HERE/tools/check-build.py" || true
fi

say "setup: done"
