#!/usr/bin/env bash
#
# coverage-gate.sh - coverage ratchet for the fork's new code.
#
# Compares per-file line coverage from an lcov tracefile against the
# recorded baseline (tests/coverage-baseline.tsv). The ratchet rule:
#
#   - a file whose coverage DROPS fails the gate
#   - a file whose coverage RISES updates the baseline (ratchet up)
#   - a NEW file enters the baseline only if its measured coverage is at or
#     above COVERAGE_ENTRY_FLOOR (default 50.00 percentage points); a new
#     file below the floor fails the gate unless it is declared, with a
#     reason, in tests/coverage-entry-floor-exempt.txt
#   - a file with ZERO instrumented lines has no measurable coverage: it is
#     reported as "unmeasurable" and recorded as "n/a", never as 100.00%
#   - a file that still exists but produced no tracefile record was NOT
#     COMPILED in this configuration: it is reported as "unmeasured-by-config",
#     its baseline entry is PRESERVED, and it is never silently dropped
#   - removed files (no record, and the path is gone) drop out of the baseline
#
# The baseline is the minimum standard for the code as it exists today;
# any coverage-improving change lifts it permanently.
#
# WHY THE BASELINE STORES MORE THAN A PERCENTAGE (2026-09-12)
#   The ratchet used to reconstruct "the hit lines the baseline expects" as
#   `round(baseline_pct * instrumented_lines_now)`. That is a like-for-like
#   comparison only while the instrumented-line count is stable - but LF is a
#   *compile-time* quantity (it comes from the .gcno files: a line counts once
#   some translation unit instantiates it) while LH is a *run-time* one. A
#   header full of templates therefore gains instrumented lines whenever any
#   including TU is added, with no edit to the header at all, and the old
#   arithmetic turned that into a fabricated regression: include/AudioPorts.h is
#   byte-identical to the commit the baseline was taken at
#   (`git diff 961052a0c HEAD -- include/AudioPorts.h` is empty) yet was
#   reported as 81 covered lines lost, because 233 instrumented lines replaced
#   the (smaller) set the recorded percentage had been divided into. A gate that
#   fails on the compiler's instantiation set is not measuring coverage, and the
#   only "fix" for it would be tests written to move a number.
#
#   Every baseline row therefore now carries the instrumented-line count (LF)
#   and a content fingerprint beside the percentage:
#
#       <path><TAB><pct|n/a><TAB><lf|-><TAB><sha256-16|->
#
#   and the comparison is chosen by what actually moved:
#
#   * LF unchanged                     -> the original hit-line ratchet, exactly
#                                         as before (this is every stable file).
#   * LF moved, bytes unchanged        -> the file's own code is identical, so a
#                                         ratio across the two LF values is not a
#                                         comparison of anything. Judged on
#                                         COVERED LINES: a covered line may never
#                                         be lost. Reported as `denominator-moved`,
#                                         never silently.
#   * LF moved, bytes changed, header  -> both causes are present at once (the
#                                         file's own edit AND the instantiations
#                                         of whatever includes it) and the gate
#                                         cannot separate them, so it will not
#                                         claim a regression it cannot justify:
#                                         covered lines are still held hard, and
#                                         the entry is `REANCHOR-REQUIRED` - it
#                                         fails, and a human records a reason
#                                         with --reanchor-file to reconcile it.
#   * LF moved, bytes changed, not .h  -> a source file's LF is its own lines, so
#                                         growth is its own new code: the
#                                         ORIGINAL strict rule applies unchanged
#                                         (new code must arrive covered at the
#                                         recorded rate). A REGRESSION.
#   * LF unknown (legacy 2-column row) -> the original arithmetic, and the row is
#                                         named as legacy in the output.
#
#   Covered lines are held in EVERY branch: a genuine loss of executed lines
#   fails whichever way LF moved. Nothing here lowers a target.
#
# Usage:
#   tests/coverage-gate.sh <tracefile> [baseline-file] [--check]
#   tests/coverage-gate.sh <tracefile> [baseline-file] --reanchor-file <path> "reason"
#
#   <tracefile>      lcov tracefile filtered to the fork's new code
#                    (tests/run-coverage.sh produces one at
#                    <build>/coverage/coverage-fork.info)
#   [baseline-file]  defaults to tests/coverage-baseline.tsv
#   --check          CI mode: never write the baseline, only report
#   --reanchor-file  move exactly ONE failing entry to its measured value, with a
#                    recorded reason. Mirrors tests/complexity-gate.sh and
#                    tests/file-length-gate.sh. Refused (exit 2) on a blank
#                    reason, and refused (exit 2) when the named path is not
#                    failing - a re-anchor is a decision with a reason, not a
#                    silencer, and it cannot move a passing entry.
#
# Environment:
#   COVERAGE_TOLERANCE     jitter tolerance in percentage points (default 0.05;
#                          used only for files whose instrumented-line count is
#                          unknown)
#   COVERAGE_ENTRY_FLOOR   new-file entry floor in percentage points (default 50.00)
#   COVERAGE_JITTER_LINES  covered lines a file may lose without failing (default 1)
#
# Exit codes: 0 = pass, 1 = regression / below-floor new file / entry requiring a
#             recorded re-anchor, 2 = usage/setup error (including a floor
#             exemption with a blank reason, and a re-anchor with a blank reason
#             or a path that is not failing).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOLERANCE="${COVERAGE_TOLERANCE:-0.05}"   # percentage points of allowed jitter
ENTRY_FLOOR="${COVERAGE_ENTRY_FLOOR:-50.00}"  # percentage points: new-file floor
EXEMPT_FILE="${SCRIPT_DIR}/coverage-entry-floor-exempt.txt"
SCOPE_FILE="${SCRIPT_DIR}/fork-sources.txt"

