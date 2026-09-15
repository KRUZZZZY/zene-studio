#!/usr/bin/env bash
# release-staging-path-gate.sh — the release job is the ONLY sanctioned way to stage
# artefacts. REL-2 (2026-09-15).
#
# WHY THIS EXISTS
# ---------------
# Two independent holes let a release be staged off a ref nobody had judged:
#
#   1. every package upload step in build.yml was
#        `if: startsWith(github.ref, 'refs/tags/') || github.event_name == 'workflow_dispatch'`
#      so a manual dispatch on ANY branch staged the release packages with no fitness
#      verdict anywhere in the run. That is a second sanctioned staging path, and the
#      release gate cannot see it: a dispatch does not create a tag, and the gate is
#      tag-only.
#   2. the tag-time `release-gate` job is `needs:` every build job with no `if: always()`,
#      so on a red matrix GitHub SKIPS it (measured: build run 34870198514 @ f611c888b,
#      5 of 7 platform jobs red, `release gate (green matrix required)` = `skipped`). A
#      skipped job records no refusal, and `skipped` is not a conclusion any downstream
#      consumer can read as "this ref was judged and refused".
#
# This gate is the mechanical form of the three things the release path must keep true.
# It reads the tracked workflow files and refuses the SHAPE that would skip, so the
# property cannot be lost by a later edit that looks harmless:
#
#   A1  staging is tag-only          every release-package upload step carries a tag-ref
#                                    guard and no branch-reachable alternative
#   A2  the release job's coverage   build.yml's release-gate `needs:` names every build
#                                    job, so the seven-platform matrix is in its graph
#   A3  FAIL, never SKIP             that job carries `always()`, so a red upstream makes
#                                    it run and refuse instead of being skipped away
#   A4  one release job              .github/workflows/release.yml exists and runs
#                                    tests/release-ref-fitness.sh
#   A5  no conclusion filter         the release workflow has no `if:` that reads an
#                                    upstream conclusion — the idiom that turns a red
#                                    upstream into a green no-op
#   A6  staging needs the verdict    the release workflow's staging job `needs:` the
#                                    fitness job, so nothing is staged before the refusal
#
# Usage: bash tests/release-staging-path-gate.sh [--verbose]
# Exit codes: 0 = every assertion holds, 1 = at least one does not, 2 = setup error.

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# RELEASE_STAGING_ROOT lets the self-test (tests/test-release-ref-fitness.sh) run this
# checker against a deliberately weakened COPY of the workflows, which is the only way to
# prove it has ever been seen red. Unset (the normal case) it reads this repository.
ROOT="${RELEASE_STAGING_ROOT:-$(cd "$HERE/.." && pwd)}"
cd "$ROOT" || exit 2

VERBOSE=0
[ "${1:-}" = "--verbose" ] && VERBOSE=1

command -v python3 >/dev/null 2>&1 || { echo "error: python3 required" >&2; exit 2; }
[ -d .github/workflows ] || { echo "error: no .github/workflows" >&2; exit 2; }

FAILED=0
verdict() { # verdict <id> <ok|FAIL> <sentence>
	if [ "$2" = "ok" ]; then
		printf '  PASS %-4s %s\n' "$1" "$3"
	else
		printf '  FAIL %-4s %s\n' "$1" "$3"
		FAILED=1
	fi
}

echo "=== release staging path: is the release job the only way to stage artefacts? ==="

# ---------------------------------------------------------------------------
# A1 — staging is tag-only. The package/release uploads are recognised by the
# same path rule tests/test-package-upload-guard.sh uses (build/<pkg>.AppImage|dmg|exe),
# so the two gates agree on which steps are release staging.
# ---------------------------------------------------------------------------
A1_OUT="$(python3 - <<'PY'
import glob, re, sys

