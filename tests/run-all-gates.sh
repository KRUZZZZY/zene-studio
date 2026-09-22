#!/usr/bin/env bash
# run-all-gates.sh — run every executable QA gate for the LMMS standards fork.
#
# Usage:
#   bash tests/run-all-gates.sh                 # gates 1, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 (Gate 5 ≈3 min)
#   bash tests/run-all-gates.sh --with-coverage # + Gate 2 (full coverage build; slow)
#   bash tests/run-all-gates.sh --no-mutation   # skip Gate 5 (mutation sweep)
#   bash tests/run-all-gates.sh --strict        # pass --strict to gates that support it
#   bash tests/run-all-gates.sh --whole-tree    # Gates 4, 7, 8 over the whole-tree scope
#                                               # (tests/all-sources.txt) instead of the
#                                               # default fork scope (tests/fork-sources.txt).
#                                               # Upstream code is grandfathered in
#                                               # tests/*-baseline-all.tsv.
#
# SCOPE POLICY (2026-09-12, corrected 2026-09-13; the whole-tree scope is GREEN again as of the
# 0.3.0 W2-process lane). The default run is the ENFORCED scope: the fork scope plus the tools
# scope, which is also what CI's static-gates job runs. The whole-tree scope is NOT part of a
# default run, and saying so is the point of the summary's scope line: between the post-alpha
# merges and 2026-09-12 the all-scope ratchets were red while every default run and every CI job
# reported green, because nothing that runs by default measured them.
#
# 2026-09-13 (W2-process): the all scope went red a third time at the 0.2.1-alpha tip
# (`post-alpha/integration` @ 5565b4b1b, carried into 70f2d087c) — 28 complexity and 34
# file-length regression lines — while three documents still said it was green. The decision taken
# was the "smallest honest action" the handoff named, executed as a recorded act rather than a
# whole-scope `--reanchor`: **49 per-path `--reanchor-file <path> "<reason>"` calls** over 35
# distinct files (15 complexity paths, 34 file-length paths; one file can need both), each reason
# naming the path, its measured growth and its class — 36 of the 49 sit in 24 upstream-inherited
# files declared in tests/upstream-modifications.txt, 4 in 3 fork-authored product sources, 9 in 8
# fork-authored test sources that the all scope measures because tests/fork-sources.txt
# deliberately excludes test-side sources. The reasons and the path lists are recorded in
# tests/QA-GATES.md ("Scope policy", "The 2026-09-13 re-anchor, per path"). A whole-tree run now
# exits 0 on all three of its gates. Ratchets still move ONLY by a recorded act (never by hand,
# never by trimming code): `--reanchor-file <path> "<reason>"` per file, or a whole-scope
# `--reanchor "<reason>"` at an integration point, with the reason naming the CI failure or the
# growth accepted, and TOLERANCE 0. Run `--whole-tree` before a freeze and before publishing a
# release; it is no longer a scope that can be left red and unreported.
#
# Gate 2 (coverage) and Gate 5 (mutation) stay fork-scoped: their runs are expensive and
# their baselines are meaningful per-file. Whole-tree coverage is measured separately
# (docs/CONVENTIONS.md records the numbers and their date).
#
# Gates 4, 7 and 8 also run the `tools` scope (tests/tools-sources.txt — the fork's own
# tooling under tools/, with its own baselines) in the same gate row, so a regression in
# the tooling fails the same gate as a regression in the product.
#
# Gate 11 (evidence / oversized files, added 2026-09-13 with REPO-2, extended 2026-09-16)
# measures what the tree carries besides code — evidence suffixes, stray renders,
# archives/packages, compiled artefacts, and anything over the size cap — and runs its
# own --self-test control in the same row, so the gate's red/green fixtures are part of
# every suite run rather than a claim in a document. tests/QA-GATES.md documents both.
#
# Gate 12 (real-time safety / the whole-tree sweep, added 2026-09-16 with board card
# #678 and feature row 52) is the sweeping enforcement of AGENTS.md's realtime rule:
# tests/rt-safety-sweep.py over tests/rt-safety-scope.txt, judged against
# tests/rt-safety-allowlist.txt. tests/QA-GATES.md and docs/RT-SAFETY-SWEEP.md document
# it; it needs no build, so it runs in the default set and in CI's static-gates job.
#
# Gate 13 (the scripted checks, added 2026-09-17 with CI-FIX6) runs what CI's checks.yml
# `scripted-checks` job runs on every push: tests/scripted/verify (the fixtures' own
# red/green control), tests/scripted/check-namespace and tests/scripted/check-strings.
# Until this row existed this suite did not run them, so a header that opens a namespace
# of its own - include/zene/api/ControlApi.h, the ARCH-2 boundary - turned CI red in run
# 35253612944 ("include/zene/api/ControlApi.h: File has no namespace lmms") while every
# local run of THIS script stayed green: nothing that runs by default measured the
# scripted checks, which is the same blind spot the SCOPE POLICY above was written about.
# check-strings parses the stylesheets with python3-tinycss2 - the one non-stdlib import
# in the scripted checks, installed by CI in its job and not carried in the source tree -
# so a machine without it records the row as SKIP with that reason rather than passing
# quietly: a skipped gate is not a pass (see Exit codes).
#
# Gate 14 (the release oracle's own red/green proof, added 2026-09-22 with board card #738)
# runs tests/test-release-ref-fitness.sh. tests/release-ref-fitness.sh is the only thing
# that decides whether a ref may be cut a tag, and a check that has never been seen
# refusing a ref is a claim rather than a gate — so its harness drives it BOTH ways
# against refs it builds itself: a deliberately red ref is REFUSED by g9-fork-sources, the
# same leg PASSES on the ref that red ref was cut from, a leg that cannot be measured
# refuses rather than passing, and the staging-path policy gate has been seen red against a
# deliberately weakened workflow copy. Until this row existed the harness was wired into
# nothing, and two of its controls had silently stopped controlling: control 2 compared a
# literal `static gates (3, 4, 6, 7, 8, 9, 11)` against a job that quality-gates.yml names
# `static gates (3, 4, 6, 7, 8, 9, 11, 12)`, and control 5 stripped PATH expecting that to
# remove the lizard tool, which reaches the complexity gate as a Python MODULE and so
# stayed reachable — a control that passes for the wrong reason is worse than no control,
# because it reports confidence. It needs no build and no tool beyond git and python3
# (measured 2026-09-22: 46 s on a developer box, 33 s under PYTHONNOUSERSITE=1 with
# PATH=/usr/bin:/bin, which is the bare-runner shape), so it runs in the default set.
# What that does NOT buy is CI enforcement: quality-gates.yml's static-gates job runs its
# gates as individual steps and never invokes this runner, so this row is measured by every
# suite run and by no workflow yet. Making it a CI step means renaming that job, whose name
# carries the gate numbers and is bound in tests/release-ci-evidence-gate.sh:36 and :104 -
# a change to the release path's required-job set, recorded on card #738. tests/QA-GATES.md
# ("Gate 14") says the same in full.
#
# Gate 15 (the verification-debt proof, added 2026-09-22 with board card #740) runs
# tests/test-verification-debt.sh, which drives the three gates that were fixed for
# verification debt - coverage-gate.sh's entry floor, this runner's own SKIP-is-not-a-pass
# rule, and the unregistered-source diagnosis - TWICE against the same synthetic fixture:
# once from the pre-fix revision, fetched with `git show <base>:tests/<name>`, and once from
# this working tree, asserting the direction of the change rather than just the end state.
# It is a gate for the same reason Gate 14 is: measured 2026-09-22 it was RED at the branch
# tip and wired into nothing, so four documents went on claiming it exits 0. Its fixtures had
# stopped matching the gates they drive - they stubbed only the nine gates that existed at the
# harness's own pre-fix base, so gates 10-13 ran inside it against absent scripts and FAILED,
# and its registration fixture predated Gate 9's all-sources-reproduce.sh requirement (which
# the gate reports as a setup error, tests/fork-sources-gate.sh:208). Its fixture now DERIVES
# the runner's gate count from this file's own `banner N` rows and stubs every script this
# file names, so a sixteenth gate is stubbed the day it lands; a stub interpreter at the front
# of the fixture PATH removes the python3/tinycss2 host dependency; and it asserts that it did
# not stub over the runner it tests. It needs `git` with real history (it fetches the pre-fix
# revision by name) and `python3`; measured 2026-09-22: about 1 s, so it costs a default run
# nothing. No SKIP path, on the same reasoning as Gate 14.
#
# Exit codes (a skipped gate is NOT a pass):
#   0  every gate ran and passed
#   1  at least one gate FAILED
#   3  every gate that ran passed, but at least one was SKIPPED — the run is
#      incomplete, not green. The summary names the skipped gates and how to
#      run each of them.
# Each gate's own output is printed.
# Gate 5 (mutation testing) is enforced by tests/mutation-gate.sh — see tests/QA-GATES.md.

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
cd "$ROOT" || exit 2

