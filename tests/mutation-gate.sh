#!/usr/bin/env bash
#
# mutation-gate.sh — Gate 5 (mutation testing) for the LMMS standards fork.
#
# WHY THIS EXISTS
#   Gate 5 was the only gate that was not a script. The adopted ruleset asks for
#   mutation testing with a >=80% kill score on core modules, and no packaged
#   C++ mutation tool exists in this environment (verified: apt has no mull /
#   mutation package; LLVM 18 is installed but mull is not packaged; mutmut is
#   absent). This script is a SMALL, HONEST, SELF-CONTAINED harness scoped to
#   ONE fork-new translation unit that is already at 100% line coverage:
#
#       src/core/RoutingGraph.cpp   (194 lines, 100% line coverage)
#       tests/src/core/RoutingGraphTest.cpp -> build/tests/RoutingGraphTest
#
#   It is deliberately not a whole-project mutation runner. A scoped gate that
#   is correct beats a broad one that is flaky.
#
# WHAT IT DOES (per mutant, all verified by execution — no claims without proof)
#   1. generate real source mutations (operator flips, constant changes,
#      condition negations, single-statement deletions) using an embedded
#      Python generator that masks comments and string/char literals first, so
#      a mutation can never land in a comment, a string, or whitespace;
#   2. apply the mutation to the pristine file and prove it landed by comparing
#      the written file's sha256 against the in-memory expected content, and
#      by `git diff --quiet` (must report a change, and only to that file);
#   3. rebuild ONLY that TU + relink the test binary, and prove the rebuild
#      actually happened (build log names the object, and both the object file
#      and the test binary mtimes strictly increase);
#   4. run the test binary: exit 0 = SURVIVED, non-zero/crash = KILLED,
#      timeout = KILLED (reported as such);
#   5. restore the pristine source and prove the tree is unmutated again
#      (sha256 + `git diff --quiet`) before the next mutant.
#
#   A mutant that does not compile/link is INVALID, never "killed", and is
#   counted separately from the kill score.
#
# SCORING
#   kill score = killed / (killed + survived); invalid mutants are excluded and
#   listed separately. Exit 0 if the score is >= the threshold (default 80%),
#   else 1. Exit 2 is reserved for harness/setup errors (dirty tree, broken
#   control run, failed restore, stale rebuild) — those mean the harness itself
#   is untrustworthy and its output must not be used as a score.
#
# SELECTION
#   Candidates are generated deterministically from the source. The gate runs a
#   deterministic stratified sample: candidates are grouped by mutation
#   operator, each group is ordered by sha256("<seed>:<index>"), and the groups
#   are interleaved round-robin so every operator class is represented. Same
#   seed + same source => same mutants, on any machine, with no RNG. --all runs
#   every candidate; --max-mutants N / --seed S control the sample.
#
# KNOWN LIMITATIONS (stated, not hidden — also in tests/QA-GATES.md Gate 5)
#   - Scope is ONE translation unit (RoutingGraph.cpp). The score says nothing
#     about the rest of the fork.
#   - The embedded generator implements a fixed operator set; it is not a
#     general-purpose mutation engine and has no equivalent-mutant detection.
#     Survivors are reported, not silently discarded; the doc names which are
#     semantically equivalent.
#   - `assert(...)` lines are excluded from mutation: QtTest has no death-test
#     facility, so a mutation that only changes an assert's firing condition
#     cannot be killed by this harness and would dilute the score.
#   - Mutants that provoke undefined behaviour (out-of-bounds ids/ports) are
#     included and reported as whatever really happens; a UB mutant that passes
#     is a false survivor, which is a limitation of mutation testing in C++,
#     not a licence to hand-edit the score.
#   - The control (unmutated source) is run 3x before and 1x after the sweep;
#     a flaky control aborts the run with exit 2 instead of producing a score.
#
# Usage:
#   bash tests/mutation-gate.sh                    # 30-mutant sample, seed 0
#   bash tests/mutation-gate.sh --max-mutants 10   # quicker sample
#   bash tests/mutation-gate.sh --all              # every candidate (slow)
#   bash tests/mutation-gate.sh --seed 7           # different sample
#   bash tests/mutation-gate.sh --threshold 90     # stricter pass mark
#   bash tests/mutation-gate.sh --list             # print candidates, mutate nothing
#   bash tests/mutation-gate.sh --self-test        # prove INVALID/KILLED/SURVIVED classification
#   bash tests/mutation-gate.sh --self-test-only   # only the classification proof (~30s)
#
# Exit codes: 0 = pass, 1 = score below threshold, 2 = harness/setup error.

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
cd "$ROOT" || exit 2

