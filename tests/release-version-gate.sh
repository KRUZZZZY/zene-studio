#!/usr/bin/env bash
# release-version-gate.sh — the version a release reports must agree with the version its
# documents name and with the tag it is released under.
#
# WHY THIS EXISTS
# ---------------
# The product's version string is not one fact in one place. `CMakeLists.txt` declares it as the
# fallback; `cmake/modules/VersionInfo.cmake` overrides it from `git describe` whenever a tag is
# reachable, so the string a *release* actually reports is the tag's, not the file's;
# `docs/RELEASE-NOTES-v<version>.md` names it; `README.md` advertises it to downloads; the issue
# template asks every bug reporter for it; and CI packages it as `zene-<version>-<platform>`. Nothing
# checked that those agree, and both ways they drift are silent:
#
#   * tag the release commit with a version the tree does not declare — the artefacts then report a
#     version no document mentions, and the notes describe a different release;
#   * ship documents for the previous release — the download link, the notes and the feedback form
#     name a product version that is not what users are running.
#
# `tests/release-honesty-gate.sh` cannot see either defect: it reads build *options*, so it proves
# what the build CONTAINS, not what it IS CALLED.
#
# WHAT IT CHECKS
#   1  tree     CMakeLists.txt's VERSION_MAJOR/VERSION_MINOR/VERSION_RELEASE[/VERSION_STAGE] compose
#               the fallback version `<major>.<minor>.<release>[-<stage>]`
#   2  docs     docs/RELEASE-NOTES-v<version>.md exists and its H1 names <version>
#   3  README   the Download section names <version> and links `releases/tag/v<version>`
#   4  tag      the release build's ref (GITHUB_REF=refs/tags/v…) must be exactly v<version>;
#               a v* tag pointing at HEAD must be v<version>; and a tag named v<version> that exists
#               in the repository must be an ancestor of HEAD (or HEAD itself)
#
# Usage:
#   bash tests/release-version-gate.sh [--repo DIR] [--version X.Y.Z-stage]
#
# --repo is for the red/green harness (tests/test-release-version-gate.sh), which runs this gate
# against deliberately broken copies of the tree. --version is for a deliberate override.
#
# Exit status: 0 the version, the documents and the tag agree; 1 they disagree; 2 the tree's own
# version cannot be read at all (fail-closed: an unreadable version is not agreement).

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
OVERRIDE_VERSION=""

while [ $# -gt 0 ]; do
	case "$1" in
		--repo)    ROOT="${2:?--repo needs a directory}";    shift 2 ;;
		--version) OVERRIDE_VERSION="${2:?--version needs a value}"; shift 2 ;;
		-h|--help) sed -n '2,36p' "$HERE/release-version-gate.sh" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) echo "release-version-gate: unknown argument '$1'" >&2; exit 2 ;;
	esac
done

CMAKELISTS="$ROOT/CMakeLists.txt"
README="$ROOT/README.md"
[ -f "$CMAKELISTS" ] || { echo "release-version-gate: no CMakeLists.txt under $ROOT" >&2; exit 2; }

version_var() { # version_var <NAME>
	sed -n "s/^SET($1[[:space:]]*\"\([^\"]*\)\").*/\1/p" "$CMAKELISTS" | head -1
}

if [ -n "$OVERRIDE_VERSION" ]; then
	VERSION="$OVERRIDE_VERSION"
else
	V_MAJOR="$(version_var VERSION_MAJOR)"
	V_MINOR="$(version_var VERSION_MINOR)"
	V_RELEASE="$(version_var VERSION_RELEASE)"
	V_STAGE="$(version_var VERSION_STAGE)"
	if [ -z "$V_MAJOR" ] || [ -z "$V_MINOR" ] || [ -z "$V_RELEASE" ]; then
		echo "release-version-gate: CMakeLists.txt does not declare VERSION_MAJOR/MINOR/RELEASE" >&2
		echo "  (a version nobody can read is not a version — fail-closed)" >&2
		exit 2
	fi
	VERSION="$V_MAJOR.$V_MINOR.$V_RELEASE"
	[ -n "$V_STAGE" ] && VERSION="$VERSION-$V_STAGE"
fi

echo "=== release version: the tree, its documents and its tag must name the same release ==="
echo "repo             : $ROOT"
echo "declared version : $VERSION"
echo

failures=0
fail() { echo "  [FAIL] $1"; failures=$((failures + 1)); }
pass() { echo "  [PASS] $1"; }

# --- 1. the tree's own value, echoed so a reviewer can see what was read -------------
pass "tree: CMakeLists.txt declares $VERSION"