WITH_COVERAGE=0
SKIP_MUTATION=0
STRICT=""
SCOPE_ARG=""
for arg in "$@"; do
	case "$arg" in
		--with-coverage) WITH_COVERAGE=1 ;;
		--no-mutation) SKIP_MUTATION=1 ;;
		--strict) STRICT="--strict" ;;
		--whole-tree) SCOPE_ARG="--scope all" ;;
	esac
done

# NOTE: the `=()` on both arrays is load-bearing, not style. An array that is only *declared*
# (`declare -a RESULTS`) is UNSET, and under `set -u` an expansion of it dies with
# "<name>: unbound variable". `SKIPPED` is empty on precisely the best possible run — the one where
# every gate executed — so the summary block used to fail exactly when it had nothing to skip, and
# was masked on every run where a gate *did* skip. Same idiom as tests/fork-sources-gate.sh.
declare -a RESULTS=()
declare -a SKIPPED=()
fail=0

banner() { printf '\n================ Gate %s: %s ================\n' "$1" "$2"; }
# NOTE: record must always return 0. It is called as `cmd && record PASS || record FAIL`,
# so a non-zero return from the PASS branch would fall through and record FAIL as well.
#
# SKIP is not a failure, but it is not a pass either: the summary names every
# skipped gate and the run exits 3 (see the exit-code block in the header), so a
# gate that never ran can never make a run look green.
record() {
	RESULTS+=("$1|$2|$3")
	if [[ "$3" == "FAIL" ]]; then
		fail=1
	elif [[ "$3" == "SKIP" ]]; then
		SKIPPED+=("$1|$2|$4")
	fi
	return 0
}

