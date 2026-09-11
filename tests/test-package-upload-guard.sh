#!/usr/bin/env bash
# test-package-upload-guard.sh — red/green proof for the package-upload guard.
#
# The failure this covers. `.github/workflows/build.yml` uploads each platform's package
# by glob (`path: build/lmms-*.AppImage`, `*.dmg`, `*.exe`). actions/upload-artifact does
# NOT fail when that glob matches nothing: `if-no-files-found` defaults to `warn`, so the
# upload step warns and the job goes green having published ZERO assets for that platform.
# A rename that misses one packaging path (lmms-* -> zene-*) would produce exactly that:
# a green release with nothing to download.
#
# The guard is a step-level flag — `if-no-files-found: error` — on every package-upload
# step. This harness proves two things, both with exit codes:
#
#   part 1  the decision itself, exercised BOTH ways against real directories:
#             a directory holding a matching package + `error` -> accept (exit 0)
#             an empty directory               + `error`      -> FAIL   (exit 1),
#                                                                 naming the glob
#             an empty directory               + `warn`       -> accept (exit 0), which is
#                                                                 the silent pass this guard
#                                                                 exists to remove
#   part 2  every upload-artifact step in the workflow carries the right policy:
#             package step (path names *.AppImage|*.dmg|*.exe) -> if-no-files-found: error
#             diagnostic step (a test log)                     -> warn, deliberately exempt
#           and the checker itself is run against a deliberately weakened copy of the
#           workflow, where it MUST fail — a check that has never been seen red is a claim.
#
# It touches nothing outside a temp directory and never edits the workflow.
#
# Usage: bash tests/test-package-upload-guard.sh
# Exit codes: 0 = every assertion held, 1 = at least one did not, 2 = setup error.

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
cd "$ROOT" || exit 2

WORKFLOW="${WORKFLOW:-.github/workflows/build.yml}"
[[ -f "$WORKFLOW" ]] || { echo "error: no workflow at $WORKFLOW" >&2; exit 2; }
command -v python3 >/dev/null 2>&1 || { echo "error: python3 required" >&2; exit 2; }

FAILED=0
show() { printf '  $ %s\n' "$*"; }
check() { # name expected actual
	if [[ "$2" == "$3" ]]; then
		printf '  PASS %-72s %s\n' "$1" "$3"
	else
		printf '  FAIL %-72s got %s, expected %s\n' "$1" "$3" "$2"
		FAILED=1
	fi
}

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# ---------------------------------------------------------------------------
# Part 1 — the glob decision, both ways.
# Mirrors actions/upload-artifact v7 (ref 043fb46d1a93c77aae656e7c1c64a875d1fc6a0a):
# src/upload/upload-artifact.ts calls core.setFailed with
#   "No files were found with the provided path: <searchPath>. No artifacts will be uploaded."
# when ifNoFilesFound == 'error', and core.warning with the same sentence for 'warn'.
# ---------------------------------------------------------------------------
echo "=== Part 1: the glob decision, both ways ==="
mkdir -p "$TMP/has" "$TMP/empty"
: >"$TMP/has/pkg-1.0-linux-x86_64.AppImage"

glob_guard() { # <root> <glob> <policy> -> 0 = step proceeds, 1 = step fails
	local root="$1" glob="$2" policy="$3"
	local matches
	matches="$(cd "$root" && compgen -G "$glob" 2>/dev/null || true)"
	if [[ -n "$matches" ]]; then
		echo "  matched: $(tr '\n' ' ' <<<"$matches")"
		return 0
	fi
	echo "  No files were found with the provided path: $glob. No artifacts will be uploaded."
	case "$policy" in
	error) return 1 ;;
	warn | ignore) return 0 ;;
	*)
		echo "  unknown if-no-files-found policy: $policy" >&2
		return 2
		;;
	esac
}

show "glob_guard $TMP/has pkg-*.AppImage error"
glob_guard "$TMP/has" 'pkg-*.AppImage' error
check "a matching package is uploaded (exit 0)" 0 "$?"

