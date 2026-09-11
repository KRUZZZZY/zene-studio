#!/usr/bin/env bash
#
# coverage-gate.sh - coverage ratchet for the fork's new code.
#
# Compares per-file line coverage from an lcov tracefile against the
# recorded baseline (tests/coverage-baseline.tsv). The ratchet rule:
#
#   - a file whose coverage DROPS by more than the tolerance fails the gate
#   - a file whose coverage RISES updates the baseline (ratchet up)
#   - a NEW file enters the baseline only if its measured coverage is at or
#     above COVERAGE_ENTRY_FLOOR (default 50.00 percentage points); a new
#     file below the floor fails the gate unless it is declared, with a
#     reason, in tests/coverage-entry-floor-exempt.txt
#   - a file with ZERO instrumented lines has no measurable coverage: it is
#     reported as "unmeasurable" and recorded as "n/a", never as 100.00%
#   - removed files drop out of the baseline
#
# The baseline is the minimum standard for the code as it exists today;
# any coverage-improving change lifts it permanently.
#
# Usage:
#   tests/coverage-gate.sh <tracefile> [baseline-file] [--check]
#
#   <tracefile>      lcov tracefile filtered to the fork's new code
#                    (tests/run-coverage.sh produces one at
#                    <build>/coverage/coverage-fork.info)
#   [baseline-file]  defaults to tests/coverage-baseline.tsv
#   --check          CI mode: never write the baseline, only report
#
# Environment:
#   COVERAGE_TOLERANCE     legacy jitter tolerance in percentage points
#                          (default 0.05; used only for files whose line
#                          counts are unavailable)
#   COVERAGE_ENTRY_FLOOR   new-file entry floor in percentage points
#                          (default 50.00)
#   COVERAGE_JITTER_LINES  covered lines a file may lose without failing
#                          (default 1)
#
# Exit codes: 0 = pass, 1 = regression or below-floor new file, 2 = usage/
# setup error (including a floor exemption with a blank reason).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOLERANCE="${COVERAGE_TOLERANCE:-0.05}"   # percentage points of allowed jitter
ENTRY_FLOOR="${COVERAGE_ENTRY_FLOOR:-50.00}"  # percentage points: new-file floor
EXEMPT_FILE="${SCRIPT_DIR}/coverage-entry-floor-exempt.txt"

if [ $# -lt 1 ] || [ $# -gt 3 ]; then
	echo "usage: $0 <tracefile> [baseline-file] [--check]" >&2
	exit 2
fi

TRACEFILE="$1"
shift
BASELINE="${SCRIPT_DIR}/coverage-baseline.tsv"
CHECK_ONLY=0
for arg in "$@"; do
	case "${arg}" in
		--check) CHECK_ONLY=1 ;;
		*) BASELINE="${arg}" ;;
	esac
done

if [ ! -f "${TRACEFILE}" ]; then
	echo "error: tracefile not found: ${TRACEFILE}" >&2
	echo "(run tests/run-coverage.sh first)" >&2
	exit 2
fi

python3 - "${TRACEFILE}" "${BASELINE}" "${CHECK_ONLY}" "${TOLERANCE}" "${SCRIPT_DIR}/.." \
	"${ENTRY_FLOOR}" "${EXEMPT_FILE}" <<'PYEOF'
import os
import sys

(tracefile, baseline_path, check_only, tolerance, repo_root, entry_floor,
 exempt_path) = (
	sys.argv[1], sys.argv[2], sys.argv[3] == "1", float(sys.argv[4]),
	os.path.abspath(sys.argv[5]), float(sys.argv[6]), sys.argv[7])

def normalize(path):
	# store baseline paths relative to the repo root so the committed
	# baseline is portable between machines/checkout locations
	if path.startswith(repo_root + os.sep):
		return path[len(repo_root) + 1:]
	return path

# --- parse lcov tracefile: per-SF LF/LH pairs -------------------------------
coverage = {}
current = None
with open(tracefile, encoding="utf-8") as fh:
	for line in fh:
		line = line.rstrip("\n")
		if line.startswith("SF:"):
			current = line[3:]
			coverage.setdefault(current, [0, 0])
		elif line.startswith("LF:") and current:
			coverage[current][0] = int(line[3:])
		elif line.startswith("LH:") and current:
			coverage[current][1] = int(line[3:])
		elif line == "end_of_record":
			current = None