# how a skipped gate is run for real — quoted in the summary
skip_hint() {
	case "$1" in
		1) echo "configure build/ (cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug -DWANT_QT6=ON)" ;;
		2) echo "pass --with-coverage" ;;
		5) echo "drop --no-mutation" ;;
		12) echo "install python3 (the sweep is a Python gate over the sources)" ;;
		13) echo "install python3-tinycss2 (sudo apt-get install -y python3-tinycss2)" ;;
		*) echo "see tests/QA-GATES.md" ;;
	esac
}

# ---- Gate 1: unit tests -----------------------------------------------------
banner 1 "unit tests (ctest)"
if [[ ! -d build ]]; then
	echo "no build/ — configure first: cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug -DWANT_QT6=ON && cmake --build build -j4"
	record 1 "ctest" "SKIP" "no configured build/ directory"
else
	# The log goes inside the build tree, not /tmp: a disk reclaim has already
	# destroyed one verification's evidence in this program, and two concurrent
	# gate runs in sibling worktrees would otherwise clobber one shared file.
	build_log="build/gate1-build.log"
	cmake --build build -j4 > "$build_log" 2>&1
	build_rc=$?
	if [[ $build_rc -ne 0 ]]; then
		echo "build FAILED (exit $build_rc) — tail of $build_log:"; tail -15 "$build_log"
		record 1 "ctest" "FAIL"
	else
		( cd build/tests && QT_QPA_PLATFORM=offscreen ctest --output-on-failure )
		ctest_rc=$?
		[[ $ctest_rc -eq 0 ]] && record 1 "ctest" "PASS" || record 1 "ctest" "FAIL"
	fi
