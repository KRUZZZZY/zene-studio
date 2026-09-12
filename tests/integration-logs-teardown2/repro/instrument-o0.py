#!/usr/bin/env python3
"""Instrument one test object's optimisation level (scratch, not committed).

The abort is a lost update on a counter the test's two threads increment with no
atomicity. Its rate is a property of how wide the compiler leaves that
load-add-store window: the release configuration (-O2) needs a rare coincidence,
the coverage configuration (CMAKE_BUILD_TYPE=Debug, -O0 + gcov) reproduced it 3
runs in 10. This recompiles ONE object at a different -O level and relinks, which
gives the same widened window without a second full build.

Usage: instrument-o0.py <build-dir> <source-basename-fragment> [-O0]
"""
import json
import os
import subprocess
import sys

build = sys.argv[1]
frag = sys.argv[2]
opt = sys.argv[3] if len(sys.argv) > 3 else "-O0"

entries = json.load(open(os.path.join(build, "compile_commands.json")))
hit = None
for e in entries:
    if frag in e["file"] and e.get("output"):
        hit = e
if hit is None:
    sys.exit("no compile_commands entry matching " + frag)

cmd = hit["command"].strip()
print("object :", hit["output"])
print("adding :", opt)
res = subprocess.run(cmd + " " + opt, shell=True, cwd=hit["directory"])
print("compile exit:", res.returncode)
sys.exit(res.returncode)
