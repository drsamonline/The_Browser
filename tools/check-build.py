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

SOURCE_ROOTS = ("src", "Tests", "tools")
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
    binary = positional[1] if len(positional) > 1 else "bin/aetheris"

    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    target = os.path.join(root, build_dir, binary)

    if not os.path.isdir(os.path.join(root, build_dir)):
        print(f"check-build: no build directory at {build_dir}", file=sys.stderr)
        return 2
    if not os.path.isfile(target):
        print(f"check-build: {binary} does not exist - the build has never linked it", file=sys.stderr)
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
        target_mtime = os.stat(target).st_mtime
        newer = [(mtime, path) for mtime, path in sources if mtime > target_mtime]
        if newer:
            print(f"check-build: {binary} is older than {len(newer)} source file(s)", file=sys.stderr)
            for _, path in newer[:5]:
                print(f"  newer: {os.path.relpath(path, root)}", file=sys.stderr)
            status = 1

    if status == 0 and not quiet:
        print(f"check-build: {binary} is current")
    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
