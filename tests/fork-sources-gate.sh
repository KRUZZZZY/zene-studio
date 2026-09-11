#!/usr/bin/env bash
# fork-sources-gate.sh — Gate 9: every tracked first-party source is registered
# in a scope manifest, and fork-NEW code is named in tests/fork-sources.txt.
#
# Rule. Every tracked source file under src/, include/, plugins/ and tests/ must be in
# exactly one of the two scope manifests:
#
#   tests/fork-sources.txt  -> OK: this product's own new code. The fork-scoped
#                              ratchets (coverage, complexity, file-length,
#                              duplication) and Gate 2's per-file baseline measure it.
#   tests/all-sources.txt   -> OK: inherited upstream code, in the whole-tree scope.
#   in neither list         -> VIOLATION, named below as
#                              "not in tests/fork-sources.txt".
#
# Why this gate exists. `include/LatencyCompensation.h` and `src/core/LatencyCompensation.cpp`
# shipped on 2026-09-10 outside every scope list. The only gate that reacted was Gate 6,
# which reported them as "undeclared change to upstream-inherited code" — the wrong
# diagnosis for two files upstream has never had, and it cost a day to read correctly.
# A file that is in no list is invisible to every ratchet, so this gate says that plainly.
#
# Deliberate exclusions (stated, not silently skipped):
#   - vendored third-party trees: src/3rdparty/, plugins/NeuralAmp/rtneural/,
#     plugins/NeuralAmp/nam/, plugins/NeuralAmp/tests/,
#     plugins/RnnoiseDenoiser/rnnoise/ (the same list as fork-sources.txt's header);
#   - tests/reference/** — frozen verbatim copies of upstream files whose provenance
#     is recorded blob-by-blob in tests/reference/ORIGIN.tsv.
# Files under those trees are not this repo's code and no ratchet refactors them.
#
# Usage:
#   bash tests/fork-sources-gate.sh            # report violations only
#   bash tests/fork-sources-gate.sh --verbose  # print the verdict for every file
#
# Exit codes: 0 = every file is registered, 1 = unregistered file(s) found, 2 = setup error.

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
cd "$ROOT" || exit 2

VERBOSE=0
for arg in "$@"; do
	case "$arg" in
		--verbose|-v) VERBOSE=1 ;;
		*) echo "usage: $0 [--verbose]" >&2; exit 2 ;;
	esac
done

FORK_FILE="$HERE/fork-sources.txt"
ALL_FILE="$HERE/all-sources.txt"
for f in "$FORK_FILE" "$ALL_FILE"; do
	if [[ ! -f "$f" ]]; then
		echo "error: scope manifest not found: $f" >&2
		exit 2
	fi
done

declare -A FORK_NEW=()
declare -A ALL_KNOWN=()
while IFS= read -r path; do
	[[ -n "$path" ]] && FORK_NEW["$path"]=1
done < <(grep -vE '^[[:space:]]*(#|$)' "$FORK_FILE")
while IFS= read -r path; do
	[[ -n "$path" ]] && ALL_KNOWN["$path"]=1
done < <(grep -vE '^[[:space:]]*(#|$)' "$ALL_FILE")

is_source() {
	case "$1" in
		*.cpp|*.c|*.h|*.hpp|*.cc|*.cxx) return 0 ;;
		*) return 1 ;;
	esac
}

is_excluded() {
	case "$1" in
		src/3rdparty/*|plugins/NeuralAmp/rtneural/*|plugins/NeuralAmp/nam/* \
		|plugins/NeuralAmp/tests/*|plugins/RnnoiseDenoiser/rnnoise/*|tests/reference/*)
			return 0 ;;
		*) return 1 ;;
	esac
}

mapfile -t TRACKED < <(git ls-files -- src include plugins tests | LC_ALL=C sort)

scanned=0
declare -a UNREGISTERED=()
declare -a INHERITED=()

for f in "${TRACKED[@]}"; do
	is_source "$f" || continue
	is_excluded "$f" && continue
	scanned=$((scanned + 1))
	if [[ -n "${FORK_NEW[$f]:-}" ]]; then
		[[ $VERBOSE -eq 1 ]] && printf '%-56s %s\n' "$f" "fork-sources (registered)"
	elif [[ -n "${ALL_KNOWN[$f]:-}" ]]; then
		INHERITED+=("$f")
		[[ $VERBOSE -eq 1 ]] && printf '%-56s %s\n' "$f" "all-sources (inherited upstream)"
	else
		UNREGISTERED+=("$f")
		printf '%-56s %s\n' "$f" "NOT IN tests/fork-sources.txt"
		printf '%-56s %s\n' "" "(and not known upstream in tests/all-sources.txt)"
	fi
done

# The other direction: a manifest entry with no file is a stale entry — the scope
# claims to measure something that is not there. Reported, not fatal (cleanup is
# deliberate work, and this gate's job is visibility).
stale=0
while IFS= read -r path; do
	if [[ -n "$path" && ! -e "$path" ]]; then
		printf '%-56s %s\n' "$path" "stale entry in tests/fork-sources.txt (no such file)"
		stale=$((stale + 1))
	fi
done < <(grep -vE '^[[:space:]]*(#|$)' "$FORK_FILE" | LC_ALL=C sort -u)

echo
echo "scanned $scanned tracked source file(s) under src/, include/, plugins/, tests/;"
echo "  ${#FORK_NEW[@]} fork-sources entry(ies), ${#INHERITED[@]} inherited upstream, ${stale} stale entry(ies)."

if [[ ${#UNREGISTERED[@]} -gt 0 ]]; then
	echo
	echo "FAIL: ${#UNREGISTERED[@]} tracked source file(s) are not in tests/fork-sources.txt"
	echo "      (and are not known upstream in tests/all-sources.txt):"
	for f in "${UNREGISTERED[@]}"; do
		echo "  $f"
	done
	echo
	echo "A file in no scope list is measured by no gate. For each file above:"
	echo "  - new code this product adds -> add it to tests/fork-sources.txt, then re-run"
	echo "    the ratchets it widens (complexity, file-length, duplication — each has its"
	echo "    own per-scope baseline);"
	echo "  - inherited from upstream master -> add it to tests/all-sources.txt."
	exit 1
fi

echo
echo "PASS: every tracked source in scope is registered (${#FORK_NEW[@]} fork-NEW, ${#INHERITED[@]} inherited)."
exit 0