SRC_REL="src/core/RoutingGraph.cpp"
TEST_NAME="RoutingGraphTest"
TEST_BIN="build/tests/${TEST_NAME}"
OBJ_REL="build/src/CMakeFiles/lmmsobjs.dir/core/RoutingGraph.cpp.o"
WORK_DIR="build/mutation-gate"
THRESHOLD=80
MAX_MUTANTS=30
SEED=0
RUN_ALL=0
LIST_ONLY=0
SELF_TEST=0
SELF_TEST_ONLY=0
TEST_TIMEOUT=10
JOBS=4

usage() { awk 'NR > 1 && /^#/ { sub(/^# ?/, ""); print; next } NR > 1 { exit }' "${BASH_SOURCE[0]}"; }

while [ $# -gt 0 ]; do
	case "$1" in
		--max-mutants) MAX_MUTANTS="${2:-}"; shift 2 ;;
		--seed)        SEED="${2:-}"; shift 2 ;;
		--threshold)   THRESHOLD="${2:-}"; shift 2 ;;
		--timeout)     TEST_TIMEOUT="${2:-}"; shift 2 ;;
		--jobs)        JOBS="${2:-}"; shift 2 ;;
		--all)         RUN_ALL=1; shift ;;
		--list)        LIST_ONLY=1; shift ;;
		--self-test)   SELF_TEST=1; shift ;;
		--self-test-only) SELF_TEST=1; SELF_TEST_ONLY=1; shift ;;
		-h|--help)     usage; exit 0 ;;
		*) echo "mutation-gate: unknown option: $1 (try --help)" >&2; exit 2 ;;
	esac
done

if ! [[ "$MAX_MUTANTS" =~ ^[0-9]+$ ]] || [ "$MAX_MUTANTS" -lt 1 ]; then
	echo "mutation-gate: --max-mutants must be a positive integer" >&2; exit 2
fi
if ! [[ "$SEED" =~ ^[0-9]+$ ]]; then
	echo "mutation-gate: --seed must be a non-negative integer" >&2; exit 2
fi
if ! [[ "$THRESHOLD" =~ ^[0-9]+$ ]] || [ "$THRESHOLD" -gt 100 ]; then
	echo "mutation-gate: --threshold must be an integer 0..100" >&2; exit 2
fi

command -v python3 >/dev/null || { echo "mutation-gate: python3 is required" >&2; exit 2; }
command -v git >/dev/null || { echo "mutation-gate: git is required" >&2; exit 2; }

[ -f "$SRC_REL" ] || { echo "mutation-gate: $SRC_REL not found (run from the fork root)" >&2; exit 2; }
[ -d build ] || { echo "mutation-gate: no build/ — configure first: cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug -DWANT_QT6=ON" >&2; exit 2; }
[ -f "$TEST_BIN" ] || { echo "mutation-gate: $TEST_BIN not found — build it first: cmake --build build --target $TEST_NAME -j$JOBS" >&2; exit 2; }
[ -f "$OBJ_REL" ] || { echo "mutation-gate: object file $OBJ_REL not found — unexpected build layout" >&2; exit 2; }

# The harness mutates a tracked file, so it must own a clean copy of it. Refuse
# to run on a dirty target: a pre-existing edit would be destroyed by restore.
if ! git diff --quiet -- "$SRC_REL" || ! git diff --cached --quiet -- "$SRC_REL"; then
	echo "mutation-gate: $SRC_REL has uncommitted changes — commit or stash first" >&2
	exit 2
fi

mkdir -p "$WORK_DIR/logs"
# One mutation run at a time: it edits a source file and relinks a binary.
if command -v flock >/dev/null; then
	exec 9>"$WORK_DIR/.lock"
	if ! flock -n 9; then
		echo "mutation-gate: another mutation run holds $WORK_DIR/.lock — refusing to run concurrently" >&2
		exit 2
	fi
fi