# --- 2. docs/RELEASE-NOTES-v<version>.md ---------------------------------------------------
NOTES="$ROOT/docs/RELEASE-NOTES-v$VERSION.md"
if [ ! -f "$NOTES" ]; then
	fail "docs: no docs/RELEASE-NOTES-v$VERSION.md — the release has no notes under its own version"
else
	NOTES_H1="$(sed -n 's/^# \(.*\)$/\1/p' "$NOTES" | head -1)"
	case "$NOTES_H1" in
		*"$VERSION"*) pass "docs: docs/RELEASE-NOTES-v$VERSION.md, H1 \"$NOTES_H1\"" ;;
		*)            fail "docs: docs/RELEASE-NOTES-v$VERSION.md has H1 \"$NOTES_H1\", which does not name $VERSION" ;;
	esac
fi

# --- 3. README's Download section ---------------------------------------------------------
if [ ! -f "$README" ]; then
	fail "README: no README.md"
else
	DOWNLOAD="$(sed -n '/^## Download/,/^## /p' "$README" | grep -v '^## ')"

	if printf '%s' "$DOWNLOAD" | grep -qF "releases/tag/v$VERSION"; then
		pass "README: the Download section links releases/tag/v$VERSION"
	else
		LINKED="$(printf '%s' "$DOWNLOAD" | grep -o 'releases/tag/v[^) ]*' | head -1)"
		fail "README: the Download section links '${LINKED:-nothing}', not releases/tag/v$VERSION"
	fi

	# The version as a bare string, so "0.2.0-alpha" is not satisfied by "0.20.0-alpha".
	if printf '%s' "$DOWNLOAD" | grep -qE "(^|[^0-9.-])$(printf '%s' "$VERSION" | sed 's/\./\\./g')([^0-9.-]|$)"; then
		pass "README: the Download section names $VERSION"
	else
		NAMED="$(printf '%s' "$DOWNLOAD" | grep -oE 'Zene Studio [0-9]+\.[0-9]+\.[0-9]+(-[a-z0-9.]+)?' | head -1)"
		fail "README: the Download section names '${NAMED:-no version}', not $VERSION"
	fi
fi

# --- 4. the tag -----------------------------------------------------------------------------
# (a) the ref a release build is running from — the one check that is deterministic at the tag.
if [ -n "${GITHUB_REF:-}" ]; then
	case "$GITHUB_REF" in
		refs/tags/*)
			REF_TAG="${GITHUB_REF#refs/tags/}"
			if [ "$REF_TAG" = "v$VERSION" ]; then
				pass "tag: the release ref $GITHUB_REF matches the declared version"
			else
				fail "tag: the release ref is $GITHUB_REF but this tree declares $VERSION (expected v$VERSION) — the artefacts would report $REF_TAG"
			fi
			;;
		*) echo "  [skip] tag: GITHUB_REF=$GITHUB_REF is not a tag ref (not a release build)" ;;
	esac
fi

if [ -d "$ROOT/.git" ] || git -C "$ROOT" rev-parse --git-dir >/dev/null 2>&1; then
	HEAD_TAGS="$(git -C "$ROOT" tag --points-at HEAD 2>/dev/null | grep -E '^v[0-9]' || true)"
	if [ -n "$HEAD_TAGS" ]; then
		BAD_TAGS=""
		while IFS= read -r t; do
			[ -z "$t" ] && continue
			[ "$t" = "v$VERSION" ] || BAD_TAGS="$BAD_TAGS $t"
		done <<< "$HEAD_TAGS"
		if [ -n "$BAD_TAGS" ]; then
			fail "tag: HEAD is tagged$BAD_TAGS but the tree declares $VERSION — a tag on this commit must be v$VERSION"
		else
			pass "tag: HEAD carries v$VERSION"
		fi
	fi

	if git -C "$ROOT" rev-parse --verify --quiet "refs/tags/v$VERSION^{commit}" >/dev/null 2>&1; then
		if git -C "$ROOT" merge-base --is-ancestor "refs/tags/v$VERSION" HEAD 2>/dev/null; then
			pass "tag: v$VERSION exists and is in HEAD's history"
		else
			fail "tag: v$VERSION exists but HEAD does not descend from it — this tree is not the $VERSION release"
		fi
	else
		echo "  [skip] tag: no v$VERSION tag exists yet (the owner creates it at freeze, Block C/F)"
	fi
else
	echo "  [skip] tag: $ROOT is not a git working tree"
fi

echo
if [ "$failures" -gt 0 ]; then
	echo "RESULT: FAIL — $failures disagreement(s) between the version, the documents and the tag"
	echo "Fix the tree or the documents — never the check. A release that ships a version string it"
	echo "does not declare is how the wrong version reaches users."
	exit 1
fi
echo "RESULT: PASS — $VERSION is the tree's, its release notes' and its download link's version"
exit 0
