#!/usr/bin/env bash
# gate-controls.sh - exercise the 2026-09-12 tests/coverage-gate.sh against a REAL tracefile.
#
# The ratchet was changed in this lane, so the change has to be shown to bite and
# shown not to fabricate. Every control below runs the gate on the tracefile the
# real instrumented build produced; only the baseline or the tracefile is mutated,
# so the control and the measurement differ only in the mutation applied here.
#
#   control  1  real tracefile vs the repo baseline (legacy rows) -> exit 1
#               (the recorded verdict before the migration; nothing mutated)
#   control  2  real tracefile vs a baseline anchored to it      -> exit 0
#               (positive control: the gate is not failing for an environmental
#                reason - parsing, paths, lcov version)
#   control  3  that baseline with one file raised 20 points      -> exit 1
#               (inverted check: the gate can still fail a passing tracefile)
#   control  4  tracefile with src/gui/PinConnector.cpp rewritten to 0 lines
#               -> the documented "unmeasured ... 0 instrumented lines" line
#   control  5  tracefile plus a zero-line record for a scope file absent from
#               the baseline -> admitted as n/a, NOT refused by the floor
#   control  6  THE DEFECT: a file whose instrumented-line count rose while its
#               bytes are unchanged and no covered line was lost -> exit 0, and
#               the output says why (denominator moved)
#   control  7  the same mutation with the file's fingerprint changed -> exit 1
#               (PROOF the strict rule was not relaxed: when the growth is the
#                file's own new code, the original bar still applies)
#   control  8  a baseline entry whose path still exists but produced no record
#               -> "unmeasured-by-config", entry PRESERVED through a write run
#   control  9  a covered line genuinely lost -> exit 1 REGRESSION
#   control 10  --reanchor-file: blank reason -> 2, passing path -> 2,
#               failing path -> 0 with the reason printed and the entry moved
#   control 11  scope accounting: the headline names the measured-file count and
#               the scope-entry count separately, and they differ
#
# Usage: bash docs/coverage-green/gate-controls.sh <repo-root> <tracefile>

set -uo pipefail

ROOT="$(cd "${1:?repo root}" && pwd)"
TRACE="$(cd "$(dirname "${2:?tracefile}")" && pwd)/$(basename "$2")"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
PASS=0; FAIL=0

check() { # check <label> <expected-exit> <actual-exit>
	if [ "$2" = "$3" ]; then echo "  OK   $1 (exit $3)"; PASS=$((PASS+1));
	else echo "  FAIL $1 (expected exit $2, got $3)"; FAIL=$((FAIL+1)); fi
}
check_str() { # check_str <label> <haystack-file> <needle>
	if grep -qF "$3" "$2"; then echo "  OK   $1"; PASS=$((PASS+1));
	else echo "  FAIL $1 (missing: $3)"; FAIL=$((FAIL+1)); fi
}

GATE="$ROOT/tests/coverage-gate.sh"

echo "=== control 1: real tracefile vs the repo baseline (--check) ==="
bash "$GATE" "$TRACE" --check > "$WORK/c1.log" 2>&1
check "recorded verdict on the un-migrated (legacy-row) baseline" 1 "$?"

echo "=== building baselines used by the controls ==="
python3 - "$TRACE" "$ROOT" "$WORK" <<'PY'
import hashlib, os, sys
trace, root, work = sys.argv[1], os.path.abspath(sys.argv[2]), sys.argv[3]
rec = {}; cur = None
for line in open(trace, encoding="utf-8"):
    line = line.rstrip("\n")
    if line.startswith("SF:"):
        cur = line[3:]
        if cur.startswith(root + os.sep): cur = cur[len(root) + 1:]
        rec.setdefault(cur, [0, 0])
    elif line.startswith("LF:") and cur: rec[cur][0] = int(line[3:])
    elif line.startswith("LH:") and cur: rec[cur][1] = int(line[3:])
    elif line == "end_of_record": cur = None

def fp(p):
    try:
        return hashlib.sha256(open(os.path.join(root, p), "rb").read()).hexdigest()[:16]
    except OSError:
        return "-"

with open(os.path.join(work, "anchored.tsv"), "w", encoding="utf-8") as fh:
    fh.write("# control baseline: re-anchored to the real tracefile (generated)\n")
    for p, (lf, lh) in sorted(rec.items()):
        pct = "n/a" if lf == 0 else f"{100.0*lh/lf:.2f}"
        fh.write(f"{p}\t{pct}\t{lf}\t{fp(p)}\n")

# pick a file with LF>0 and <100% for the strict-branch control
low = sorted((p, lf, lh) for p, (lf, lh) in rec.items() if lf > 0 and lh < lf)
with open(os.path.join(work, "pick.txt"), "w", encoding="utf-8") as fh:
    fh.write((low[0][0] if low else "") + "\n")