fi

# ---- Gate 2: coverage ratchet (optional) ------------------------------------
banner 2 "coverage ratchet"
if [[ $WITH_COVERAGE -eq 1 ]]; then
	bash tests/run-coverage.sh build-coverage && \
	bash tests/coverage-gate.sh build-coverage/coverage/coverage-fork.info --check
	[[ $? -eq 0 ]] && record 2 "coverage" "PASS" || record 2 "coverage" "FAIL"
else
	echo "skipped (pass --with-coverage)"
	record 2 "coverage" "SKIP" "--with-coverage was not passed"
fi

# ---- Gate 3: no tautological tests ------------------------------------------
banner 3 "no tautological tests"
bash tests/no-tautology-gate.sh $STRICT
[[ $? -eq 0 ]] && record 3 "no-tautology" "PASS" || record 3 "no-tautology" "FAIL"

# ---- Gate 4: per-method complexity ------------------------------------------
banner 4 "per-method complexity"
bash tests/complexity-gate.sh --check $SCOPE_ARG
rc4=$?
# The fork's own tooling under tools/ has its own scope and baseline, so it is measured
# here too: same gate row, so a red tools ratchet is a red gate 4.
bash tests/complexity-gate.sh --check --scope tools
rc4t=$?
[[ $rc4 -eq 0 && $rc4t -eq 0 ]] && record 4 "complexity" "PASS" || record 4 "complexity" "FAIL"

# ---- Gate 5: mutation testing (scoped harness, enforced) --------------------
banner 5 "mutation testing (src/core/RoutingGraph.cpp)"
if [[ $SKIP_MUTATION -eq 1 ]]; then
	echo "skipped (--no-mutation)"
	record 5 "mutation" "SKIP" "--no-mutation was passed"
else
	bash tests/mutation-gate.sh
	[[ $? -eq 0 ]] && record 5 "mutation" "PASS" || record 5 "mutation" "FAIL"
fi

# ---- Gate 6: no upstream behavioural regressions ----------------------------
banner 6 "no upstream behavioural regressions"
bash tests/no-upstream-regression-gate.sh
[[ $? -eq 0 ]] && record 6 "upstream-regression" "PASS" || record 6 "upstream-regression" "FAIL"

# ---- Gate 7: per-file length -------------------------------------------------
banner 7 "per-file length (<=500 ratchet)"
bash tests/file-length-gate.sh --check $SCOPE_ARG
rc7=$?
bash tests/file-length-gate.sh --check --scope tools
rc7t=$?
[[ $rc7 -eq 0 && $rc7t -eq 0 ]] && record 7 "file-length" "PASS" || record 7 "file-length" "FAIL"

# ---- Gate 8: token duplication ----------------------------------------------
banner 8 "token duplication (<5%)"
bash tests/duplication-gate.sh $SCOPE_ARG
rc8=$?
bash tests/duplication-gate.sh --scope tools
rc8t=$?
[[ $rc8 -eq 0 && $rc8t -eq 0 ]] && record 8 "duplication" "PASS" || record 8 "duplication" "FAIL"

# ---- Gate 9: every tracked source is registered in a scope manifest ----------
banner 9 "fork-sources registration"
bash tests/fork-sources-gate.sh
[[ $? -eq 0 ]] && record 9 "fork-sources" "PASS" || record 9 "fork-sources" "FAIL"

