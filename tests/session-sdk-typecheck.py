#!/usr/bin/env python3
"""Q050-334.2: typecheck the public TypeScript session SDK, its usage example and its test, and build the package.

Runs the package's own TypeScript compiler (tools/zene-session-sdk/node_modules, installed with `npm ci` in that
directory from its committed package-lock.json) - or the one $ZENE_TSC names - twice:
  1. `tsc -p tsconfig.json`: strict typecheck of src/, examples/ and test/ (the example imports the package by its
     published name, resolved through package.json "exports", so it typechecks exactly as a client's code would);
  2. `tsc -p tsconfig.build.json --outDir <temp>`: the publishable build (JavaScript plus .d.ts) compiles.

The test is hermetic: it never installs anything. Without a compiler it exits 77 (ctest Skipped) and says how to
get one.

Usage: python3 tests/session-sdk-typecheck.py <node> <sdk-dir>
Exit code 0 only when both compiler runs passed.
"""

import os
import shutil
import subprocess
import sys
import tempfile

BOUND_S = 300


def find_tsc(sdk):
    named = os.environ.get("ZENE_TSC")
    if named:
        return named
    bundled = os.path.join(sdk, "node_modules", "typescript", "bin", "tsc")
    types = os.path.join(sdk, "node_modules", "@types", "node")
    if os.path.isfile(bundled) and os.path.isdir(types):
        return bundled
    return None


def run(node, tsc, args, sdk):
    command = [node, tsc] + args
    print("running: %s" % " ".join(command), flush=True)
    return subprocess.run(command, cwd=sdk, timeout=BOUND_S, check=False).returncode


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2
    node, sdk = argv[1], os.path.abspath(argv[2])
    tsc = find_tsc(sdk)
    if tsc is None:
        print("SKIP: no TypeScript compiler: run `npm ci` in %s (or set ZENE_TSC)" % sdk)
        return 77
    code = run(node, tsc, ["-p", "tsconfig.json"], sdk)
    if code != 0:
        print("FAIL: the sources, example or test do not typecheck (tsc exit %d)" % code)
        return 1
    print("PASS: src/, examples/ and test/ typecheck")
    out = tempfile.mkdtemp(prefix="zene-sdk-build-")
    try:
        code = run(node, tsc, ["-p", "tsconfig.build.json", "--outDir", out], sdk)
        built = os.path.isfile(os.path.join(out, "index.js")) and os.path.isfile(os.path.join(out, "index.d.ts"))
    finally:
        shutil.rmtree(out, ignore_errors=True)
    if code != 0 or not built:
        print("FAIL: the package build failed (tsc exit %d, index.js and index.d.ts emitted: %s)" % (code, built))
        return 1
    print("PASS: the package builds (JavaScript and declarations)")
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
