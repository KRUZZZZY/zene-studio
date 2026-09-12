#!/usr/bin/env bash
# test-release-version-gate.sh — red/green proof for tests/release-version-gate.sh.
#
# WHY THIS EXISTS. A guard nobody has seen fail is a claim, not a guard: if every control passes,
# the run proves only that the gate can say yes. This harness therefore drives the gate BOTH ways —
# against the real tree (must pass) and against copies/tags that carry exactly the defects the gate
# exists to catch (must fail, each with its own exit code).
#
# THE DEFECTS EXERCISED, each one a real way a release drifts:
#   R1 the release is cut from a tag that is not the tree's version (GITHUB_REF=refs/tags/v0.1.0-alpha
#      on a tree that declares 0.2.0-alpha) — the released artefacts would report 0.1.0-alpha while
#      every document says 0.2.0-alpha
#   R2 the tree's version moves and its documents do not (VERSION_MINOR 3, notes still 0.2.0)
#   R3 the tree's version moves and the README's Download section does not (still links v0.1.0-alpha)
#   R4 the release notes for the declared version do not exist at all
#   R5 the commit is tagged with a version the tree does not declare (real git repo, real tag)
#   R6 a v<version> tag exists but this tree does not descend from it (the tag is on another commit)
#
# Usage: bash tests/test-release-version-gate.sh
# Exit status: 0 when every control behaved as declared, 1 otherwise.

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
GATE="$ROOT/tests/release-version-gate.sh"
SCRATCH="$ROOT/build/release-version-gate-scratch"

[ -f "$GATE" ] || { echo "no gate at $GATE" >&2; exit 2; }

rm -rf "$SCRATCH"
mkdir -p "$SCRATCH"

passed=0
failed=0
check() { # check <expected-exit> <label> <actual-exit>
	if [ "$3" = "$1" ]; then
		echo "  OK   $2 — exit $3 (expected $1)"
		passed=$((passed + 1))
	else
		echo "  FAIL $2 — exit $3, expected $1"
		failed=$((failed + 1))
	fi
}

