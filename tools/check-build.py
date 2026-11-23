#!/usr/bin/env python3
"""Is the binary in front of you the one the tree describes?

Twice in one session a build was killed by the sandbox's time limit, ninja printed nothing anyone read, and the
next `ctest` passed on the *previous* binary. A passing suite that tested last hour's code is worse than a red one,
because it is trusted (docs/CHECKLIST.md row 4d).

Two questions, and it fails on either:

  1. Is any source newer than the binary?      a build that never ran leaves exactly that
  2. Does ninja believe there is work to do?   an interrupted build leaves exactly that

Written as one process with no subprocesses for the source check, because the environment this was first needed in
could not fork - a guard that cannot run is not a guard. The ninja check is best-effort: if ninja is missing or
cannot be spawned it says so and continues.

Usage:
    tools/check-build.py [build-dir] [binary-relative-path] [--quiet]

Exit codes: 0 current, 1 stale, 2 no build directory.
"""

import os
import shutil
import subprocess
import sys

# `tools/` is deliberately absent: nothing in it is compiled into the binaries, and counting it made the guard
# report its own edit as a stale build.
SOURCE_ROOTS = ("src", "Tests")
SOURCE_SUFFIXES = (".cpp", ".hpp", ".h", ".inc", ".txt", ".sh", ".py")


def newest_sources(root):
    """Every file ninja would compile, newest first, as (mtime, path)."""
    found = []
    for name in SOURCE_ROOTS:
        for directory, _, files in os.walk(os.path.join(root, name)):
            for entry in files:
                if entry.endswith(SOURCE_SUFFIXES):
                    path = os.path.join(directory, entry)
                    try:
                        found.append((os.stat(path).st_mtime, path))
                    except OSError:
                        continue
    for name in ("CMakeLists.txt",):
        path = os.path.join(root, name)
        if os.path.exists(path):
            found.append((os.stat(path).st_mtime, path))
    found.sort(reverse=True)
    return found


def pending_steps(build_dir):
    """How many steps ninja would run, or None when ninja cannot be asked."""
    if not shutil.which("ninja"):
        return None
    try:
        result = subprocess.run(["ninja", "-C", build_dir, "-n"], capture_output=True, text=True, timeout=60)
    except (OSError, subprocess.SubprocessError):
        return None
    return sum(1 for line in result.stdout.splitlines() if line.startswith("["))


def main(argv):
    positional = [argument for argument in argv if not argument.startswith("--")]
    quiet = "--quiet" in argv
    build_dir = positional[0] if positional else "build"
    binary = positional[1] if len(positional) > 1 else None

    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    build_path = os.path.join(root, build_dir)

    if not os.path.isdir(build_path):
        print(f"check-build: no build directory at {build_dir}", file=sys.stderr)
        return 2

    # Every binary the build produces, unless one is named. Checking only `aetheris` reported a test-only edit as a
    # stale build, which is the kind of false positive that teaches a reader to ignore the guard.
    if binary:
        targets = [os.path.join(build_path, binary)]
    else:
        bin_dir = os.path.join(build_path, "bin")
        targets = sorted(
            os.path.join(bin_dir, entry) for entry in os.listdir(bin_dir)
            if os.path.isfile(os.path.join(bin_dir, entry))
        ) if os.path.isdir(bin_dir) else []
    if not targets:
        print(f"check-build: no binaries under {build_dir}/bin - nothing has been linked", file=sys.stderr)
        return 1

    status = 0

    steps = pending_steps(os.path.join(root, build_dir))
    if steps is None:
        if not quiet:
            print("check-build: ninja could not be asked, skipping the pending-step check", file=sys.stderr)
    elif steps > 0:
        print(f"check-build: ninja has {steps} step(s) pending - the last build did not finish", file=sys.stderr)
        status = 1

    sources = newest_sources(root)
    if sources:
        # Compare against the *newest* binary: if nothing was linked after the last source edit, the build did not
        # run. Comparing against the oldest instead made a test-only edit report `aetheris` as stale, which is true
        # and useless - aetheris does not depend on the tests.
        newest_mtime, newest_path = sources[0]
        newest_binary = max(targets, key=lambda path: os.stat(path).st_mtime)
        newest_binary_mtime = os.stat(newest_binary).st_mtime
        if newest_binary_mtime < newest_mtime:
            print(f"check-build: nothing was linked after {os.path.relpath(newest_path, root)} changed", file=sys.stderr)
            for mtime, path in sources[:5]:
                if mtime > newest_binary_mtime:
                    print(f"  newer: {os.path.relpath(path, root)}", file=sys.stderr)
            status = 1

    if status == 0 and not quiet:
        print(f"check-build: all {len(targets)} binary/binaries in {build_dir}/bin are current")
    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
