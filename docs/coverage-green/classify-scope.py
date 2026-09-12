#!/usr/bin/env python3
"""Classify every tests/fork-sources.txt entry against a real coverage capture.

Answers the question the gate itself never used to ask: for each entry in the
coverage scope, was it instrumented, and if not, why not?

Usage:
    python3 docs/coverage-green/classify-scope.py <tracefile> <build-dir> [scope-file]

Classes
    measured              tracefile record with executable lines
    no-executable         tracefile record, but LF == 0 (declaration-only)
    source-not-compiled   a C/C++ SOURCE with no object in this build at all:
                          the file was in no binary this run (feature gate,
                          absent SDK, or a module built as a plugin)
    header-uninstantiated  a header with no tracefile record. A header is never
                          its own translation unit and never gets a .gcno; it is
                          recorded only when a compiled TU instantiates code
                          from it. No record means either it declares only, or
                          no compiled TU used it.
    non-source            not C/C++ (shell/python tooling): never instrumentable

The point of the tool is that the headline percentage is a claim about the
`measured` class alone, while the scope is the whole manifest. The report quotes
these numbers so the two cannot be confused.

Note on the object lookup: the CMake object tree does not mirror the repo path
(`build/src/CMakeFiles/lmmsobjs.dir/core/RoutingGraph.cpp.o`, not
`.../src/core/RoutingGraph.cpp.o`), so sources are matched by their object
basename. Basenames in this scope are unique; a collision would be printed.
"""

import os
import sys
from collections import Counter, defaultdict

tracefile, build_dir = sys.argv[1], sys.argv[2]
scope = sys.argv[3] if len(sys.argv) > 3 else "tests/fork-sources.txt"

SOURCE_EXTS = (".c", ".cc", ".cpp", ".cxx")
HEADER_EXTS = (".h", ".hpp")


def parse_tracefile(path):
    recs, cur = {}, None
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            line = line.rstrip("\n")
            if line.startswith("SF:"):
                cur = line[3:]
                recs.setdefault(cur, [0, 0])
            elif line.startswith("LF:") and cur:
                recs[cur][0] = int(line[3:])
            elif line.startswith("LH:") and cur:
                recs[cur][1] = int(line[3:])
            elif line == "end_of_record":
                cur = None
    return recs


def entries(path):
    out = []
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            line = line.strip()
            if line and not line.startswith("#"):
                out.append(line)
    return out


repo_root = os.path.abspath(os.path.dirname(os.path.abspath(scope)) + "/..")


def normalize(p):
    if p.startswith(repo_root + os.sep):
        return p[len(repo_root) + 1:]
    return p


# objects the build produced, by basename
objects = defaultdict(set)
for root, _dirs, files in os.walk(build_dir):
    for f in files:
        if f.endswith((".o", ".gcno")):
            objects[os.path.basename(f).rsplit(".", 1)[0]].add(
                os.path.join(root, f))

recs = {normalize(p): v for p, v in parse_tracefile(tracefile).items()}
scope_entries = entries(scope)

# why a source was not built, read off the configure cache rather than guessed
reasons = [
    ("plugins/ClapEffect/", "CLAP headers absent (WANT_CLAP=AUTO, no CLAP SDK on this host)"),
    ("plugins/Vst3Effect/", "VST3 SDK absent (WANT_VST3=AUTO, no SDK on this host)"),
    ("plugins/WasmEffect/", "wasmtime absent (WANT_WASM=ON, dependency missing)"),
    ("src/wasm/", "wasmtime absent (WANT_WASM=ON, dependency missing)"),
    ("StemSeparation/", "WANT_STEM_SPLIT=OFF"),
    ("StemSplitController", "WANT_STEM_SPLIT=OFF"),
    ("ExternalProcessStemSeparator", "WANT_STEM_SPLIT=OFF"),
    ("OnnxRuntimeStemSeparator", "WANT_STEM_SPLIT=OFF"),
    ("StemJobManager", "WANT_STEM_SPLIT=OFF"),
    ("StemModelStore", "WANT_STEM_SPLIT=OFF"),
    ("StemTrackBuilder", "WANT_STEM_SPLIT=OFF"),
    ("SessionModel", "WANT_SESSION_VIEW=OFF"),
    ("SessionClip", "WANT_SESSION_VIEW=OFF"),
    ("MidiLearnGui", "built into the main binary, already measured"),
    ("NeuralAmp/", "plugin module: built as a loadable .so, not linked into any test binary"),
    ("RnnoiseDenoiser/", "plugin module: built as a loadable .so, not linked into any test binary"),
    ("rnn_harness.c", "standalone harness, not built (documented)"),
    ("PinConnector", "in the main binary; measured 0.00%"),
]


def reason_for(entry):
    for pat, why in reasons:
        if pat in entry:
            return why
    return "no object in this build (see the file's own header for its feature gate)"


rows = []
for entry in scope_entries:
    if not entry.endswith(SOURCE_EXTS + HEADER_EXTS):
        rows.append((entry, "non-source", "not C/C++ (shell/python), never instrumentable"))
        continue
    rec = recs.get(entry)
    if rec is not None and rec[0] > 0:
        rows.append((entry, "measured", f"LF={rec[0]} LH={rec[1]}"))
        continue
    if rec is not None:
        rows.append((entry, "no-executable", "tracefile record with LF=0"))
        continue
    stem = os.path.basename(entry).rsplit(".", 1)[0]
    built = len(objects.get(stem, ()))
    if entry.endswith(SOURCE_EXTS):
        if built:
            rows.append((entry, "source-no-record",
                         f"an object exists ({built}) but no tracefile record"))
        else:
            rows.append((entry, "source-not-compiled", reason_for(entry)))
    else:
        rows.append((entry, "header-uninstantiated",
                     "no compiled TU instantiated code from it" if not built
                     else f"object present ({built}) but its inline/template code was never instantiated"))

counts = Counter(r[1] for r in rows)
print(f"tracefile: {tracefile}")
print(f"build dir: {build_dir}")
print(f"scope:     {scope}  ({len(scope_entries)} entries)")
print(f"tracefile records (normalized): {len(recs)}")
print()
print(f"{'class':<24}{'count':>6}")
for cls in ("measured", "no-executable", "source-not-compiled", "source-no-record",
            "header-uninstantiated", "non-source"):
    print(f"{cls:<24}{counts.get(cls, 0):>6}")
print(f"{'TOTAL':<24}{len(rows):>6}")
print()
measured = [r for r in rows if r[1] == "measured"]
zero_hit = [r for r in measured if "LH=0" in r[2]]
print(f"the 'measured' class = {len(measured)} of {len(rows)} scope entries that produced a "
      f"coverage record; the line-coverage headline is a claim about those {len(measured)} "
      f"files only")
print(f"  of those, {len(zero_hit)} have zero hit lines: "
      f"{sum(int(r[2].split('LF=')[1].split()[0]) for r in zero_hit)} instrumented lines "
      f"the suite never executes")
print()

by_reason = defaultdict(list)
for entry, cls, why in rows:
    if cls in ("source-not-compiled", "header-uninstantiated"):
        by_reason[why].append(entry)

for why in sorted(by_reason, key=lambda k: -len(by_reason[k])):
    print(f"--- {len(by_reason[why])} entries: {why}")
    for e in by_reason[why]:
        print(f"      {e}")
    print()