PY
PICK="$(head -1 "$WORK/pick.txt")"
echo "  (under-100% file used by controls 3 and 7: ${PICK:-none})"

echo "=== control 2: real tracefile vs the anchored baseline (--check) ==="
bash "$GATE" "$TRACE" "$WORK/anchored.tsv" --check > "$WORK/c2.log" 2>&1
check "gate passes a tracefile its baseline was anchored from" 0 "$?"
grep -E "tracefile line coverage|scope:|PASS" "$WORK/c2.log" | sed 's/^/     /'

echo "=== control 3: the anchored baseline with one file raised 20 points ==="
python3 - "$WORK/anchored.tsv" "$WORK/raised.tsv" "$PICK" <<'PY'
import sys
src, dst, pick = sys.argv[1], sys.argv[2], sys.argv[3]
out = []
for line in open(src, encoding="utf-8"):
    if line.startswith("#") or not line.strip():
        out.append(line); continue
    f = line.rstrip("\n").split("\t")
    if f[0] == pick and f[1] != "n/a":
        f[1] = f"{min(100.0, float(f[1]) + 20.0):.2f}"
    out.append("\t".join(f) + "\n")
open(dst, "w", encoding="utf-8").writelines(out)
PY
bash "$GATE" "$TRACE" "$WORK/raised.tsv" --check > "$WORK/c3.log" 2>&1
check "gate still fails a tracefile whose baseline was raised" 1 "$?"
grep -E "^REGRESSION" "$WORK/c3.log" | head -3 | sed 's/^/     /'

echo "=== control 4: a scope file rewritten to zero instrumented lines ==="
python3 - "$TRACE" "$WORK/zero-one.info" "$ROOT/src/gui/PinConnector.cpp" <<'PY'
import sys
src, dst, path = sys.argv[1], sys.argv[2], sys.argv[3]
out = []; cur = None; drop = False; buf = []
for line in open(src, encoding="utf-8"):
    t = line.rstrip("\n")
    if t.startswith("SF:"):
        cur = t[3:]; buf = [line]; drop = (cur == path)
        if not drop: out.extend(buf)
        continue
    if cur is None: out.append(line); continue
    if drop:
        if t.startswith("DA:"): continue
        if t.startswith("LF:"): buf.append("LF:0\n"); continue
        if t.startswith("LH:"): buf.append("LH:0\n"); continue
        if t == "end_of_record":
            out.extend(buf); out.append(line); cur = None; drop = False; continue
        continue
    out.append(line)
open(dst, "w", encoding="utf-8").writelines(out)
PY
bash "$GATE" "$WORK/zero-one.info" --check > "$WORK/c4.log" 2>&1
check "gate reacts to a now-zero-instrumented baseline file" 1 "$?"
check_str "documented unmeasured line present" "$WORK/c4.log" \
	"unmeasured  src/gui/PinConnector.cpp: baseline 0.00% but this run reports 0 instrumented lines"

echo "=== control 5: a scope file absent from the baseline, zero instrumented lines ==="
cp "$TRACE" "$WORK/zero-new.info"
printf 'SF:%s\nLF:0\nLH:0\nend_of_record\n' "$ROOT/include/CrashReporter.h" >> "$WORK/zero-new.info"
bash "$GATE" "$WORK/zero-new.info" "$WORK/anchored.tsv" --check > "$WORK/c5.log" 2>&1
check "zero-line newcomer is admitted as n/a, not refused by the floor" 0 "$?"
check_str "admitted as unmeasurable" "$WORK/c5.log" \
	"new         include/CrashReporter.h: unmeasurable (0 instrumented lines, recorded as n/a)"

echo "=== control 6: THE DEFECT - instrumented lines grew, bytes unchanged, no line lost ==="
python3 - "$TRACE" "$WORK/grew.info" "$PICK" <<'PY'
import sys
src, dst, pick = sys.argv[1], sys.argv[2], sys.argv[3]
out = []
for line in open(src, encoding="utf-8"):
    t = line.rstrip("\n")
    if t.startswith("SF:"):
        out.append(line); cur = t[3:]
        if cur.endswith("/" + pick): grow = True
        else: grow = False
        continue
    if t.startswith("LF:") and grow:
        out.append(f"LF:{int(t[3:]) + 40}\n"); continue   # +40 instrumented lines
    out.append(line)                                      # LH unchanged: no line lost
open(dst, "w", encoding="utf-8").writelines(out)
PY
bash "$GATE" "$WORK/grew.info" "$WORK/anchored.tsv" --check > "$WORK/c6.log" 2>&1
check "no fabricated regression when only the denominator moved" 0 "$?"
check_str "named as a denominator move, not a regression" "$WORK/c6.log" "denominator-moved  $PICK:"
grep -E "denominator-moved|not comparable|instantiations" "$WORK/c6.log" | sed 's/^/     /'