TMP="$(mktemp -d)"
PRISTINE="$TMP/pristine.cpp"
CANDIDATES="$TMP/candidates.tsv"
PLAN="$TMP/plan.tsv"
cleanup() { rm -rf "$TMP"; }
trap cleanup EXIT

cp -- "$SRC_REL" "$PRISTINE"

py_sha() {
	python3 - "$1" <<'PYEOF'
import hashlib
import sys
print(hashlib.sha256(open(sys.argv[1], "rb").read()).hexdigest())
PYEOF
}

py_apply() {
	python3 - "$1" "$2" "$3" "$4" <<'PYEOF'
import hashlib
import sys

src_path, pristine_path, cand_path, idx = (
	sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4])

pristine = open(pristine_path, encoding="utf-8").read()
text = open(src_path, encoding="utf-8").read()

def sha(s):
	return hashlib.sha256(s.encode("utf-8")).hexdigest()

if sha(text) != sha(pristine):
	print("apply: current source is not pristine — refusing to mutate", file=sys.stderr)
	sys.exit(3)

row = None
with open(cand_path, encoding="utf-8") as fh:
	for line in fh:
		fields = line.rstrip("\n").split("\t")
		if fields and fields[0] == idx:
			row = fields
			break
if row is None:
	print(f"apply: candidate {idx} not found", file=sys.stderr)
	sys.exit(3)

_, line_s, rule, orig, new, kind, pos = row
if kind == "token":
	pos = int(pos)
	if text[pos:pos + len(orig)] != orig:
		print(f"apply: candidate {idx} does not match the source at offset {pos}", file=sys.stderr)
		sys.exit(3)
	mutated = text[:pos] + new + text[pos + len(orig):]
else:
	lines = text.split("\n")
	i = int(line_s) - 1
	if i < 0 or i >= len(lines) or lines[i].strip() != orig:
		print(f"apply: line {line_s} does not match the candidate", file=sys.stderr)
		sys.exit(3)
	lines[i] = ""          # delete the whole statement; line count is preserved
	mutated = "\n".join(lines)

if mutated == text:
	print("apply: mutation produced no change", file=sys.stderr)
	sys.exit(3)

with open(src_path, "w", encoding="utf-8") as fh:
	fh.write(mutated)
if sha(open(src_path, encoding="utf-8").read()) != sha(mutated):
	print("apply: written file does not match the intended mutation", file=sys.stderr)
	sys.exit(3)
print(f"applied {rule} at {src_path}:{line_s}: {orig} -> {new}")
PYEOF
}

PRISTINE_SHA="$(py_sha "$PRISTINE")"

# ---------------------------------------------------------------- generator --
# Emits TSV: idx<TAB>line<TAB>rule<TAB>orig<TAB>new<TAB>kind<TAB>pos
python3 - "$SRC_REL" > "$CANDIDATES" <<'PYEOF'
import re
import sys


def mask_code(text):
	"""Copy of text with comments and string/char literals blanked out (same
	length, newlines kept) so regexes can only match real code."""
	out = list(text)
	i, n, state = 0, len(text), "code"
	while i < n:
		ch = text[i]
		nxt = text[i + 1] if i + 1 < n else ""
		if state == "code":
			if ch == "/" and nxt == "/":
				out[i] = out[i + 1] = " "
				state, i = "line", i + 2
			elif ch == "/" and nxt == "*":
				out[i] = out[i + 1] = " "
				state, i = "block", i + 2
			elif ch == '"':
				out[i] = " "
				state, i = "string", i + 1
			elif ch == "'":
				out[i] = " "
				state, i = "char", i + 1
			else:
				i += 1
		elif state == "line":
			if ch == "\n":
				state = "code"
			else:
				out[i] = " "
			i += 1
		elif state == "block":
			if ch == "*" and nxt == "/":
				out[i] = out[i + 1] = " "
				state, i = "code", i + 2
			else:
				if ch != "\n":
					out[i] = " "
				i += 1
		else:  # string / char
			q = '"' if state == "string" else "'"
			if ch == "\\":
				out[i] = " "
				if i + 1 < n:
					out[i + 1] = " "
				i += 2
			elif ch == q:
				out[i] = " "
				state, i = "code", i + 1
			else:
				if ch != "\n":
					out[i] = " "
				i += 1
	return "".join(out)


def line_of(text, pos):
	return text.count("\n", 0, pos) + 1