VERSION="$(sed -n 's/^SET(VERSION_MAJOR[[:space:]]*"\([^"]*\)").*/\1/p' "$ROOT/CMakeLists.txt" | head -1)"
VERSION="$VERSION.$(sed -n 's/^SET(VERSION_MINOR[[:space:]]*"\([^"]*\)").*/\1/p' "$ROOT/CMakeLists.txt" | head -1)"
VERSION="$VERSION.$(sed -n 's/^SET(VERSION_RELEASE[[:space:]]*"\([^"]*\)").*/\1/p' "$ROOT/CMakeLists.txt" | head -1)"
STAGE="$(sed -n 's/^SET(VERSION_STAGE[[:space:]]*"\([^"]*\)").*/\1/p' "$ROOT/CMakeLists.txt" | head -1)"
[ -n "$STAGE" ] && VERSION="$VERSION-$STAGE"
echo "=== release-version-gate red/green harness (tree declares $VERSION) ==="
echo

# --- a copy of just the three inputs the gate reads, so a defect can be injected -----------
fixture() { # fixture <dir> [--git]
	local dir="$1"
	rm -rf "$dir"
	mkdir -p "$dir/docs"
	cp "$ROOT/CMakeLists.txt" "$dir/CMakeLists.txt"
	cp "$ROOT/README.md" "$dir/README.md"
	cp "$ROOT/docs/RELEASE-NOTES-v$VERSION.md" "$dir/docs/RELEASE-NOTES-v$VERSION.md"
}

inject_version_minor() { sed -i "s/^SET(VERSION_MINOR[[:space:]]*\"[^\"]*\")/SET(VERSION_MINOR       \"$1\")/" "$2"; }
inject_readme_version() { sed -i "s/v$VERSION/v$1/g; s/Zene Studio $VERSION/Zene Studio $1/g" "$2"; }

echo "--- the gate must PASS on what the tree actually is -------------------------------"
bash "$GATE" --repo "$ROOT" > "$SCRATCH/green-tree.log" 2>&1; rc=$?
check 0 "G0 real tree, nothing injected" "$rc"
tail -3 "$SCRATCH/green-tree.log" | sed 's/^/       /'

GITHUB_REF="refs/tags/v$VERSION" bash "$GATE" --repo "$ROOT" > "$SCRATCH/green-tag.log" 2>&1; rc=$?
check 0 "G0b real tree, running from the release tag ref GITHUB_REF=refs/tags/v$VERSION" "$rc"

echo
echo "--- the gate must FAIL on each injected defect -----------------------------------"

GITHUB_REF="refs/tags/v0.1.0-alpha" bash "$GATE" --repo "$ROOT" > "$SCRATCH/red-release-ref.log" 2>&1; rc=$?
check 1 "R1 release build running from refs/tags/v0.1.0-alpha" "$rc"
grep -m1 "\[FAIL\]" "$SCRATCH/red-release-ref.log" | sed 's/^/       /'

fixture "$SCRATCH/r2"
inject_version_minor "3" "$SCRATCH/r2/CMakeLists.txt"
bash "$GATE" --repo "$SCRATCH/r2" > "$SCRATCH/red-r2.log" 2>&1; rc=$?
check 1 "R2 tree says 0.3.0-alpha, the notes and README still say $VERSION" "$rc"
grep -m2 "\[FAIL\]" "$SCRATCH/red-r2.log" | sed 's/^/       /'

fixture "$SCRATCH/r3"
inject_readme_version "0.1.0-alpha" "$SCRATCH/r3/README.md"
bash "$GATE" --repo "$SCRATCH/r3" > "$SCRATCH/red-r3.log" 2>&1; rc=$?
check 1 "R3 README's Download section reverted to the previous release" "$rc"
grep -m1 "README" "$SCRATCH/red-r3.log" | sed 's/^/       /'

fixture "$SCRATCH/r4"
rm "$SCRATCH/r4/docs/RELEASE-NOTES-v$VERSION.md"
bash "$GATE" --repo "$SCRATCH/r4" > "$SCRATCH/red-r4.log" 2>&1; rc=$?
check 1 "R4 no release notes under the declared version" "$rc"
grep -m1 "\[FAIL\]" "$SCRATCH/red-r4.log" | sed 's/^/       /'

# R5 / R6 need real git, so the fixture becomes a tiny repository.
fixture "$SCRATCH/r5"
git -C "$SCRATCH/r5" init -q
git -C "$SCRATCH/r5" -c user.email=gate@test -c user.name=gate add -A
git -C "$SCRATCH/r5" -c user.email=gate@test -c user.name=gate commit -qm "fixture"
git -C "$SCRATCH/r5" -c user.email=gate@test -c user.name=gate tag v0.1.0-alpha
bash "$GATE" --repo "$SCRATCH/r5" > "$SCRATCH/red-r5.log" 2>&1; rc=$?
check 1 "R5 HEAD tagged v0.1.0-alpha on a tree that declares $VERSION" "$rc"
grep -m1 "\[FAIL\]" "$SCRATCH/red-r5.log" | sed 's/^/       /'

fixture "$SCRATCH/r6"
git -C "$SCRATCH/r6" init -q
git -C "$SCRATCH/r6" -c user.email=gate@test -c user.name=gate add -A
git -C "$SCRATCH/r6" -c user.email=gate@test -c user.name=gate commit -qm "fixture"
git -C "$SCRATCH/r6" -c user.email=gate@test -c user.name=gate checkout -q --orphan elsewhere
git -C "$SCRATCH/r6" -c user.email=gate@test -c user.name=gate commit -qm "other root"
git -C "$SCRATCH/r6" -c user.email=gate@test -c user.name=gate tag "v$VERSION"
git -C "$SCRATCH/r6" -c user.email=gate@test -c user.name=gate checkout -q master 2>/dev/null \
	|| git -C "$SCRATCH/r6" -c user.email=gate@test -c user.name=gate checkout -q main
bash "$GATE" --repo "$SCRATCH/r6" > "$SCRATCH/red-r6.log" 2>&1; rc=$?
check 1 "R6 a v$VERSION tag exists on a commit HEAD does not descend from" "$rc"
grep -m1 "\[FAIL\]" "$SCRATCH/red-r6.log" | sed 's/^/       /'

echo
echo "=== summary ==="
echo "controls: $((passed + failed))   passed: $passed   failed: $failed"
if [ "$failed" -gt 0 ]; then
	echo "RESULT: FAIL — a control did not behave as declared (see $SCRATCH/*.log)"
	exit 1
fi
echo "RESULT: PASS — every control behaved as declared; the gate is red on all six injected defects"
exit 0