STAGING = re.compile(r"build[/\\][^/\\]*\.(?:AppImage|dmg|exe)$")
bad, good, steps = [], 0, 0
for path in sorted(glob.glob(".github/workflows/*.yml")):
    text = open(path, encoding="utf-8").read().splitlines()
    steps_all, cur = [], None
    for ln in text:
        if re.match(r"^      - ", ln):
            if cur is not None:
                steps_all.append(cur)
            cur = []
        if cur is not None:
            cur.append(ln)
    if cur is not None:
        steps_all.append(cur)
    for st in steps_all:
        body = "\n".join(st)
        if "uses: actions/upload-artifact" not in body:
            continue
        p = re.search(r"^\s+path:\s*(\S.*?)\s*$", body, re.M)
        if not (p and STAGING.search(p.group(1))):
            continue
        steps += 1
        guard = re.search(r"^\s+if:\s*(.+?)\s*$", body, re.M)
        g = guard.group(1) if guard else ""
        wf = path.split("/")[-1]
        if not g:
            bad.append("%s: a release-package upload step has no `if:` — it stages on every "
                       "push and every dispatch (unguarded staging)" % wf)
            continue
        if "workflow_dispatch" in g:
            bad.append("%s: a release-package upload step is staged on `workflow_dispatch` — "
                       "a dispatch on a branch stages release packages that no fitness "
                       "verdict has judged (guard: %s)" % (wf, g))
            continue
        if "refs/tags/" not in g:
            bad.append("%s: a release-package upload step is not tag-guarded (guard: %s)"
                       % (wf, g))
            continue
        good += 1
if steps == 0:
    bad.append("no release-package upload step found in .github/workflows/ — the "
               "workflow shape changed and this gate no longer covers staging")
for b in bad:
    print("  " + b)
print("  %d release-package upload step(s): %d tag-guarded, %d other(s)" % (steps, good, len(bad) and steps - good or 0))
sys.exit(1 if bad else 0)
PY
)"; A1_RC=$?
echo "$A1_OUT" | sed '$d' | grep -q . && echo "$A1_OUT" | head -n -1
if [ "$A1_RC" -eq 0 ]; then
	verdict A1 ok "every release-package upload step is tag-guarded (staging is tag-only)"
else
	verdict A1 FAIL "a release-package upload step is reachable on a ref the release job never judged"
fi
[ "$VERBOSE" -eq 1 ] && echo "$A1_OUT" | tail -1

# ---------------------------------------------------------------------------
# A2/A3 — build.yml's tag-time release gate: full matrix in `needs:`, and always-run.
# ---------------------------------------------------------------------------
BUILD=".github/workflows/build.yml"
[ -f "$BUILD" ] || { echo "error: no $BUILD" >&2; exit 2; }

JOB_NEEDS="$(python3 - "$BUILD" <<'PY'
import re, sys
lines = open(sys.argv[1], encoding="utf-8").read().splitlines()
# find the release-gate job key at two-space indent under jobs:
start = None
for i, ln in enumerate(lines):
    if re.match(r"^  release-gate:\s*$", ln):
        start = i
        break
if start is None:
    print("NO-JOB"); raise SystemExit
needs, always, guards = [], False, []
i = start + 1
while i < len(lines) and not re.match(r"^  [A-Za-z0-9_-]+:\s*$", lines[i]):
    ln = lines[i]
    m = re.match(r"^      - (\S+)\s*$", ln)
    if m and lines and "needs" in "\n".join(lines[max(start, i-6):i]):
        needs.append(m.group(1))
    gm = re.match(r"^    if:\s*(.+?)\s*$", ln)
    if gm:
        guards.append(gm.group(1))
        if "always()" in gm.group(1):
            always = True
    i += 1
print("NEEDS\t" + ",".join(needs))
print("ALWAYS\t" + ("yes" if always else "no"))
print("GUARD\t" + " | ".join(guards))
PY
)"
if echo "$JOB_NEEDS" | grep -q '^NO-JOB'; then
	verdict A2 FAIL "build.yml has no release-gate job — the tag-time release gate is gone"
	verdict A3 FAIL "build.yml has no release-gate job — cannot assert it fails rather than skips"