# (rule, regex over masked code, replacement)
TOKEN_RULES = [
	("cmp-eq", re.compile(r"=="), lambda m: "!="),
	("cmp-ne", re.compile(r"!="), lambda m: "=="),
	("cmp-le", re.compile(r"(?<!<)<="), lambda m: ">="),
	("cmp-ge", re.compile(r"(?<!>)>="), lambda m: "<="),
	("cmp-lt", re.compile(r"(?<=\s)<(?=\s)"), lambda m: ">"),
	("cmp-gt", re.compile(r"(?<=\s)>(?=\s)"), lambda m: "<"),
	("logic-and", re.compile(r"&&"), lambda m: "||"),
	("logic-or", re.compile(r"\|\|"), lambda m: "&&"),
	("inc-dec", re.compile(r"\+\+"), lambda m: "--"),
	("dec-inc", re.compile(r"--"), lambda m: "++"),
	("const-zero", re.compile(r"(?<![\w.-])0(?![\w.])"), lambda m: "1"),
	("const-one", re.compile(r"(?<![\w.-])1(?![\w.])"), lambda m: "0"),
	("const-neg-one", re.compile(r"(?<![\w.])-1(?![\w.])"), lambda m: "0"),
	("ret-true", re.compile(r"\breturn true\b"), lambda m: "return false"),
	("ret-false", re.compile(r"\breturn false\b"), lambda m: "return true"),
	("min-max", re.compile(r"\bstd::min\b"), lambda m: "std::max"),
	("max-min", re.compile(r"\bstd::max\b"), lambda m: "std::min"),
	("greater-less", re.compile(r"\bstd::greater<int>"), lambda m: "std::less<int>"),
	("neg-drop", re.compile(r"if \(!"), lambda m: "if ("),
]

DECL_START = re.compile(
	r"^(int|unsigned|const|auto|std::|QString|QDom|QHash|float|double|bool|char|void|"
	r"RoutingGraph|RoutingConnection|RoutingNode|ch_cnt_t|f_cnt_t|if|for|while|return|"
	r"else|assert|#)\b")


def gen_candidates(text, masked):
	cands = []
	for rule, rx, repl in TOKEN_RULES:
		for m in rx.finditer(masked):
			orig, new = m.group(0), repl(m)
			if new == orig:
				continue
			cands.append({"rule": rule, "pos": m.start(), "line": line_of(text, m.start()),
				"orig": orig, "new": new, "kind": "token"})

	# whole-line single-statement deletion. Only lines that begin and end at
	# paren/bracket depth 0 are considered, so a continuation line of a
	# multi-line statement (e.g. the remove_if lambda in removeNode) is never
	# cut in half. Braces are not counted: statements live inside function
	# bodies, which would otherwise put every candidate at depth >= 1.
	masked_lines = masked.split("\n")
	orig_lines = text.split("\n")
	depth = 0
	for lineno, (ml, ol) in enumerate(zip(masked_lines, orig_lines), 1):
		start_depth = depth
		depth += ml.count("(") - ml.count(")") + ml.count("[") - ml.count("]")
		s = ml.strip()
		if start_depth != 0 or not s or not s.endswith(";"):
			continue
		if DECL_START.match(s) or s.startswith("}"):
			continue
		if not re.match(r"^(\+\+|--)?[\w\.\[\]\(\)\->:]+(\s*=[^=].*|\s*\(.*\))?;$", s):
			continue
		cands.append({"rule": "stmt-delete", "pos": -1, "line": lineno,
			"orig": ol.strip(), "new": "<deleted>", "kind": "line"})
	return cands


text = open(sys.argv[1], encoding="utf-8").read()
masked = mask_code(text)
cands = gen_candidates(text, masked)
# assert() lines are excluded: QtTest has no death test (see the script header).
assert_lines = {i for i, l in enumerate(text.split("\n"), 1) if l.strip().startswith("assert(")}
cands = [c for c in cands if c["line"] not in assert_lines]
for i, c in enumerate(cands):
	print("\t".join([str(i), str(c["line"]), c["rule"], c["orig"], c["new"],
		c["kind"], str(c["pos"])]))
PYEOF

TOTAL="$(wc -l < "$CANDIDATES" | tr -d ' ')"