# ---- Gate 10: test-source registration ---------------------------------------
# Gate 9 answers "is this file in a scope manifest"; it cannot answer "is this
# test ever built". Three test sources were found unregistered by hand in one
# night (one of which could not even compile), all while every gate was green.
banner 10 "test-source registration"
bash tests/unregistered-tests-gate.sh
[[ $? -eq 0 ]] && record 10 "unregistered-tests" "PASS" || record 10 "unregistered-tests" "FAIL"

# ---- Gate 11: no committed evidence, no oversized files -----------------------
# Every other gate in this suite measures CODE. Gate 11 measures what the tree
# carries besides code: run logs, exit files, merge leftovers, coverage captures,
# stray renders, archives/packages and compiled artefacts, and anything over the
# size cap. The 0.2.x line shipped 140.4 MiB of it (1,368 tracked files) while all
# ten gates above stayed green, and the owner's CP-1 decision on 2026-09-13 is what
# removed it; this gate is what stops it re-accumulating. Its own red/green control
# runs FIRST and is part of the row — a gate that has never been seen red is a
# claim: the control goes red on a .log, an over-cap file, an archive and a compiled
# artefact (naming each refused path) and on a TRACKED over-cap file through the git
# index, and green on a clean tree and on an exempted path.
banner 11 "no committed evidence / no oversized files"
bash tests/evidence-gate.sh --self-test
rc11c=$?
bash tests/evidence-gate.sh
rc11=$?
[[ $rc11c -eq 0 && $rc11 -eq 0 ]] && record 11 "evidence" "PASS" || record 11 "evidence" "FAIL"

# ---- Gate 12: real-time safety (the whole-tree sweep) ------------------------
# AGENTS.md rule 4 - "no allocation, no locking, no unbounded growth on
# audio-thread paths" - was held per feature until 2026-09-16; docs/CONVENTIONS.md
# row 9 said so in as many words ("partially enforced - a rule held by tests where
# they exist, not by a sweeping gate"). tests/rt-safety-sweep.py now sweeps every
# path:symbol pair the tree DECLARES in tests/rt-safety-scope.txt and judges what it
# finds against tests/rt-safety-allowlist.txt - a reason and a COUNT per accepted
# hit, a ratchet that may only fall - and it reads sources, so it needs no build and
# runs here rather than in gate 1. Its own control is the ctest RtSafetySelfTest
# (a deliberate allocation, lock and growth MUST fail); docs/RT-SAFETY-SWEEP.md
# records the same control run against this tree.
banner 12 "real-time safety (whole-tree sweep)"
if ! command -v python3 > /dev/null 2>&1; then
	echo "no python3 on PATH — the sweep is a Python gate over the sources"
	record 12 "rt-safety" "SKIP" "no python3 on PATH"
else
	python3 tests/rt-safety-sweep.py --check
	[[ $? -eq 0 ]] && record 12 "rt-safety" "PASS" || record 12 "rt-safety" "FAIL"
fi

# ---- Gate 13: the scripted checks (namespace / strings / the harness) ---------
# CI's checks.yml `scripted-checks` job runs these on every push; this row is the local
# half of the same gate (the Gate 13 paragraph in the header says why it exists). The
# fixtures' own red/green control (tests/scripted/verify) runs FIRST and is part of the
# row: it drives both scripts against synthetic trees that MUST fail and MUST pass, so a
# script that stopped detecting its class of defect is a red gate here rather than a
# green one. The scripts are run as executables, exactly as CI runs them, because their
# interpreter is the shebang's (`#!/usr/bin/python3`) and not this shell.
banner 13 "scripted checks (scripted/verify + check-namespace + check-strings)"
if ! command -v python3 > /dev/null 2>&1; then
	echo "no python3 on PATH — the scripted checks are Python scripts"
	record 13 "scripted-checks" "SKIP" "no python3 on PATH"