else
	NEEDS_LINE="$(printf '%s\n' "$JOB_NEEDS" | sed -n 's/^NEEDS\t//p')"
	ALWAYS_LINE="$(printf '%s\n' "$JOB_NEEDS" | sed -n 's/^ALWAYS\t//p')"
	MISSING=""
	for j in linux-x86_64 linux-arm64 macos mingw msvc msys2; do
		case ",$NEEDS_LINE," in *",$j,"*) ;; *) MISSING="$MISSING $j" ;; esac
	done
	if [ -z "$MISSING" ]; then
		verdict A2 ok "release-gate needs: all six build jobs (linux-x86_64, linux-arm64, macos, mingw, msvc, msys2 = the seven platforms)"
	else
		verdict A2 FAIL "release-gate does not need:$MISSING — the matrix is not fully in its graph"
	fi
	if [ "$ALWAYS_LINE" = "yes" ]; then
		verdict A3 ok "release-gate carries always(): a red upstream makes it RUN and refuse, not skip"
	else
		verdict A3 FAIL "release-gate has no always() — on a red matrix GitHub skips it, so it records 'skipped' instead of a refusal (the measured defect: run 34870198514)"
	fi
fi

# ---------------------------------------------------------------------------
# A4/A5/A6 — the release workflow itself.
# ---------------------------------------------------------------------------
REL=".github/workflows/release.yml"
if [ ! -f "$REL" ]; then
	verdict A4 FAIL "no $REL — there is no release job to judge a ref before the tag"
	verdict A5 FAIL "no $REL — cannot assert the release job has no conclusion filter"
	verdict A6 FAIL "no $REL — cannot assert staging waits for the verdict"
else
	OUT="$(python3 - "$REL" <<'PY'
import re, sys
text = open(sys.argv[1], encoding="utf-8").read()
lines = text.splitlines()
has_fitness = "release-ref-fitness.sh" in text
has_staging = bool(re.search(r"^  stage[-_]artefacts:", text, re.M))
print("FITNESS\t" + ("yes" if has_fitness else "no"))
# A conclusion filter anywhere in the workflow's `if:` lines turns a red upstream into
# a green no-op. The release path must never carry one.
filters = [ln.strip() for ln in lines
           if re.match(r"^\s+if:\s", ln) and "conclusion" in ln]
print("FILTER\t" + (" | ".join(filters) if filters else "-"))
print("STAGING\t" + ("yes" if has_staging else "no"))
if has_staging:
    m = re.search(r"^  stage[-_]artefacts:(.*?)(?=^  [A-Za-z0-9_-]+:|\Z)", text, re.M | re.S)
    body = m.group(1) if m else ""
    needs = re.findall(r"^      - (\S+)\s*$", body, re.M)
    if not needs:
        # `needs: [a, b]` is flow style; both forms are accepted because the assertion is
        # about the dependency existing, not about how it is spelled.
        mflow = re.search(r"^\s+needs:\s*\[(.*?)\]\s*$", body, re.M)
        if mflow:
            needs = [x.strip() for x in mflow.group(1).split(",") if x.strip()]
    print("STAGING_NEEDS\t" + (",".join(needs) if needs else "-"))
else:
    print("STAGING_NEEDS\t-")
PY
)"
	if printf '%s\n' "$OUT" | grep -q '^FITNESS	yes'; then
		verdict A4 ok "release.yml runs tests/release-ref-fitness.sh (the ref-fitness oracle)"
	else
		verdict A4 FAIL "release.yml does not run tests/release-ref-fitness.sh — the release job would decide without the fitness measure"
	fi
	if printf '%s\n' "$OUT" | grep -q '^FILTER	-'; then
		verdict A5 ok "release.yml has no if: that reads an upstream conclusion (a red upstream fails the job)"
	else
		verdict A5 FAIL "release.yml filters on an upstream conclusion: $(printf '%s\n' "$OUT" | sed -n 's/^FILTER\t//p') — that is the skip defect"
	fi
	SN="$(printf '%s\n' "$OUT" | sed -n 's/^STAGING_NEEDS\t//p')"
	if printf '%s\n' "$OUT" | grep -q '^STAGING	yes' && case ",$SN," in *,fitness,*|*,release-fitness,*) true ;; *) false ;; esac; then
		verdict A6 ok "the staging job needs: the fitness job — nothing is staged before the verdict"
	else
		verdict A6 FAIL "the release workflow has no staging job that needs: the fitness job (staging is not downstream of the refusal)"
	fi
fi

echo
if [ "$FAILED" -eq 1 ]; then
	echo "RESULT: FAIL — the release path can stage, or skip, around a refusal."
	exit 1
fi
echo "RESULT: PASS — staging is tag-only, the release job is in the graph of the seven-platform matrix, it fails rather than skips, and staging waits for its verdict."
exit 0
