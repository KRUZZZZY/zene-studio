#!/usr/bin/env bash
# evidence-gate.sh — Gate 11: no committed evidence, no oversized files.
#
# WHY THIS EXISTS (REPO-2 / CP-1, 2026-09-13). The 0.2.x line shipped 145 MB of run
# logs, exit-code files, merge leftovers, coverage captures and audio renders inside
# tests/ — 1,368 tracked files whose durable value is a hash, not a clone's disk (the
# change plan measured 145 MB / 697 `.log`; the measured removal is 140.4 MiB / 1,368
# files, hashes kept in tests/evidence-manifest.tsv). Nothing refused them: every gate
# in this suite measures CODE, and a directory full of `*.log` is not code. The owner
# took CP-1 on 2026-09-13 — "delete the evidence, keep its hashes, and land REPO-2's
# gate that refuses evidence file types + oversized files" — and this is that gate.
# It is what stops the deletion from being a one-off spring clean that re-accumulates
# in the next lane.
#
# WHAT IT REFUSES, and why each class is refused rather than tolerated:
#   1. EVIDENCE SUFFIXES (anywhere in the tree): the output of a run.
#        log exit ours theirs        — the three merge leftovers and the exit-code files
#                                      0.2.x committed for every lane
#        gcda gcno gcov lcov info    — coverage data and lcov tracefiles
#        profraw profdata            — LLVM profile data
#        junit jtr                   — machine-readable test reports
#      A `.info` file is refused because lcov's tracefile is the only `.info` this repo
#      has ever produced; if that stops being true, the exemption list below is the
#      documented way to say so with a reason (fail-closed — see "exemptions").
#   2. RENDERS (audio/video) OUTSIDE data/: wav mp3 flac ogg aiff aif opus mp4 mkv webm
#      m4a. `data/samples/**` is bundled product content (181 `.ogg`, 26 `.wav`, 33
#      `.flac` at this commit) and is exempt by DIRECTORY, not by entry — the point is
#      that a committed RENDER of a test run must not sit in the tree beside the test.
#   3. OVERSIZED FILES: any tracked file larger than EVIDENCE_SIZE_CAP_BYTES
#      (default 1048576 = 1 MiB). The cap catches the shape a text suffix cannot: a
#      `.zip`, `.bin`, `.mmp` or dumped fixture that nobody intended to commit. Four
#      files exceed it at this commit and every one is vendored third-party data; they
#      are named in tests/evidence-gate-exempt.txt with a reason.
#
# EXEMPTIONS. tests/evidence-gate-exempt.txt holds `<path prefix><TAB><reason>` lines.
# A blank reason exits 2 — an exemption without a stated reason is not honoured, the
# same fail-closed rule tests/file-length-exempt.txt and the coverage entry floor use.
# The file is a REQUIRED input (absence is exit 2, not "no exemptions"), so a deleted
# exemption home cannot silently mute the gate.
#
# Usage:
#   bash tests/evidence-gate.sh                  # the tree: every tracked file
#   bash tests/evidence-gate.sh --verbose        # print the verdict for every file
#   bash tests/evidence-gate.sh --self-test      # red/green control on a temp tree:
#                                                #   clean tree -> 0, a .log -> 1,
#                                                #   an over-cap file -> 1, an exempt
#                                                #   path -> 0. A check that has never
#                                                #   been seen red is a claim.
#   bash tests/evidence-gate.sh --tree DIR       # scan DIR instead of git ls-files
#
# Exit codes: 0 = nothing refused, 1 = at least one file refused, 2 = setup error.

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
cd "$ROOT" || exit 2

CAP="${EVIDENCE_SIZE_CAP_BYTES:-1048576}"
EXEMPT="${EVIDENCE_GATE_EXEMPT:-$HERE/evidence-gate-exempt.txt}"
MODE="check"
TREE=""
VERBOSE=0

usage() {
	sed -n '2,60p' "$HERE/evidence-gate.sh" | sed -n 's/^# \{0,1\}//p'
}

while [ $# -gt 0 ]; do
	case "$1" in
		--verbose|-v) VERBOSE=1; shift ;;
		--self-test)  MODE="self-test"; shift ;;
		--tree)       TREE="${2:?--tree needs a directory}"; shift 2 ;;
		-h|--help)    usage; exit 0 ;;
		*) echo "evidence-gate: unknown argument '$1'" >&2; exit 2 ;;
	esac
done

# Evidence and render suffixes, refused by name. Space-separated (not `|`): a case
# pattern whose alternatives come from a variable is a single LITERAL pattern in bash,
# so the list is matched with an explicit loop instead — see the note in is_evidence_name.
EVIDENCE_SUFFIXES="${EVIDENCE_SUFFIXES:-log exit ours theirs gcda gcno gcov lcov info profraw profdata junit jtr}"
# Renders, refused only outside product content (data/).
RENDER_SUFFIXES="${RENDER_SUFFIXES:-wav mp3 flac ogg aiff aif opus mp4 mkv webm m4a}"