echo "=== control 7: the same growth with the file's own content changed ==="
python3 - "$WORK/anchored.tsv" "$WORK/edited.tsv" "$PICK" <<'PY'
import sys
src, dst, pick = sys.argv[1], sys.argv[2], sys.argv[3]
out = []
for line in open(src, encoding="utf-8"):
    if line.startswith("#") or not line.strip():
        out.append(line); continue
    f = line.rstrip("\n").split("\t")
    if f[0] == pick and len(f) > 3:
        f[3] = "deadbeefdeadbeef"          # different bytes -> the file's own code changed
    out.append("\t".join(f) + "\n")
open(dst, "w", encoding="utf-8").writelines(out)
PY
bash "$GATE" "$WORK/grew.info" "$WORK/edited.tsv" --check > "$WORK/c7.log" 2>&1
check "STRICT rule still applies when the file's own code changed" 1 "$?"
grep -E "^REGRESSION" "$WORK/c7.log" | head -2 | sed 's/^/     /'

echo "=== control 8: a baseline entry with no record, whose path still exists ==="
cp "$WORK/anchored.tsv" "$WORK/config.tsv"
printf 'src/core/StemModelStore.cpp\t62.02\t400\t-  \n' | sed 's/ *$//' >> "$WORK/config.tsv"
bash "$GATE" "$TRACE" "$WORK/config.tsv" --check > "$WORK/c8.log" 2>&1
check "an existing-but-uncompiled entry is not a failure" 0 "$?"
check_str "reported as unmeasured-by-config" "$WORK/c8.log" \
	"unmeasured-by-config  src/core/StemModelStore.cpp:"
cp "$WORK/config.tsv" "$WORK/config-write.tsv"
bash "$GATE" "$TRACE" "$WORK/config-write.tsv" > "$WORK/c8w.log" 2>&1
check "write run accepts it" 0 "$?"
if grep -q "^src/core/StemModelStore.cpp	62.02" "$WORK/config-write.tsv"; then
	echo "  OK   baseline entry PRESERVED through a write run (the old rule deleted it)"
	PASS=$((PASS+1))
else
	echo "  FAIL baseline entry was dropped by the write run"
	FAIL=$((FAIL+1))
fi

echo "=== control 9: a covered line genuinely lost ==="
python3 - "$TRACE" "$WORK/lost.info" "$PICK" <<'PY'
import sys
src, dst, pick = sys.argv[1], sys.argv[2], sys.argv[3]
out = []
for line in open(src, encoding="utf-8"):
    t = line.rstrip("\n")
    if t.startswith("SF:"):
        out.append(line); cur = t[3:]; grow = cur.endswith("/" + pick); continue
    if t.startswith("LH:") and grow:
        out.append(f"LH:{max(0, int(t[3:]) - 5)}\n"); continue   # 5 lines lost
    out.append(line)
open(dst, "w", encoding="utf-8").writelines(out)
PY
bash "$GATE" "$WORK/lost.info" "$WORK/anchored.tsv" --check > "$WORK/c9.log" 2>&1
check "lost covered lines still fail in the like-for-like branch" 1 "$?"
grep -E "^REGRESSION" "$WORK/c9.log" | head -2 | sed 's/^/     /'

echo "=== control 10: --reanchor-file validation ==="
bash "$GATE" "$TRACE" --reanchor-file "$PICK" "" > "$WORK/c10a.log" 2>&1
check "blank reason is refused" 2 "$?"
bash "$GATE" "$TRACE" --reanchor-file "src/core/RoutingGraph.cpp" "not failing" > "$WORK/c10b.log" 2>&1
check "a path that is not failing is refused" 2 "$?"
cp "$WORK/raised.tsv" "$WORK/anchor.tsv"
bash "$GATE" "$TRACE" "$WORK/anchor.tsv" --reanchor-file "$PICK" \
	"control: recorded move for the harness" > "$WORK/c10c.log" 2>&1
check "a failing path re-anchors with a reason" 0 "$?"
check_str "the reason is printed" "$WORK/c10c.log" "reason: control: recorded move for the harness"
python3 - "$WORK/anchor.tsv" "$PICK" <<'PY'
import sys
path = sys.argv[2]
for line in open(sys.argv[1], encoding="utf-8"):
    if line.startswith(path + "	"):
        print("     moved entry now:", line.rstrip())
PY

echo "=== control 11: scope accounting names the measured subset ==="
check_str "scope line present" "$WORK/c2.log" "entries in tests/fork-sources.txt"
check_str "the claim is scoped to the measured files" "$WORK/c2.log" \
	"NOT about the"
grep -E "^scope:|^       the line-coverage" "$WORK/c2.log" | sed 's/^/     /'

echo
echo "gate-controls: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ] || exit 1
exit 0
