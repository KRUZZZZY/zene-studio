#!/usr/bin/env bash
# fork-sources-gate.sh — Gate 9: every tracked first-party source is registered
# in a scope manifest, and fork-NEW code is named in tests/fork-sources.txt.
#
# Rule. Every tracked source file under src/, include/, plugins/, tests/, tools/ and
# modules/ must be in one of the three scope manifests:
#
#   tests/fork-sources.txt  -> OK: this product's own new PRODUCT code. The fork-scoped
#                              ratchets (coverage, complexity, file-length,
#                              duplication) and Gate 2's per-file baseline measure it.
#   tests/all-sources.txt   -> OK: the whole-tree first-party C/C++ scope (upstream
#                              code plus the fork's C/C++ tests and probes).
#   tests/tools-sources.txt -> OK: the fork's own developer tooling under tools/,
#                              measured by `--scope tools` on gates 4, 7 and 8 with its
#                              own baselines (added 2026-09-11; see that file's header
#                              for why tooling must not go in either of the other two).
#   in none of them         -> VIOLATION, named below as
#                              "not in tests/fork-sources.txt".
#
# Why this gate exists. `include/LatencyCompensation.h` and `src/core/LatencyCompensation.cpp`
# shipped on 2026-09-10 outside every scope list. The only gate that reacted was Gate 6,
# which reported them as "undeclared change to upstream-inherited code" — the wrong
# diagnosis for two files upstream has never had, and it cost a day to read correctly.
# A file that is in no list is invisible to every ratchet, so this gate says that plainly.
#
# Why tools/ is scanned, and why tools gets its own list. `tools/` does not exist
# upstream (`git ls-tree -r --name-only 4e677cb6c6ab -- tools` is empty), so every file
# under it is fork-authored. Before tests/tools-sources.txt existed, such a file had no
# honest home: tests/fork-sources.txt would widen the C/C++ product ratchets onto tooling
# (measured: an 818-line Python tool with CCN up to 83), and tests/upstream-modifications.txt
# is the upstream-DIVERGENCE ledger, where a fork-authored file is a false statement.
# Scanning tools/ here means the fork's tooling is registered like everything else. Non-code
# files under tools/ (*.md, *.sample, .gitignore, transcripts) stay outside every scope by
# extension, which is exactly how the same files under src/ and tests/ are treated.
#
# Why modules/ is scanned. `modules/wasm/` is this fork's own wasm sandbox fixture tree:
# the `.wat` modules and the one reference C module (modules/wasm/demo/gain_clip.c) that
# tests/src/wasm/WasmSandboxTest.cpp compiles to wasm. The .wat files are not sources by
# extension and need no home, but gain_clip.c is a first-party C source, and it was
# registered in tests/all-sources.txt while lying OUTSIDE this gate's pathspec - a
# registration no run could ever validate (the all-scope also never measured it, so the
# file was registered and unmeasured at the same time). modules/ is now part of the scan,
# which makes that entry checkable and gives any future module source a home.
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
# Both modes also RUN the whole-tree manifest's own recipe (tests/all-sources-reproduce.sh).
# A path registered in tests/fork-sources.txt but missing from tests/all-sources.txt is
# invisible to the registration check below — the file has a home, so this gate stays green
# while the whole-tree scope measures neither it nor its entries. That is not hypothetical:
# 20 paths were lost on 2026-09-12 and 5 more on 2026-09-15 (see tests/all-sources.txt's own
# header). The recipe check is what sees it; the manifest's recipe is the single source of
# truth for its own derivation, so the command and the list cannot drift apart.
#
# Exit codes: 0 = every file is registered AND every manifest reproduces from its own recipe,
# 1 = unregistered file(s), or a manifest that does not reproduce, 2 = setup error.

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
TOOLS_FILE="$HERE/tools-sources.txt"
for f in "$FORK_FILE" "$ALL_FILE"; do
	if [[ ! -f "$f" ]]; then
		echo "error: scope manifest not found: $f" >&2
		exit 2
	fi
done
# tools-sources.txt is required of this repo, but absent in synthetic fixtures that only
# exercise the src/ side of this gate — so it is read if present and its absence is
# reported only against a tools/ file that needs it, rather than failing the whole run.
TOOLS_AVAILABLE=0
[[ -f "$TOOLS_FILE" ]] && TOOLS_AVAILABLE=1

declare -A FORK_NEW=()
declare -A ALL_KNOWN=()
declare -A TOOLS_KNOWN=()
while IFS= read -r path; do
	[[ -n "$path" ]] && FORK_NEW["$path"]=1
done < <(grep -vE '^[[:space:]]*(#|$)' "$FORK_FILE")
while IFS= read -r path; do
	[[ -n "$path" ]] && ALL_KNOWN["$path"]=1
done < <(grep -vE '^[[:space:]]*(#|$)' "$ALL_FILE")
if [[ $TOOLS_AVAILABLE -eq 1 ]]; then
	while IFS= read -r path; do
		[[ -n "$path" ]] && TOOLS_KNOWN["$path"]=1
	done < <(grep -vE '^[[:space:]]*(#|$)' "$TOOLS_FILE")
fi