suffix_in() { # <lowercased-extension> <space-separated list> -> 0 on a match
	local ext="$1" s
	[ -n "$ext" ] || return 1
	for s in $2; do
		[ "$ext" = "$s" ] && return 0
	done
	return 1
}

is_evidence_name() { # <repo-relative path> -> 0 if the NAME alone is refused
	local base="${1##*/}" ext="${1##*.}"
	case "$base" in
		*.log.gz|*.info.gz|*.exit.gz) return 0 ;;   # a compressed run log is still a run log
	esac
	suffix_in "${ext,,}" "$EVIDENCE_SUFFIXES"
}

is_render_name() { # <repo-relative path> -> 0 if the NAME is a render outside data/
	local ext="${1##*.}"
	case "$1" in data/*) return 1 ;; esac
	suffix_in "${ext,,}" "$RENDER_SUFFIXES"
}

# ---- exemptions: required input, fail-closed --------------------------------
if [ ! -f "$EXEMPT" ]; then
	echo "error: $EXEMPT is missing - it is this gate's exemption home, and its absence" >&2
	echo "       silently makes every exemption it would hold unenforceable." >&2
	echo "       If no exemption is needed, commit the file with its header and no entries." >&2
	exit 2
fi
declare -a EXEMPT_PATHS=()
declare -a EXEMPT_WHY=()
while IFS=$'\t' read -r ex_path ex_why; do
	[ -n "${ex_path:-}" ] || continue
	case "$ex_path" in '#'*) continue ;; esac
	if [ -z "${ex_why// /}" ]; then
		echo "exempt error: $(basename "$EXEMPT") entry '$ex_path' has no reason" >&2
		echo "an exemption must say why the file is not evidence the gate should refuse" >&2
		echo "(this gate refuses to honour a blank one)" >&2
		exit 2
	fi
	EXEMPT_PATHS+=("$ex_path")
	EXEMPT_WHY+=("$ex_why")
done < "$EXEMPT"

exempt_reason() { # <path> -> prints the reason and returns 0 when exempt
	local i p
	for i in "${!EXEMPT_PATHS[@]}"; do
		p="${EXEMPT_PATHS[$i]}"
		# A trailing "*" or "/" makes the entry a prefix/glob; `[[ == ]]` with an
		# unquoted right-hand side is bash's own glob match (not a regex), which is
		# what an exemption list should be readable as.
		if [[ "$p" == *'*'* || "$p" == *'/' ]]; then
			# shellcheck disable=SC2053  # the glob is the point here
			if [[ "$1" == $p || "$1" == "$p"* ]]; then
				printf '%s' "${EXEMPT_WHY[$i]}"
				return 0
			fi
		elif [ "$1" = "$p" ]; then
			printf '%s' "${EXEMPT_WHY[$i]}"
			return 0
		fi
	done
	return 1
}

# ---- one scan, two ways to get the file list --------------------------------
scan_list() { # prints "<bytes>\t<path>" for every candidate file, smallest work first
	local f size
	if [ -n "$TREE" ]; then
		[ -d "$TREE" ] || { echo "evidence-gate: --tree is not a directory: $TREE" >&2; exit 2; }
		while IFS= read -r f; do
			rel="${f#"$TREE"/}"
			size="$(wc -c < "$f" 2>/dev/null || echo 0)"
			printf '%s\t%s\n' "$size" "$rel"
		done < <(find "$TREE" -type f -not -path '*/.git/*' | LC_ALL=C sort)
	else
		while IFS= read -r -d '' f; do
			[ -f "$f" ] || continue        # submodule gitlinks and vanished entries
			size="$(wc -c < "$f" 2>/dev/null || echo 0)"
			printf '%s\t%s\n' "$size" "$f"
		done < <(git ls-files -z)
	fi
}

scan_tree() { # <bytes> <path> ... -> 0 clean, 1 refused; prints what it refused
	local refused=0 scanned=0 size f why
	while IFS=$'\t' read -r size f; do
		[ -n "${f:-}" ] || continue
		scanned=$((scanned + 1))
		if why="$(exempt_reason "$f")"; then
			[ "$VERBOSE" -eq 1 ] && printf '  [exempt] %s  (%s)\n' "$f" "$why"
			continue
		fi
		if is_evidence_name "$f"; then
			printf '  [REFUSED] %s  (evidence file type: the output of a run)\n' "$f"
			refused=$((refused + 1))
			continue
		fi
		if is_render_name "$f"; then
			printf '  [REFUSED] %s  (a render; audio belongs in data/, not beside a test)\n' "$f"
			refused=$((refused + 1))
			continue
		fi
		if [ "$size" -gt "$CAP" ]; then
			printf '  [REFUSED] %s  (%s bytes > %s cap)\n' "$f" "$size" "$CAP"
			refused=$((refused + 1))
			continue
		fi
		[ "$VERBOSE" -eq 1 ] && printf '  [  ok  ] %s  (%s bytes)\n' "$f" "$size"
	done < <(scan_list)
	echo
	echo "evidence-gate: $scanned file(s) scanned, $refused refused (cap ${CAP} bytes, $((${#EXEMPT_PATHS[@]})) exemption(s))"
	[ "$refused" -eq 0 ]
}

# ---- self-test: the control the gate itself is checked against --------------
self_test() {
	local tmp rc=0 mine
	tmp="$(mktemp -d)"
	mine="$HERE/evidence-gate.sh"
	# A tiny helper so every assertion reads the same and no `A && B || C` can run
	# the failure branch of a true test (shellcheck SC2015, which is a real bug shape).
	expect() { # <label> <expected-exit> <actual-exit>
		if [ "$2" -eq "$3" ]; then
			printf '  PASS %-58s exit %s\n' "$1" "$3"
		else
			printf '  FAIL %-58s exit %s (expected %s)\n' "$1" "$3" "$2"
			rc=9
		fi
	}
	# A clean tree must pass — and a .wav under data/ is product content, not a render.
	mkdir -p "$tmp/clean/src" "$tmp/clean/data/samples"
	: >"$tmp/clean/src/main.cpp"
	printf 'x' >"$tmp/clean/data/samples/shape.wav"
	: >"$tmp/clean/README.md"
	echo "=== self-test 1: a clean tree (+ a .wav under data/) -> expect 0 ==="
	bash "$mine" --tree "$tmp/clean" >/dev/null 2>&1; expect "clean tree" 0 "$?"
	# ...a committed run log must fail...
	mkdir -p "$tmp/loggy/build"
	: >"$tmp/loggy/build/ctest.log"
	echo "=== self-test 2: a committed run log -> expect 1 ==="
	bash "$mine" --tree "$tmp/loggy" >/dev/null 2>&1; expect "a .log is refused" 1 "$?"
	# ...an over-cap file must fail...
	mkdir -p "$tmp/fat"
	python3 -c "open('$tmp/fat/blob.bin','wb').write(b'0'*2000000)"
	echo "=== self-test 3: a 2 MB file against a 1 MiB cap -> expect 1 ==="
	bash "$mine" --tree "$tmp/fat" >/dev/null 2>&1; expect "an over-cap file is refused" 1 "$?"
	# ...a render outside data/ must fail...
	mkdir -p "$tmp/render/tests/out"
	: >"$tmp/render/tests/out/render-1.wav"
	echo "=== self-test 4: a render outside data/ -> expect 1 ==="
	bash "$mine" --tree "$tmp/render" >/dev/null 2>&1; expect "a stray render is refused" 1 "$?"
	# ...the exemption list must still be honoured (the same 2 MB file, exempted)...
	mkdir -p "$tmp/exempt/plugins/RnnoiseDenoiser/rnnoise"
	python3 -c "open('$tmp/exempt/plugins/RnnoiseDenoiser/rnnoise/rnnoise_data.c','wb').write(b'0'*2000000)"
	echo "=== self-test 5: the same 2 MB file under an exempted prefix -> expect 0 ==="
	EVIDENCE_GATE_EXEMPT="$EXEMPT" bash "$mine" --tree "$tmp/exempt" >/dev/null 2>&1
	expect "an exempted prefix passes" 0 "$?"
	# ...and a blank reason must be refused, not honoured (fail-closed).
	printf 'src/	\n' >"$tmp/blank-reason.txt"
	echo "=== self-test 6: an exemption with a blank reason -> expect 2 ==="
	EVIDENCE_GATE_EXEMPT="$tmp/blank-reason.txt" bash "$mine" --tree "$tmp/clean" >/dev/null 2>&1
	expect "a blank reason is a setup error" 2 "$?"
	rm -rf "$tmp"
	echo
	if [ "$rc" -ne 0 ]; then
		echo "RESULT: FAIL - the gate did not behave as its own contract says."
		exit 1
	fi
	echo "RESULT: PASS - the gate goes red on a log, an over-cap file and a stray render,"
	echo "        goes green on a clean tree and on an exempted path, and refuses a blank reason."
	exit 0
}

if [ "$MODE" = "self-test" ]; then
	self_test
fi

echo "=== evidence gate: what this tree commits, beyond code ==="
echo "cap      : ${CAP} bytes per file (EVIDENCE_SIZE_CAP_BYTES)"
echo "exempt   : ${EXEMPT#"$ROOT"/} (${#EXEMPT_PATHS[@]} entry(ies))"
if [ -n "$TREE" ]; then echo "tree     : $TREE (not the git index)"; else echo "tree     : git ls-files"; fi
echo
if scan_tree; then
	echo "PASS: no committed evidence file types and nothing over the cap."
	exit 0
fi
echo
echo "FAIL: the files above are committed evidence (or exceed the size cap)."
echo "  - a run's output belongs in a build/ or /tmp directory, or in the lane report;"
echo "    record hashes, not megabytes (tests/evidence-manifest.tsv is the shape)."
echo "  - if a file is genuinely product content or vendored data, name it in"
echo "    tests/evidence-gate-exempt.txt with a reason — never delete a gate's rule to"
echo "    make one file pass."
exit 1
