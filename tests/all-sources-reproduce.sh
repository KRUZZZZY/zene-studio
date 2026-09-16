#!/usr/bin/env bash
# all-sources-reproduce.sh — the whole-tree manifest's own recipe, enforced.
#
# WHY THIS EXISTS. tests/all-sources.txt carries the command that generates it ("Regenerate
# with") and the diff that verifies it ("Verify it"), and until 2026-09-15 NOTHING RAN EITHER.
# The manifest lost seven paths silently once (2026-09-12, twenty paths on re-derivation) and
# then five more (2026-09-15: include/CrashReporterFormat.h, src/core/CrashReporterFormat.cpp,
# src/core/CrashReporterWindows.cpp, src/core/ControlCommandsCrashControl.cpp, all four
# fork-NEW and therefore already registered in tests/fork-sources.txt — Gate 9 was green while
# the whole-tree scope measured none of them — plus tools/import-detection-proof.cpp). A recipe
# that nothing runs is a claim, not a check.
#
# So this script IS the "Verify it" step, run by Gate 9 (tests/fork-sources-gate.sh) on every
# run rather than by hand. It reads the recipe out of the manifest's own header — the manifest
# stays the single source of truth for its own derivation, so the two cannot drift apart — runs
# it unpiped, and requires the manifest's entry list to equal the recipe's output exactly.
#
# A path in fork-sources.txt but not in this manifest is exactly the class this catches: the
# fork scope green while the whole-tree scope measures neither the file nor its entries.
#
# Usage:
#   bash tests/all-sources-reproduce.sh                 # check tests/all-sources.txt
#   bash tests/all-sources-reproduce.sh <manifest>      # check a copy (used by --self-test)
#   bash tests/all-sources-reproduce.sh --self-test     # prove the check can fail
#
# Exit codes: 0 = REPRODUCES, 1 = the manifest and its recipe disagree, 2 = setup error.

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
MANIFEST="$HERE/all-sources.txt"
SELF_TEST=0
case "${1:-}" in
	--self-test) SELF_TEST=1 ;;
	"") ;;
	-*) echo "usage: $0 [--self-test|<manifest>]" >&2; exit 2 ;;
	*) MANIFEST="$1" ;;
esac

# The recipe is the block the manifest's own header publishes under "Regenerate with", up to
# the "Verify it" marker — one command, printed as `#`-comments with the leading `# ` stripped.
recipe_of() { # <manifest> -> the command on stdout
	awk '
		/^# Regenerate with/ { on = 1; next }
		on && /^# Verify it/ { exit }
		on && /^#/ { sub(/^# ?/, ""); print; next }
		on && !/^#/ { exit }
	' "$1"
}

run_check() { # <manifest> -> 0 if it reproduces, 1 if not
	local manifest="$1" recipe rc
	recipe="$(recipe_of "$manifest")"
	if [[ -z "${recipe// /}" ]]; then
		echo "all-sources-reproduce: no 'Regenerate with' block in $manifest — cannot check it" >&2
		return 2
	fi
	# The entry list the recipe says the manifest must hold. Run unpiped: the exit code is the
	# recipe's, never a downstream command's (a pipe would report the last stage's status).
	bash -c "$recipe" > "$tmp.expected" 2>"$tmp.err"
	rc=$?
	if [[ $rc -ne 0 ]]; then
		echo "all-sources-reproduce: the recipe itself failed (exit $rc):" >&2
		sed 's/^/  /' "$tmp.err" | head -5 >&2
		return 2
	fi
	grep -vE '^[[:space:]]*(#|$)' "$manifest" > "$tmp.have"
	if diff -u "$tmp.have" "$tmp.expected" > "$tmp.diff"; then
		return 0
	fi
	return 1
}

tmp="$(mktemp -d)/repro.$$"
trap 'rm -rf "$(dirname "$tmp")"' EXIT

if [[ $SELF_TEST -eq 1 ]]; then
	cd "$ROOT" || exit 2
	echo "all-sources-reproduce: self-test — the check must pass, then fail on a broken copy"
	rc_ok=0; rc_drop=0; rc_extra=1
	bash "$0" > "$tmp.selftest.ok" 2>&1 && rc_ok=0 || rc_ok=$?
	# control 1: one path deleted from a copy — the exact failure of 2026-09-12/15
	grep -vE '^[[:space:]]*(#|$)' "$MANIFEST" | sed '1d' > "$tmp.body"
	cat <(grep -E '^[[:space:]]*(#|$)' "$MANIFEST") "$tmp.body" > "$tmp.dropped.txt"
	bash "$0" "$tmp.dropped.txt" > "$tmp.selftest.drop" 2>&1 && rc_drop=0 || rc_drop=$?
	# control 2: one path added that the recipe cannot derive
	{ grep -E '^[[:space:]]*(#|$)' "$MANIFEST"; grep -vE '^[[:space:]]*(#|$)' "$MANIFEST"; echo "src/core/ThisPathIsNotTracked.cpp"; } > "$tmp.extra.txt"
	bash "$0" "$tmp.extra.txt" > "$tmp.selftest.extra" 2>&1 && rc_extra=0 || rc_extra=$?
	fail=0
	[[ $rc_ok -eq 0 ]] || { echo "  FAIL: the real manifest does not reproduce (exit $rc_ok)"; fail=1; }
	[[ $rc_drop -eq 1 ]] || { echo "  FAIL: a copy with one path deleted was NOT refused (exit $rc_drop)"; fail=1; }
	[[ $rc_extra -eq 1 ]] || { echo "  FAIL: a copy with an underivable path was NOT refused (exit $rc_extra)"; fail=1; }
	if [[ $fail -eq 0 ]]; then
		echo "SELF-TEST PASS: reproduces as-is; a dropped path (exit $rc_drop) and an underivable"
		echo "                path (exit $rc_extra) are both refused."
		exit 0
	fi
	echo "SELF-TEST FAIL"
	exit 1
fi

cd "$ROOT" || exit 2
if run_check "$MANIFEST"; then
	echo "REPRODUCES: the entry list in $(basename "$MANIFEST") is the recipe's own output."
	exit 0
fi
rc=$?
if [[ $rc -eq 2 ]]; then
	exit 2
fi
echo "FAIL: $(basename "$MANIFEST") does not match the recipe in its own header."
echo "      '-' is an entry the recipe does not derive, '+' is a path the recipe derives and"
echo "      the manifest is missing. A missing fork-NEW path is a file the whole-tree scope"
echo "      measures nowhere while Gate 9 stays green:"
sed -E 's/^/      /' "$tmp.diff" | head -40
echo
echo "      Fix by re-deriving the list: run the 'Regenerate with' command from the manifest's"
echo "      header and write its output over the entry list (never hand-edit, never delete a"
echo "      path the recipe derives)."
exit 1