else
	tests/scripted/verify
	rc13v=$?
	tests/scripted/check-namespace
	rc13n=$?
	if python3 -c 'import tinycss2' > /dev/null 2>&1; then
		tests/scripted/check-strings
		rc13s=$?
		strings_missing=""
	else
		echo "python3-tinycss2 is not installed — check-strings cannot parse the stylesheets"
		echo "(to run it: sudo apt-get install -y python3-tinycss2)"
		rc13s=0
		strings_missing="check-strings did not run: python3-tinycss2 is not installed"
	fi
	if [[ $rc13v -ne 0 || $rc13n -ne 0 || $rc13s -ne 0 ]]; then
		record 13 "scripted-checks" "FAIL"
	elif [[ -n "$strings_missing" ]]; then
		record 13 "scripted-checks" "SKIP" "$strings_missing"
	else
		record 13 "scripted-checks" "PASS"
	fi
fi

# ---- Gate 14: the release oracle's own red/green proof -----------------------
# The header's Gate 14 paragraph says why this row exists. It has no SKIP path on purpose:
# it needs nothing but git and python3, and a harness that cannot run is not a pass either.
banner 14 "release oracle's own red/green proof (test-release-ref-fitness.sh)"
bash tests/test-release-ref-fitness.sh
[[ $? -eq 0 ]] && record 14 "release-fitness-selftest" "PASS" || record 14 "release-fitness-selftest" "FAIL"

# ---- Gate 15: the verification-debt proof ------------------------------------
# The header's Gate 15 paragraph says why this row exists. Like Gate 14 it has no SKIP path:
# a harness that cannot run is not a pass, and this one costs about a second.
banner 15 "verification-debt red/green proof (test-verification-debt.sh)"
bash tests/test-verification-debt.sh
[[ $? -eq 0 ]] && record 15 "verification-debt-selftest" "PASS" || record 15 "verification-debt-selftest" "FAIL"

# ---- summary ----------------------------------------------------------------
printf '\n================ SUMMARY ================\n'
printf '%-6s %-24s %s\n' "gate" "name" "result"
for row in "${RESULTS[@]}"; do
	IFS='|' read -r g n r <<< "$row"
	printf '%-6s %-24s %s\n' "$g" "$n" "$r"
done
echo
# The enforced scope, named on every run, and the one scope this run does NOT measure - so
# "everything passed" can never be read as "the whole tree was measured". See SCOPE POLICY
# in the header and tests/QA-GATES.md.
if [[ "$SCOPE_ARG" == "--scope all" ]]; then
	echo "scope: whole-tree (tests/all-sources.txt) + tools — gates 4, 7 and 8 were run over both"
else
	echo "scope: fork (tests/fork-sources.txt) + tools — the enforced scope, the same one CI's"
	echo "       static-gates job runs. The WHOLE-TREE scope (gates 4/7/8 --scope all) was NOT"
	echo "       measured by this run; use --whole-tree for it (tests/QA-GATES.md, 'Scope policy')."
fi
echo
if [[ ${#SKIPPED[@]} -gt 0 ]]; then
	printf 'skipped: %d of %d gates did not run\n' "${#SKIPPED[@]}" "${#RESULTS[@]}"
	for row in "${SKIPPED[@]}"; do
		IFS='|' read -r g n why <<< "$row"
		printf '  gate %s (%s): %s — to run it: %s\n' "$g" "$n" "$why" "$(skip_hint "$g")"
	done
	echo
fi
if [[ $fail -eq 1 ]]; then
	echo "RESULT: FAIL — see the failing gate above"
	exit 1
fi
if [[ ${#SKIPPED[@]} -gt 0 ]]; then
	echo "RESULT: PASS-WITH-SKIPS (exit 3) — ${#SKIPPED[@]} of ${#RESULTS[@]} gates did not run;"
	echo "        this run is INCOMPLETE, not green. Run the gates listed above, then re-run."
	exit 3
fi
echo "RESULT: PASS — every executed gate passed (${#RESULTS[@]}/${#RESULTS[@]} ran)"