if [ "$LIST_ONLY" -eq 1 ]; then
	echo "mutation-gate: $TOTAL mutation candidates in $SRC_REL"
	printf '  %-5s %-6s %-14s %s\n' "idx" "line" "rule" "mutation"
	while IFS=$'\t' read -r idx line rule orig new kind pos; do
		if [ "$kind" = "line" ]; then
			printf '  %-5s %-6s %-14s delete: %s\n' "$idx" "$line" "$rule" "$orig"
		else
			printf '  %-5s %-6s %-14s %s -> %s\n' "$idx" "$line" "$rule" "$orig" "$new"
		fi
	done < "$CANDIDATES"
	exit 0
fi

# ------------------------------------------------------- deterministic sample --
python3 - "$CANDIDATES" "$SEED" "$MAX_MUTANTS" "$RUN_ALL" > "$PLAN" <<'PYEOF'
import hashlib
import sys

path, seed, max_mutants, run_all = (
	sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4] == "1")
rows = []
with open(path, encoding="utf-8") as fh:
	for line in fh:
		line = line.rstrip("\n")
		if line:
			rows.append(line)

if run_all or max_mutants >= len(rows):
	for row in rows:
		print(row)
	sys.exit(0)

groups = {}
for i, row in enumerate(rows):
	rule = row.split("\t")[2]
	groups.setdefault(rule, []).append(i)
# deterministic order inside a group: sha256("<seed>:<index>") — no RNG, so the
# same seed picks the same mutants on any machine and any Python version.
for rule in groups:
	groups[rule].sort(key=lambda i: hashlib.sha256(f"{seed}:{i}".encode()).hexdigest())
order = []
while True:
	added = False
	for rule in sorted(groups):
		if groups[rule]:
			order.append(groups[rule].pop(0))
			added = True
	if not added:
		break
for i in order[:max_mutants]:
	print(rows[i])
PYEOF

SELECTED="$(wc -l < "$PLAN" | tr -d ' ')"
if [ "$SELF_TEST_ONLY" -eq 1 ]; then
	: > "$PLAN"     # classification self-test only: do not sweep the source
	SELECTED=0
fi
cp -- "$PLAN" "$WORK_DIR/plan.tsv"   # persist the exact selection for reproducibility

# ------------------------------------------------------------------- control --
banner() { printf '\n==== %s ====\n' "$1"; }

banner "control: unmutated source must build and pass"
echo "pristine sha256: $PRISTINE_SHA"
# Force the TU to be recompiled from the pristine source so the control cannot
# pass on a stale object/binary left over from an earlier session.
rm -f "$OBJ_REL"
if ! cmake --build build --target "$TEST_NAME" -j"$JOBS" > "$WORK_DIR/logs/control-build.log" 2>&1; then
	echo "mutation-gate: control build FAILED — harness untrustworthy, no score produced" >&2
	tail -20 "$WORK_DIR/logs/control-build.log" >&2
	exit 2
fi
if ! grep -q "Building CXX object .*RoutingGraph.cpp.o" "$WORK_DIR/logs/control-build.log"; then
	echo "mutation-gate: control build did not recompile $SRC_REL — harness untrustworthy" >&2
	exit 2
fi
for run in 1 2 3; do
	if ! QT_QPA_PLATFORM=offscreen timeout "$TEST_TIMEOUT" "$TEST_BIN" > "$WORK_DIR/logs/control-$run.log" 2>&1; then
		echo "mutation-gate: control test run $run FAILED (flaky control) — harness untrustworthy, no score produced" >&2
		tail -20 "$WORK_DIR/logs/control-$run.log" >&2
		exit 2
	fi
done
echo "control: pristine TU recompiled, test binary passed 3/3 runs"

# --------------------------------------------------------------- mutant loop --
if [ "$SELF_TEST_ONLY" -eq 1 ]; then
	banner "self-test-only: skipping the mutant sweep"
else
	banner "mutants: $SELECTED selected of $TOTAL candidates (seed $SEED)"
fi
declare -a ROWS
killed=0 survived=0 invalid=0
run_index=0