if not coverage:
	print("error: tracefile contains no source records", file=sys.stderr)
	sys.exit(2)

now = {}
totals = {}
hits = {}
unmeasurable = []
for path, (total, hit) in coverage.items():
	key = normalize(path)
	totals[key] = total
	hits[key] = hit
	if total == 0:
		# A file with no instrumented lines has NO measurable coverage. Banking
		# it as 100.00% is a claim the tracefile does not support, and the entry
		# survives every later run - so it is recorded as UNMEASURED ("n/a") and
		# reported with that explicit status instead.
		unmeasurable.append(key)
		now[key] = None
	else:
		now[key] = round(100.0 * hit / total, 2)

# --- load baseline -----------------------------------------------------------
# A value is a float percentage, or None for a file recorded as "n/a"
# (present in the tracefile with zero instrumented lines).
baseline = {}
if os.path.exists(baseline_path):
	with open(baseline_path, encoding="utf-8") as fh:
		for line in fh:
			line = line.rstrip("\n")
			if not line or line.startswith("#"):
				continue
			path, _, pct = line.partition("\t")
			pct = pct.strip()
			baseline[path] = None if pct in ("n/a", "N/A", "") else float(pct)

# --- entry floor: exemptions -------------------------------------------------
# A new file enters the baseline at its measured coverage, but the gate refuses
# to admit one that was never measured at a number it cannot defend. Exceptions
# are declared, with a reason, in tests/coverage-entry-floor-exempt.txt
# (path<TAB>reason - the same shape as upstream-modifications.txt).
floor_exempt = {}
if os.path.exists(exempt_path):
	with open(exempt_path, encoding="utf-8") as fh:
		for lineno, line in enumerate(fh, 1):
			line = line.rstrip("\n")
			if not line.strip() or line.lstrip().startswith("#"):
				continue
			path, _, why = line.partition("\t")
			path = path.strip()
			if not path:
				continue
			if not why.strip():
				print(f"error: {exempt_path}:{lineno}: '{path}' has no reason", file=sys.stderr)
				print("a below-floor exemption must say why (this gate refuses to honour a blank one)",
					file=sys.stderr)
				sys.exit(2)
			floor_exempt[path] = why.strip()

# --- ratchet -----------------------------------------------------------------
# Compare HIT LINES, not rounded percentages. One gcov line can flip between
# runs on byte-identical code (thread scheduling, template/inline attribution),
# so a file tolerates up to COVERAGE_JITTER_LINES lost lines (default 1); more
# than that is a real regression. Any net gain raises the baseline.
jitter_lines = int(os.environ.get("COVERAGE_JITTER_LINES", "1"))
regressions, improvements, additions, below_floor, unmeasured_now = [], [], [], [], []
for path in sorted(now):
	if path not in baseline:
		if now[path] is None:
			additions.append(path)              # unmeasurable new file -> "n/a"
		elif now[path] < entry_floor and path not in floor_exempt:
			below_floor.append((path, now[path]))   # refused entry
		else:
			additions.append(path)
		continue
	total = totals.get(path, 0)
	hit = hits.get(path, 0)
	if baseline[path] is None:
		# Recorded as unmeasured ("n/a"): the first run that actually measures
		# it is an admission, so the entry floor applies here too.
		if now[path] is None:
			continue
		if now[path] < entry_floor and path not in floor_exempt:
			below_floor.append((path, now[path]))
		else:
			improvements.append((path, None, now[path]))
		continue
	if now[path] is None:
		# Was measurable in the baseline, now reports zero instrumented lines:
		# not a coverage drop, but not silent either - it is reported.
		unmeasured_now.append((path, baseline[path]))
		continue
	if total:
		expected = round(baseline[path] * total / 100.0)
		lost = expected - hit
		if lost > jitter_lines:
			regressions.append((path, baseline[path], now[path], total, lost))
		elif lost < 0:
			improvements.append((path, baseline[path], now[path]))
	else:
		delta = now[path] - baseline[path]
		if delta < -tolerance:
			regressions.append((path, baseline[path], now[path], 0, 0))
		elif delta > tolerance:
			improvements.append((path, baseline[path], now[path]))
removed = sorted(set(baseline) - set(now))