TRACEFILE=""
BASELINE="${SCRIPT_DIR}/coverage-baseline.tsv"
CHECK_ONLY=0
REANCHOR_SPECS=()
positional=0
while [ $# -gt 0 ]; do
	case "$1" in
		--check) CHECK_ONLY=1; shift ;;
		--reanchor-file)
			rn_path="${2:-}"
			rn_reason="${3:-}"
			if [ -z "${rn_path// /}" ] || [ -z "${rn_reason// /}" ]; then
				echo "usage: $0 <tracefile> [baseline-file] --reanchor-file <path> \"reason\"" >&2
				echo "an unrecorded re-anchor is not allowed" >&2
				exit 2
			fi
			REANCHOR_SPECS+=("${rn_path}"$'	'"${rn_reason}")
			shift 3
			;;
		-h|--help) sed -n '1,120p' "$0" >&2; exit 0 ;;
		-*) echo "usage: $0 <tracefile> [baseline-file] [--check|--reanchor-file <path> \"reason\"]" >&2; exit 2 ;;
		*)
			if [ $positional -eq 0 ]; then TRACEFILE="$1"; else BASELINE="$1"; fi
			positional=$((positional + 1))
			shift
			;;
	esac
done

if [ -z "${TRACEFILE}" ]; then
	echo "usage: $0 <tracefile> [baseline-file] [--check|--reanchor-file <path> \"reason\"]" >&2
	exit 2