run_mutant() {
	local idx="$1" line="$2" rule="$3" orig="$4" new="$5" kind="$6" pos="$7"
	local label result build_state obj_before obj_after bin_before bin_after dirty rc now_sha
	run_index=$((run_index + 1))

	if [ "$kind" = "line" ]; then
		label="delete: $orig"
	else
		label="$orig -> $new"
	fi

	obj_before="$(stat -c %Y "$OBJ_REL")"
	bin_before="$(stat -c %Y "$TEST_BIN")"

	# 1. apply + prove it landed (sha256 of the written file vs intended content)
	if ! py_apply "$SRC_REL" "$PRISTINE" "$CANDIDATES" "$idx" > "$WORK_DIR/logs/apply-$idx.log" 2>&1; then
		echo "  mutant $idx: APPLY FAILED — harness error" >&2
		cat "$WORK_DIR/logs/apply-$idx.log" >&2
		cp -- "$PRISTINE" "$SRC_REL"
		exit 2
	fi
	if git diff --quiet -- "$SRC_REL"; then
		echo "  mutant $idx: git reports no change after apply — harness error" >&2
		cp -- "$PRISTINE" "$SRC_REL"
		exit 2
	fi
	dirty="$(git status --porcelain -- "$SRC_REL")"
	if [ "$dirty" != " M $SRC_REL" ]; then
		echo "  mutant $idx: unexpected git status '$dirty' — harness error" >&2
		cp -- "$PRISTINE" "$SRC_REL"
		exit 2
	fi

	# 2. rebuild + prove the mutated TU really was recompiled and relinked
	if cmake --build build --target "$TEST_NAME" -j"$JOBS" > "$WORK_DIR/logs/build-$idx.log" 2>&1; then
		obj_after="$(stat -c %Y "$OBJ_REL")"
		bin_after="$(stat -c %Y "$TEST_BIN")"
		if ! grep -q "Building CXX object .*RoutingGraph.cpp.o" "$WORK_DIR/logs/build-$idx.log" \
			|| [ "$obj_after" -le "$obj_before" ] || [ "$bin_after" -le "$bin_before" ]; then
			echo "  mutant $idx: build reported success but the mutated TU was not rebuilt/relinked" >&2
			echo "  (that would silently test a stale binary — aborting rather than reporting a false result)" >&2
			cp -- "$PRISTINE" "$SRC_REL"
			exit 2
		fi
		build_state="ok"
	else
		build_state="no-build"
	fi

	# 3. classify by the real test binary's exit code
	if [ "$build_state" = "no-build" ]; then
		result="INVALID"
		invalid=$((invalid + 1))
	else
		# Run through a nested bash so bash's own "Segmentation fault" job
		# notice for a crashing mutant lands in the log file, not the table.
		QT_QPA_PLATFORM=offscreen bash -c 'timeout "$1" "$2"; exit $?' _ "$TEST_TIMEOUT" "$TEST_BIN" > "$WORK_DIR/logs/test-$idx.log" 2>&1
		rc=$?
		if [ "$rc" -eq 124 ] || [ "$rc" -eq 137 ]; then
			result="KILLED(timeout)"
			killed=$((killed + 1))
		elif [ "$rc" -eq 0 ]; then
			result="SURVIVED"
			survived=$((survived + 1))
		else
			result="KILLED"
			killed=$((killed + 1))
		fi
	fi

	# 4. restore + prove the tree is unmutated again
	cp -- "$PRISTINE" "$SRC_REL"
	now_sha="$(py_sha "$SRC_REL")"
	if [ "$now_sha" != "$PRISTINE_SHA" ] || ! git diff --quiet -- "$SRC_REL"; then
		echo "  mutant $idx: RESTORE FAILED — tree left mutated, aborting" >&2
		exit 2
	fi

	printf '  %-3s %-30s %-14s %-42s %-8s %s\n' \
		"$run_index" "$SRC_REL:$line" "$rule" "$label" "$build_state" "$result"
	ROWS+=("$run_index"$'	'"$SRC_REL:$line"$'	'"$rule"$'	'"$label"$'	'"$build_state"$'	'"$result")
}

printf '  %-3s %-30s %-14s %-42s %-8s %s\n' "#" "site" "rule" "mutation" "build" "result"
printf '  %s\n' "--------------------------------------------------------------------------------------------------------------------"
while IFS=$'\t' read -r idx line rule orig new kind pos; do
	[ -n "${idx:-}" ] || continue
	run_mutant "$idx" "$line" "$rule" "$orig" "$new" "$kind" "$pos"
done < "$PLAN"

