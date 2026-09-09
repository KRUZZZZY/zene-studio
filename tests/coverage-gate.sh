#!/usr/bin/env bash
#
# coverage-gate.sh - coverage ratchet for the fork's new code.
#
# Compares per-file line coverage from an lcov tracefile against the
# recorded baseline (tests/coverage-baseline.tsv). The ratchet rule:
#
#   - a file whose coverage DROPS by more than the tolerance fails the gate
#   - a file whose coverage RISES updates the baseline (ratchet up)
#   - new files enter the baseline at their current coverage
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
# Exit codes: 0 = pass, 1 = regression found, 2 = usage/setup error.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOLERANCE="${COVERAGE_TOLERANCE:-0.05}"   # percentage points of allowed jitter

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

python3 - "${TRACEFILE}" "${BASELINE}" "${CHECK_ONLY}" "${TOLERANCE}" "${SCRIPT_DIR}/.." <<'PYEOF'
import os
import sys

tracefile, baseline_path, check_only, tolerance, repo_root = (
	sys.argv[1], sys.argv[2], sys.argv[3] == "1", float(sys.argv[4]),
	os.path.abspath(sys.argv[5]))

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
for path, (total, hit) in coverage.items():
	now[normalize(path)] = 100.0 if total == 0 else round(100.0 * hit / total, 2)

# --- load baseline -----------------------------------------------------------
baseline = {}
if os.path.exists(baseline_path):
	with open(baseline_path, encoding="utf-8") as fh:
		for line in fh:
			line = line.rstrip("\n")
			if not line or line.startswith("#"):
				continue
			path, _, pct = line.partition("\t")
			baseline[path] = float(pct)

# --- ratchet -----------------------------------------------------------------
regressions, improvements, additions = [], [], []
for path in sorted(now):
	if path not in baseline:
		additions.append(path)
		continue
	delta = now[path] - baseline[path]
	if delta < -tolerance:
		regressions.append((path, baseline[path], now[path]))
	elif delta > tolerance:
		improvements.append((path, baseline[path], now[path]))
removed = sorted(set(baseline) - set(now))

for path, old, new in regressions:
	print(f"REGRESSION  {path}: {old:.2f}% -> {new:.2f}%")
for path, old, new in improvements:
	print(f"improved    {path}: {old:.2f}% -> {new:.2f}%")
for path in additions:
	print(f"new         {path}: {now[path]:.2f}%")
for path in removed:
	print(f"removed     {path}")

# --- report ------------------------------------------------------------------
# Headline is LINE-WEIGHTED (sum of hit lines / sum of instrumented lines), which is
# the standard definition of line coverage. An unweighted mean of per-file percentages
# would let a 2-line 0% file drag the headline as hard as a 269-line one.
total_lines = sum(t for t, _ in coverage.values())
total_hit = sum(h for _, h in coverage.values())
total_pct = (100.0 * total_hit / total_lines) if total_lines else 0.0
print(f"\nfiles: {len(now)}  fork-code line coverage: {total_pct:.2f}%  ({total_hit}/{total_lines} lines)")

if regressions:
	print("\nFAIL: coverage regressed; add or fix tests before merging.", file=sys.stderr)
	sys.exit(1)

if check_only:
	print("PASS (check mode: baseline not updated)")
	sys.exit(0)

if improvements or additions or removed:
	new_baseline = {p: (now[p] if p in now else baseline[p]) for p in sorted(set(baseline) | set(now))}
	with open(baseline_path, "w", encoding="utf-8") as fh:
		fh.write("# per-file fork-code line coverage baseline (percentage)\n")
		fh.write("# maintained by tests/coverage-gate.sh; do not edit by hand\n")
		for path, pct in new_baseline.items():
			fh.write(f"{path}\t{pct:.2f}\n")
	print(f"baseline updated: {baseline_path}")
else:
	print("PASS (no change)")

sys.exit(0)
PYEOF