for path in unmeasurable:
	print(f"unmeasurable  {path}: 0 instrumented lines - no coverage measurable "
		f"(recorded as n/a, never as 100%)")
for path, old, new, total, lost in regressions:
	print(f"REGRESSION  {path}: {old:.2f}% -> {new:.2f}%  ({lost} of {total} covered lines lost)")
for path, old, new in improvements:
	old_s = "n/a" if old is None else f"{old:.2f}%"
	print(f"improved    {path}: {old_s} -> {new:.2f}%")
for path in additions:
	if now[path] is None:
		print(f"new         {path}: unmeasurable (0 instrumented lines, recorded as n/a)")
	elif path in floor_exempt:
		print(f"new         {path}: {now[path]:.2f}% (below the {entry_floor:.2f}% entry floor, "
			f"exempt: {floor_exempt[path]})")
	else:
		print(f"new         {path}: {now[path]:.2f}%")
for path, old in unmeasured_now:
	print(f"unmeasured  {path}: baseline {old:.2f}% but this run reports 0 instrumented lines")
for path in removed:
	print(f"removed     {path}")

# --- report ------------------------------------------------------------------
# Headline is LINE-WEIGHTED (sum of hit lines / sum of instrumented lines), which is
# the standard definition of line coverage. An unweighted mean of per-file percentages
# would let a 2-line 0% file drag the headline as hard as a 269-line one.
total_lines = sum(t for t, _ in coverage.values())
total_hit = sum(h for _, h in coverage.values())
total_pct = (100.0 * total_hit / total_lines) if total_lines else 0.0
print(f"\ntracefile line coverage: {total_pct:.2f}%  ({total_hit}/{total_lines} lines over {len(now)} files)")
print(f"entry floor: {entry_floor:.2f}% for files not yet in the baseline "
	f"(COVERAGE_ENTRY_FLOOR; {len(floor_exempt)} declared exemption(s))")

# The ratchet only compares files the baseline knows about, so report that scope's own
# figure too: for a fork-scoped tracefile the two coincide, for a whole-tree tracefile
# the tracefile figure includes vendored/generated records the baseline never gates.
if baseline:
	scope_files = [p for p in now if p in baseline]
	scope_total = sum(totals[p] for p in scope_files)
	scope_hit = sum(hits[p] for p in scope_files)
	if scope_total:
		print(f"baseline-scope line coverage: {100.0 * scope_hit / scope_total:.2f}%  "
			f"({scope_hit}/{scope_total} lines over {len(scope_files)} files in the baseline)")

if regressions or below_floor:
	if regressions:
		print("\nFAIL: coverage regressed; add or fix tests before merging.", file=sys.stderr)
	if below_floor:
		print(f"\nFAIL: {len(below_floor)} new file(s) entered below the "
			f"{entry_floor:.2f}% coverage entry floor:", file=sys.stderr)
		for path, pct in below_floor:
			print(f"  {path}: {pct:.2f}%", file=sys.stderr)
		print("New code must arrive with tests (or declare a reason in "
			f"{os.path.basename(exempt_path)}):", file=sys.stderr)
		print(f"  {exempt_path}", file=sys.stderr)
	sys.exit(1)

if check_only:
	print("PASS (check mode: baseline not updated)")
	sys.exit(0)

def fmt(p):
	if p not in now:
		return baseline[p]
	if now[p] is None:
		return None
	return now[p]

new_baseline = {}
for p in sorted(set(baseline) | set(now)):
	value = fmt(p)
	new_baseline[p] = value
if new_baseline != baseline or improvements or additions or removed:
	with open(baseline_path, "w", encoding="utf-8") as fh:
		fh.write("# per-file fork-code line coverage baseline (percentage)\n")
		fh.write("# maintained by tests/coverage-gate.sh; do not edit by hand\n")
		fh.write(f"# n/a = present in the tracefile with 0 instrumented lines (unmeasurable);\n")
		fh.write(f"# new files enter at >= {entry_floor:.2f}% (COVERAGE_ENTRY_FLOOR) unless declared in\n")
		fh.write(f"# {os.path.basename(exempt_path)} with a reason.\n")
		for path, pct in new_baseline.items():
			fh.write(f"{path}\t{'n/a' if pct is None else format(pct, '.2f')}\n")
	print(f"baseline updated: {baseline_path}")
else:
	print("PASS (no change)")

sys.exit(0)
PYEOF