# -------------------------------------------------------------- final control --
banner "final control: tree restored, pristine source still builds and passes"
if [ "$(py_sha "$SRC_REL")" != "$PRISTINE_SHA" ] || ! git diff --quiet -- "$SRC_REL"; then
	echo "mutation-gate: source is not pristine after the sweep — aborting" >&2
	exit 2
fi
rm -f "$OBJ_REL"   # force a recompile from the restored source, not the last mutant
if ! cmake --build build --target "$TEST_NAME" -j"$JOBS" > "$WORK_DIR/logs/final-build.log" 2>&1; then
	echo "mutation-gate: final control build FAILED — harness untrustworthy" >&2
	exit 2
fi
if ! QT_QPA_PLATFORM=offscreen timeout "$TEST_TIMEOUT" "$TEST_BIN" > "$WORK_DIR/logs/final-control.log" 2>&1; then
	echo "mutation-gate: final control test FAILED — harness untrustworthy" >&2
	tail -20 "$WORK_DIR/logs/final-control.log" >&2
	exit 2
fi
echo "final control: pristine source recompiled, test binary passed"
if ! git diff --quiet -- "$SRC_REL"; then
	# the sha256 check above already proved the content is the pre-run content,
	# so a remaining diff against HEAD means the TU was already dirty at start
	echo "note: $SRC_REL has pre-existing uncommitted edits (content verified restored)"
fi
dirty="$(git status --porcelain | grep -v -- "$SRC_REL\$" | head -5 || true)"
if [ -n "$dirty" ]; then
	echo "note: uncommitted paths unrelated to the sweep (not caused by this run):"
	printf '%s\n' "$dirty"
fi

# ---------------------------------------------------------------- self-test ---
if [ "$SELF_TEST" -eq 1 ]; then
	banner "self-test: classification (INVALID / KILLED / SURVIVED)"
	st=0
	# 1. a source that cannot compile must be INVALID, not killed
	python3 - "$SRC_REL" <<'PYEOF'
import sys
p = sys.argv[1]
t = open(p, encoding="utf-8").read()
open(p, "w", encoding="utf-8").write(
	t.replace("auto RoutingGraph::addNode", "this is not C++\nauto RoutingGraph::addNode", 1))
PYEOF
	if cmake --build build --target "$TEST_NAME" -j"$JOBS" > "$WORK_DIR/logs/selftest-invalid.log" 2>&1; then
		echo "  self-test INVALID:  FAIL (a broken source compiled — harness blind)"; st=1
	else
		echo "  self-test INVALID:  PASS (broken source did not compile)"
	fi
	cp -- "$PRISTINE" "$SRC_REL"
	# 2. a known-lethal mutation must be KILLED. Chosen by source text, not by a
	#    hardcoded line number: flipping the channel-loop condition makes the gain
	#    node copy no audio, which ProcessesKnownBlock must detect.
	lethal_line="$(grep -n 'for (ch_cnt_t c = 0; c < channels; ++c)' "$SRC_REL" | cut -d: -f1)"
	lethal_idx="$(awk -F'	' -v L="$lethal_line" '$2==L && $3=="cmp-lt" {print $1; exit}' "$CANDIDATES")"
	if [ -z "$lethal_idx" ]; then
		echo "  self-test KILLED:   FAIL (known-lethal candidate not found in $SRC_REL)"; st=1
	else
		py_apply "$SRC_REL" "$PRISTINE" "$CANDIDATES" "$lethal_idx" > /dev/null 2>&1
		cmake --build build --target "$TEST_NAME" -j"$JOBS" > "$WORK_DIR/logs/selftest-killed.log" 2>&1
		if QT_QPA_PLATFORM=offscreen timeout "$TEST_TIMEOUT" "$TEST_BIN" > /dev/null 2>&1; then
			echo "  self-test KILLED:   FAIL (known-lethal mutant survived — harness blind)"; st=1
		else
			echo "  self-test KILLED:   PASS (known-lethal mutant was killed)"
		fi
	fi
	cp -- "$PRISTINE" "$SRC_REL"
	# 3. a known-equivalent mutation must SURVIVE. `plan.reserve(count);` only
	#    reserves capacity; deleting it cannot change observable behaviour, so a
	#    harness that "kills" it is lying. This proves the harness reports
	#    survivors instead of always claiming a kill.
	equiv_line="$(grep -n 'plan\.reserve(count);' "$SRC_REL" | cut -d: -f1)"
	equiv_idx="$(awk -F'	' -v L="$equiv_line" '$2==L && $3=="stmt-delete" {print $1; exit}' "$CANDIDATES")"
	if [ -z "$equiv_idx" ]; then
		echo "  self-test SURVIVED: FAIL (known-equivalent candidate not found in $SRC_REL)"; st=1
	else
		py_apply "$SRC_REL" "$PRISTINE" "$CANDIDATES" "$equiv_idx" > /dev/null 2>&1
		cmake --build build --target "$TEST_NAME" -j"$JOBS" > "$WORK_DIR/logs/selftest-survived.log" 2>&1
		if QT_QPA_PLATFORM=offscreen timeout "$TEST_TIMEOUT" "$TEST_BIN" > /dev/null 2>&1; then
			echo "  self-test SURVIVED: PASS (equivalent mutant survived, as expected)"
		else
			echo "  self-test SURVIVED: FAIL (equivalent mutant was killed — unexpected)"; st=1
		fi
	fi
	cp -- "$PRISTINE" "$SRC_REL"
	# leave the tree pristine + green before reporting
	rm -f "$OBJ_REL"
	cmake --build build --target "$TEST_NAME" -j"$JOBS" > /dev/null 2>&1
	if [ "$st" -ne 0 ]; then
		echo "mutation-gate: SELF-TEST FAILED — the harness cannot be trusted" >&2
		exit 2
	fi
	echo "self-test: PASS (INVALID, KILLED and SURVIVED are all classified correctly)"