is_source() {
	case "$1" in
		*.cpp|*.c|*.h|*.hpp|*.cc|*.cxx) return 0 ;;
		*) return 1 ;;
	esac
}

# tools/ is the fork's own tooling and is written in Python and shell as well as C++,
# so its extension set is wider than the product scope's.
is_tools_source() {
	case "$1" in
		*.cpp|*.c|*.h|*.hpp|*.cc|*.cxx|*.py|*.sh) return 0 ;;
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

mapfile -t TRACKED < <(git ls-files -- src include plugins tests tools modules | LC_ALL=C sort)

scanned=0
declare -a UNREGISTERED=()
declare -a INHERITED=()
declare -a TOOLING=()

for f in "${TRACKED[@]}"; do
	if [[ "$f" == tools/* ]]; then
		is_tools_source "$f" || continue
	else
		is_source "$f" || continue
	fi
	is_excluded "$f" && continue
	scanned=$((scanned + 1))
	if [[ -n "${TOOLS_KNOWN[$f]:-}" ]]; then
		TOOLING+=("$f")
		[[ $VERBOSE -eq 1 ]] && printf '%-56s %s\n' "$f" "tools-sources (fork tooling; gates 4/7/8 --scope tools)"
	elif [[ -n "${FORK_NEW[$f]:-}" ]]; then
		[[ $VERBOSE -eq 1 ]] && printf '%-56s %s\n' "$f" "fork-sources (registered)"
	elif [[ -n "${ALL_KNOWN[$f]:-}" ]]; then
		INHERITED+=("$f")
		[[ $VERBOSE -eq 1 ]] && printf '%-56s %s\n' "$f" "all-sources (whole-tree scope)"
	else
		UNREGISTERED+=("$f")
		printf '%-56s %s\n' "$f" "NOT IN tests/fork-sources.txt"
		printf '%-56s %s\n' "" "(and not in tests/all-sources.txt or tests/tools-sources.txt)"
		if [[ "$f" == tools/* && $TOOLS_AVAILABLE -eq 0 ]]; then
			printf '%-56s %s\n' "" "(this tree has no tests/tools-sources.txt at all)"
		fi
	fi
done

# The other direction: a manifest entry with no file is a stale entry — the scope
# claims to measure something that is not there. Reported, not fatal (cleanup is
# deliberate work, and this gate's job is visibility).
stale=0
list_stale() { # <manifest> <label> — prints stale entries, increments $stale
	local path
	while IFS= read -r path; do
		if [[ -n "$path" && ! -e "$path" ]]; then
			printf '%-56s %s\n' "$path" "stale entry in tests/$2 (no such file)"
			stale=$((stale + 1))
		fi
	done < <(grep -vE '^[[:space:]]*(#|$)' "$1" | LC_ALL=C sort -u)
}

list_stale "$FORK_FILE" "fork-sources.txt"
list_stale "$ALL_FILE" "all-sources.txt"
if [[ $TOOLS_AVAILABLE -eq 1 ]]; then
	list_stale "$TOOLS_FILE" "tools-sources.txt"
fi

# The manifests' own recipes, RUN rather than trusted. A manifest that carries a recipe
# nobody executes is a claim, not a check: tests/all-sources.txt lost twenty paths silently
# on 2026-09-12 and five more on 2026-09-15 — all of them fork-NEW, all of them already
# registered in tests/fork-sources.txt — so the registration check above was green while the
# whole-tree scope measured none of them. A file in fork-sources.txt and missing from
# all-sources.txt is invisible to every other check in this script; the manifest's own
# "Verify it" step is the only thing that sees it. Added 2026-09-15 with REPO-58.
echo
if [[ -f "$HERE/all-sources-reproduce.sh" ]]; then
	bash "$HERE/all-sources-reproduce.sh"
	reproduce_rc=$?
	if [[ $reproduce_rc -ne 0 ]]; then
		echo
		echo "FAIL: a scope manifest does not reproduce from its own header recipe (exit $reproduce_rc)."
		echo "      Registration and derivation are two different questions; this gate now asks both."
		exit 1
	fi
else
	echo "FAIL: tests/all-sources-reproduce.sh is missing — the whole-tree manifest's recipe"
	echo "      would be unchecked."
	exit 2
fi

echo
echo "scanned $scanned tracked source file(s) under src/, include/, plugins/, tests/, tools/, modules/;"
echo "  ${#FORK_NEW[@]} fork-sources entry(ies), ${#INHERITED[@]} all-sources (whole-tree),"
echo "  ${#TOOLING[@]} tools-sources (fork tooling), ${stale} stale entry(ies)."

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
	echo "  - inherited from upstream master -> add it to tests/all-sources.txt;"
	echo "  - the fork's own tooling under tools/ -> add it to tests/tools-sources.txt"
	echo "    (measured by gates 4, 7 and 8 with --scope tools; never put it in"
	echo "    tests/upstream-modifications.txt, which is the upstream-divergence ledger)."
	exit 1
fi

echo
echo "PASS: every tracked source in scope is registered (${#FORK_NEW[@]} fork-NEW, ${#INHERITED[@]} inherited, ${#TOOLING[@]} tooling)."
exit 0