show "glob_guard $TMP/empty pkg-*.AppImage error"
glob_guard "$TMP/empty" 'pkg-*.AppImage' error
check "no matching package fails the job under 'error' (exit 1)" 1 "$?"

show "glob_guard $TMP/empty pkg-*.AppImage warn"
glob_guard "$TMP/empty" 'pkg-*.AppImage' warn
check "no matching package only warns under 'warn' (exit 0) - the silent pass" 0 "$?"

echo

# ---------------------------------------------------------------------------
# Part 2 — the workflow carries the right policy on the right steps.
# ---------------------------------------------------------------------------
check_policies() { # <workflow-file> -> prints a table, exits 0 iff every step is right
	python3 - "$1" <<'PY'
import re, sys

path = sys.argv[1]
lines = open(path, encoding="utf-8").read().splitlines()

steps, cur = [], None
for ln in lines:
    if re.match(r"^      - ", ln):
        if cur is not None:
            steps.append(cur)
        cur = []
    if cur is not None:
        cur.append(ln)
if cur is not None:
    steps.append(cur)

PACKAGE = re.compile(r"build[/\\][^/\\]*\.(?:AppImage|dmg|exe)$")
bad, pkg, diag = [], 0, 0
print("  %-34s %-46s %s" % ("step", "path", "if-no-files-found"))
for st in steps:
    body = "\n".join(st)
    if "uses: actions/upload-artifact" not in body:
        continue
    name = re.search(r"^\s*- name:\s*(.+?)\s*$", body, re.M)
    path_in = re.search(r"^\s+path:\s*(\S.*?)\s*$", body, re.M)
    policy = re.search(r"^\s+if-no-files-found:\s*(\w+)", body, re.M)
    name = name.group(1) if name else "(unnamed)"
    path_in = path_in.group(1) if path_in else "(no path)"
    policy = policy.group(1) if policy else "warn"  # the action's default
    print("  %-34s %-46s %s" % (name[:34], path_in[:46], policy))
    if PACKAGE.search(path_in):
        pkg += 1
        if policy != "error":
            bad.append("%s: package path %s has if-no-files-found: %s (must be error)"
                       % (name, path_in, policy))
    else:
        diag += 1
        if policy == "error":
            bad.append("%s: diagnostic path %s must not fail the job (never 'error')"
                       % (name, path_in))
if pkg == 0:
    bad.append("no package upload step found - the workflow shape changed")
if diag == 0:
    bad.append("no exempt diagnostic upload step found - state the exemption explicitly")
for b in bad:
    print("  FAIL %s" % b)
print("  %d package upload step(s), %d deliberately-exempt diagnostic upload step(s)" % (pkg, diag))
sys.exit(1 if bad else 0)
PY
}

echo "=== Part 2: $WORKFLOW carries the guard on every package step ==="
show "python3 <checker> $WORKFLOW"
check_policies "$WORKFLOW" >"$TMP/live.log" 2>&1
rc=$?
cat "$TMP/live.log"
check "the workflow as committed passes the checker (exit 0)" 0 "$rc"

# The checker must be able to go red: strip the guard from a copy and require a failure.
echo
echo "=== Part 2 control: the checker fails on a workflow without the guard ==="
sed '/if-no-files-found: error/d' "$WORKFLOW" >"$TMP/weakened.yml"
show "sed '/if-no-files-found: error/d' <workflow> > weakened.yml && python3 <checker> weakened.yml"
check_policies "$TMP/weakened.yml" >"$TMP/weakened.log" 2>&1
rc=$?
grep -c '^  FAIL' "$TMP/weakened.log" >/dev/null && head -3 "$TMP/weakened.log"
check "the same workflow minus the flag is REJECTED (exit 1)" 1 "$rc"

echo
if [[ $FAILED -eq 1 ]]; then
	echo "RESULT: FAIL - at least one assertion above did not hold."
	exit 1
fi
echo "RESULT: PASS - the glob decision fails on an empty match and the workflow carries the guard."
exit 0