fi
if [ "$SELF_TEST_ONLY" -eq 1 ]; then
	if [ "$st" -eq 0 ]; then
		echo "mutation-gate: self-test-only run complete"
		exit 0
	fi
	exit 2
fi

# -------------------------------------------------------------------- report ---
valid=$((killed + survived))
if [ "$valid" -gt 0 ]; then
	score="$(python3 - "$killed" "$valid" <<'PYEOF'
import sys
k, v = int(sys.argv[1]), int(sys.argv[2])
print(f"{100.0 * k / v:.1f}")
PYEOF
)"
else
	score="0.0"
fi

banner "result"
{
	echo "mutation-gate: $SRC_REL"
	echo "candidates generated : $TOTAL"
	echo "mutants run          : $SELECTED (seed $SEED; deterministic stratified sample)"
	echo "valid mutants        : $valid"
	echo "  killed             : $killed"
	echo "  survived           : $survived"
	echo "invalid (no build)   : $invalid"
	echo "kill score           : ${killed}/${valid} = ${score}%  (threshold ${THRESHOLD}%)"
	echo
	echo "mutant table (build ok => the mutated TU really was recompiled and relinked):"
	echo "  #   site                           rule           mutation                                   build    result"
	for row in "${ROWS[@]}"; do
		IFS=$'	' read -r ri site rule label build_state result <<< "$row"
		printf '  %-3s %-30s %-14s %-42s %-8s %s\n' "$ri" "$site" "$rule" "$label" "$build_state" "$result"
	done
	echo
	echo "invalid mutants are excluded from the score; survivors are listed above and"
	echo "named in tests/QA-GATES.md Gate 5. Logs: $WORK_DIR/logs/"
} | tee "$WORK_DIR/summary.txt"

# machine-readable results
{
	printf 'idx\tsite\trule\tmutation\tbuild\tresult\n'
	for row in "${ROWS[@]}"; do
		IFS=$'	' read -r ri site rule label build_state result <<< "$row"
		printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$ri" "$site" "$rule" "$label" "$build_state" "$result"
	done
} > "$WORK_DIR/results.tsv"

if [ "$valid" -eq 0 ]; then
	echo "mutation-gate: no valid mutants — harness produced no score (exit 2)" >&2
	exit 2
fi

if python3 - "$score" "$THRESHOLD" <<'PYEOF'
import sys
sys.exit(0 if float(sys.argv[1]) >= float(sys.argv[2]) else 1)
PYEOF
then
	echo "PASS: kill score ${score}% >= ${THRESHOLD}%"
	exit 0
else
	echo "FAIL: kill score ${score}% < ${THRESHOLD}%" >&2
	exit 1
fi