fi
if [ "${CHECK_ONLY}" -eq 1 ] && [ ${#REANCHOR_SPECS[@]} -gt 0 ]; then
	echo "error: --check and --reanchor-file are mutually exclusive (a re-anchor writes the baseline)" >&2
	exit 2
fi
if [ ! -f "${TRACEFILE}" ]; then
	echo "error: tracefile not found: ${TRACEFILE}" >&2
	echo "(run tests/run-coverage.sh first)" >&2
	exit 2
fi

# The re-anchor decisions are passed as a file so any number of them can be
# recorded in one run: a baseline can hold several entries that each need their
# own reason, and clearing them one per run would need a write per reason.
REANCHOR_FILE=""
if [ ${#REANCHOR_SPECS[@]} -gt 0 ]; then
	REANCHOR_FILE="$(mktemp)"
	trap 'rm -f "${REANCHOR_FILE}"' EXIT
	printf '%s\n' "${REANCHOR_SPECS[@]}" > "${REANCHOR_FILE}"
fi

python3 - "${TRACEFILE}" "${BASELINE}" "${CHECK_ONLY}" "${TOLERANCE}" "${SCRIPT_DIR}/.." \
	"${ENTRY_FLOOR}" "${EXEMPT_FILE}" "${REANCHOR_FILE}" "${SCOPE_FILE}" <<'PYEOF'
import hashlib
import os
import sys
import tempfile

(tracefile, baseline_path, check_only, tolerance, repo_root, entry_floor,
 exempt_path, reanchor_file, scope_path) = (
	sys.argv[1], sys.argv[2], sys.argv[3] == "1", float(sys.argv[4]),
	os.path.abspath(sys.argv[5]), float(sys.argv[6]), sys.argv[7], sys.argv[8],
	sys.argv[9])

# path -> reason, in the order the caller recorded them
reanchor = []
if reanchor_file:
	with open(reanchor_file, encoding="utf-8") as fh:
		for line in fh:
			line = line.rstrip("\n")
			if not line.strip():
				continue
			path, _, why = line.partition("	")
			path, why = path.strip(), why.strip()
			if not path or not why:
				print("error: a re-anchor entry has a blank path or reason", file=sys.stderr)
				sys.exit(2)
			reanchor.append((path, why))


def normalize(path):
	# store baseline paths relative to the repo root so the committed
	# baseline is portable between machines/checkout locations
	if path.startswith(repo_root + os.sep):
		return path[len(repo_root) + 1:]
	return path


def fingerprint(rel):
	"""sha256 (first 16 hex) of the file's bytes, or None if unreadable.

	Stored so the gate can tell a file whose own code changed from one whose
	instrumented-line count moved because a *different set of translation units*
	was compiled. Nothing in a tracefile distinguishes those, and they need
	opposite rules.
	"""
	try:
		with open(os.path.join(repo_root, rel), "rb") as fh:
			return hashlib.sha256(fh.read()).hexdigest()[:16]
	except OSError:
		return None


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
# A row is `path<TAB>pct<TAB>lf<TAB>hash`. `pct` is a float or "n/a"; `lf` and
# `hash` are "-" when unknown (a legacy 2-column row carries neither).
baseline = {}
legacy_rows = []
if os.path.exists(baseline_path):
	with open(baseline_path, encoding="utf-8") as fh:
		for line in fh:
			line = line.rstrip("\n")
			if not line or line.startswith("#"):
				continue
			fields = line.split("\t")
			path = fields[0]
			pct = fields[1].strip() if len(fields) > 1 else ""
			lf = fields[2].strip() if len(fields) > 2 else "-"
			fp = fields[3].strip() if len(fields) > 3 else "-"
			baseline[path] = {
				"pct": None if pct in ("n/a", "N/A", "") else float(pct),
				"lf": None if lf in ("-", "", "n/a") else int(lf),
				"hash": None if fp in ("-", "") else fp,
			}
			if baseline[path]["lf"] is None:
				legacy_rows.append(path)

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
improvements, additions, below_floor = [], [], []
regressions = []        # hard failures: executed lines were lost, or new source
                        # code entered under-covered
needs_anchor = []       # the gate cannot form a like-for-like bar; fails until a
                        # human records a reason with --reanchor-file
unmeasured_by_config, unmeasured_now, denominator_moved = [], [], []
legacy_compared = []


def add_regression(path, pct_b, pct_n, lf_b, lf_n, lh_b, lh_n, why):
	regressions.append({
		"path": path, "pct_b": pct_b, "pct_n": pct_n,
		"lf_b": lf_b, "lf_n": lf_n, "lh_b": lh_b, "lh_n": lh_n, "why": why,
	})


for path in sorted(now):
	if path not in baseline:
		if now[path] is None:
			additions.append(path)              # unmeasurable new file -> "n/a"
		elif now[path] < entry_floor and path not in floor_exempt:
			below_floor.append((path, now[path]))   # refused entry
		else:
			additions.append(path)
		continue
	entry = baseline[path]
	pct_b, lf_b, hash_b = entry["pct"], entry["lf"], entry["hash"]
	total = totals.get(path, 0)
	hit = hits.get(path, 0)
	if pct_b is None:
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
		unmeasured_now.append((path, pct_b))
		continue

	if lf_b is None:
		# Legacy row: the denominator's history is unknown, so the only
		# comparison available is the one this gate used before the LF column
		# existed. Named as legacy so nobody mistakes it for like-for-like.
		expected = round(pct_b * total / 100.0)
		lost = expected - hit
		legacy_compared.append(path)
		if lost > jitter_lines:
			add_regression(path, pct_b, now[path], None, total, expected, hit,
				"legacy row: no instrumented-line count was recorded when this "
				"baseline was written, so the recorded percentage cannot be compared "
				"like-for-like (re-record it with --reanchor-file and a reason)")
		elif lost < 0:
			improvements.append((path, pct_b, now[path]))
		continue

	hit_at_baseline = round(pct_b * lf_b / 100.0)
	if lf_b == total:
		# Like-for-like: the instrumented-line count did not move, so the ratio
		# comparison is exact and this branch is the rule this gate always had.
		lost = hit_at_baseline - hit
		if lost > jitter_lines:
			add_regression(path, pct_b, now[path], lf_b, total, hit_at_baseline, hit,
				"coverage fell")
		elif lost < 0:
			improvements.append((path, pct_b, now[path]))
		continue

	# The instrumented-line count moved. First, the hard bar that holds in every
	# branch: covered lines may never be lost.
	if hit < hit_at_baseline - jitter_lines:
		add_regression(path, pct_b, now[path], lf_b, total, hit_at_baseline, hit,
			f"covered lines fell (LF {lf_b} -> {total})")
		continue

	unchanged_content = hash_b is not None and hash_b == fingerprint(path)
	if unchanged_content:
		# The file's own code is identical; the extra instrumented lines are
		# instantiations the compiler now sees and this run does not execute. No
		# covered line was lost, so this is not a regression - and the percentage
		# is not comparable across the two LF values.
		denominator_moved.append({
			"path": path, "pct_b": pct_b, "pct_n": now[path], "lf_b": lf_b,
			"lf_n": total, "lh_b": hit_at_baseline, "lh_n": hit,
		})
		continue

	# The bytes changed, so the file's own edit is one cause of the growth - and
	# for a file whose LF is contributed by many TUs (a header), the other cause
	# is present at once and cannot be separated from the tracefile. For a file
	# that owns its own lines (anything but a header) the growth IS its own new
	# code, and the original strict rule stands.
	expected = round(pct_b * total / 100.0)
	lost = expected - hit
	if lost <= jitter_lines:
		if lost < 0:
			improvements.append((path, pct_b, now[path]))
		continue
	if path.endswith((".h", ".hpp", ".hxx", ".hh")):
		needs_anchor.append({
			"path": path, "pct_b": pct_b, "pct_n": now[path], "lf_b": lf_b,
			"lf_n": total, "lh_b": hit_at_baseline, "lh_n": hit,
			"lost": lost,
		})
	else:
		add_regression(path, pct_b, now[path], lf_b, total, expected, hit,
			f"new code entered under-covered (LF {lf_b} -> {total})")

# --- a file with no record: removed, or just not compiled this run? ----------
# The old rule was "no record -> removed -> drop the baseline entry". But a file
# can produce no record because this configuration did not compile it at all
# (WANT_STEM_SPLIT=OFF, an absent SDK, ...). Dropping its entry silently takes
# the file out of the ratchet, and a later run with the feature on would then
# refuse it through the entry floor. The two cases are separated by the only
# evidence that distinguishes them: does the path still exist?
removed = []
for path in sorted(set(baseline) - set(now)):
	if os.path.exists(os.path.join(repo_root, path)):
		unmeasured_by_config.append(path)
	else:
		removed.append(path)

for path in unmeasurable:
	print(f"unmeasurable  {path}: 0 instrumented lines - no coverage measurable "
		f"(recorded as n/a, never as 100%)")
for r in regressions:
	print(f"REGRESSION  {r['path']}: {r['pct_b']:.2f}% -> {r['pct_n']:.2f}%  "
		f"({r['lh_b'] - r['lh_n']} of {r['lf_n']} covered lines lost; {r['why']})")
for r in needs_anchor:
	print(f"REANCHOR-REQUIRED  {r['path']}: recorded {r['pct_b']:.2f}% was taken at an "
		f"unrecorded instrumented-line count; this run measures {r['pct_n']:.2f}% over "
		f"{r['lf_n']} lines")
	print(f"                   LF moved {r['lf_b']} -> {r['lf_n']} for a file whose bytes "
		f"ALSO changed, so the growth is its own edit and the instantiations of "
		f"whatever includes it, mixed inseparably")
	print(f"                   covered lines did not fall ({r['lh_b']} -> {r['lh_n']}), so "
		f"this is not a lost-coverage regression and the gate will not call it one: "
		f"reconcile it with a reason (--reanchor-file) or add a test")
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
for r in denominator_moved:
	print(f"denominator-moved  {r['path']}: instrumented lines {r['lf_b']} -> {r['lf_n']} with "
		f"the file BYTE-IDENTICAL (fingerprint unchanged)")
	print(f"                   coverage {r['pct_b']:.2f}% -> {r['pct_n']:.2f}% is therefore "
		f"not comparable: covered lines {r['lh_b']} -> {r['lh_n']} (none lost)")
	print(f"                   the added instrumented lines are compiler instantiations this "
		f"configuration sees and this run does not execute; judged on covered lines")
for path in unmeasured_by_config:
	print(f"unmeasured-by-config  {path}: exists and is in scope but this build compiled no "
		f"translation unit for it (baseline entry PRESERVED, not dropped)")
for path in removed:
	print(f"removed     {path}")
if legacy_compared:
	print(f"note: {len(legacy_compared)} baseline row(s) have no recorded instrumented-line "
		f"count and were compared with the pre-2026-09-12 arithmetic: "
		f"{', '.join(legacy_compared[:5])}{' ...' if len(legacy_compared) > 5 else ''}")

# --- scope accounting --------------------------------------------------------
# The headline is a claim about the files the instrumentation reached. That is
# NOT the same as the scope, and saying which is which is the difference between
# a measurement and a marketing number.
scope_entries = []
if os.path.exists(scope_path):
	with open(scope_path, encoding="utf-8") as fh:
		for line in fh:
			line = line.strip()
			if line and not line.startswith("#"):
				scope_entries.append(line)

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

if scope_entries:
	scope_seen = [p for p in scope_entries if p in now]
	scope_missing = [p for p in scope_entries if p not in now]
	print(f"scope: {len(scope_entries)} entries in tests/fork-sources.txt; "
		f"{len(scope_seen)} produced a record in this capture; "
		f"{len(scope_missing)} did not")
	print(f"       the line-coverage number above is a claim about those {len(scope_seen)} "
		f"files, NOT about the {len(scope_entries)}-entry scope")
	print("       (per-entry reason for the gap: python3 docs/coverage-green/classify-scope.py "
		f"{os.path.relpath(tracefile, repo_root)} <build-dir>)")

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

# --- single-file re-anchor ---------------------------------------------------
# `--reanchor-file` moves exactly ONE failing entry to what it measures now, with
# a reason that is printed. The rest of the baseline is carried over unchanged.
# It refuses a path that is not failing, so it cannot snatch a passing entry out
# of the ratchet, and it refuses a blank reason. The entry re-enters the ratchet
# at the measured value, with its LF and fingerprint recorded, so the next run
# compares like for like.
if reanchor:
	failing = [r["path"] for r in regressions] + [r["path"] for r in needs_anchor]
	refused = False
	for key, why in reanchor:
		if key not in failing:
			print(f"reanchor-file: '{key}' is not failing this run - nothing to re-anchor",
				file=sys.stderr)
			if key not in baseline and key not in now:
				print("(the path is in neither the baseline nor the tracefile)", file=sys.stderr)
			else:
				print("(a re-anchor moves a failing entry, it does not move a passing one)",
					file=sys.stderr)
			refused = True
			continue
		if key not in now or now[key] is None:
			print(f"reanchor-file: '{key}' has no measurable coverage in this run", file=sys.stderr)
			refused = True
			continue
		baseline[key] = {"pct": now[key], "lf": totals[key], "hash": fingerprint(key)}
		print(f"\nRE-ANCHORED (single file): {key} -> {now[key]:.2f}% "
			f"(LF {totals[key]}, LH {hits[key]})")
		print(f"reason: {why}")
	regressions = [r for r in regressions if r["path"] not in {k for k, _ in reanchor}]
	needs_anchor = [r for r in needs_anchor if r["path"] not in {k for k, _ in reanchor}]
	if refused:
		sys.exit(2)

if regressions or below_floor or needs_anchor:
	if regressions:
		print("\nFAIL: coverage regressed; add or fix tests before merging.", file=sys.stderr)
	if needs_anchor:
		print(f"\nFAIL: {len(needs_anchor)} entr(ies) need a recorded re-anchor decision "
			f"(the gate cannot form a like-for-like bar for them):", file=sys.stderr)
		for r in needs_anchor:
			print(f"  {r['path']}", file=sys.stderr)
		print("Record the decision with --reanchor-file <path> \"reason\", or add a test.",
			file=sys.stderr)
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


def fmt(path):
	"""New baseline value for `path`, or None if the entry must be DROPPED."""
	if path in now:
		if now[path] is None:
			return {"pct": None, "lf": totals[path], "hash": fingerprint(path)}
		return {"pct": now[path], "lf": totals[path], "hash": fingerprint(path)}
	# Not measured this run. A file that still exists was simply not compiled in
	# this configuration - its entry is preserved untouched. Only a path that is
	# actually gone drops out of the ratchet.
	if path in baseline and os.path.exists(os.path.join(repo_root, path)):
		return baseline[path]
	return None


new_baseline = {}
for path in sorted(set(baseline) | set(now)):
	new_baseline[path] = fmt(path)

if new_baseline != baseline or improvements or additions or removed or unmeasured_now:
	for path in legacy_rows:
		e = new_baseline.get(path)
		if e is None or e.get("lf") is None:
			continue
		old = baseline[path]["pct"]
		old_s = "n/a" if old is None else f"{old:.2f}%"
		new_s = "n/a" if e["pct"] is None else f"{e['pct']:.2f}%"
		print(f"migrated legacy row: {path}: {old_s} (no recorded instrumented-line count) "
			f"-> {new_s} (LF {e['lf']})")
	fd, tmp = tempfile.mkstemp(prefix=".coverage-baseline.", dir=os.path.dirname(baseline_path))
	with os.fdopen(fd, "w", encoding="utf-8") as fh:
		fh.write("# per-file fork-code line coverage baseline\n")
		fh.write("# maintained by tests/coverage-gate.sh; do not edit by hand\n")
		fh.write("# <path><TAB><pct|n/a><TAB><instrumented lines|-><TAB><sha256-16 of the file|->\n")
		fh.write("# n/a = present in the tracefile with 0 instrumented lines (unmeasurable);\n")
		fh.write(f"# new files enter at >= {entry_floor:.2f}% (COVERAGE_ENTRY_FLOOR) unless declared in\n")
		fh.write(f"# {os.path.basename(exempt_path)} with a reason.\n")
		fh.write("# LF and the fingerprint exist so a percentage that moved because the COMPILED\n")
		fh.write("# TRANSLATION-UNIT SET changed is not read as a coverage regression; see the\n")
		fh.write("# gate's header. A file that still exists but produced no record keeps its entry.\n")
		for path, e in new_baseline.items():
			if e is None:
				continue
			pct_s = "n/a" if e["pct"] is None else format(e["pct"], ".2f")
			lf_s = "-" if e["lf"] is None else str(e["lf"])
			fp_s = e["hash"] or "-"
			fh.write(f"{path}\t{pct_s}\t{lf_s}\t{fp_s}\n")
	os.replace(tmp, baseline_path)
	print(f"baseline updated: {baseline_path}")
else:
	print("PASS (no change)")

sys.exit(0)
PYEOF
