#!/usr/bin/env bash
# prove-posix-unchanged.sh — the CODE-9 (Windows named-pipe transport) local proof.
#
# Claims this proves, and how:
#
#   1. The three control translation units still COMPILE under the fork's REAL flags.
#      The flags are borrowed from a sibling tree's compile_commands.json (this lane had
#      no build tree of its own: a full build is ~25 GB and the owner directive for this
#      pass forbids spending the window on one). Missing generated headers
#      (lmms_export.h, AUTOMOC includes) are the one thing still read out of that tree.
#
#   2. The POSIX path is BYTE-FOR-BYTE UNCHANGED. Not "the diff looks small": the token
#      stream the compiler sees on POSIX is compared against release/0.3.0.
#          c++ -E <rev/file>  ->  drop line markers  ->  diff
#      The old revision is preprocessed OUT OF TREE so its own quoted includes resolve to
#      its own copies first; Qt and the rest of the worktree are shared, so the comparison
#      is like for like. src/core/ControlServer.cpp is additionally byte-identical to
#      release/0.3.0 (checked separately, as a file).
#
#   3. The new Windows TU contributes NOTHING on POSIX: its own symbols do not appear in
#      its preprocessed output at all.
#
# Usage:
#   bash tests/prove-posix-unchanged.sh [<foreign-build-tree> [<foreign-source-tree>]]
# Defaults: the merge-train worktree beside this one (…/zene-030). Any tree built from
# this fork works; only compile_commands.json and its build/ directories are read.
#
# Exit codes: 0 = every claim held, 1 = a claim failed, 2 = setup error.
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
cd "$ROOT" || exit 2

BASE_REV="${CODE9_BASE_REV:-release/0.3.0}"
# The tree the COMPILE FLAGS are borrowed from. It must be a tree of this fork that has
# been configured and built; the default is the merge-train worktree beside this one.
FOREIGN_SRC="${2:-$(dirname "$ROOT")}"
FOREIGN_BUILD="${1:-$FOREIGN_SRC/build}"
SCRATCH="${TMPDIR:-/tmp}/code9-posix-proof.$$"

FILES=(include/ControlServer.h src/core/ControlServer.cpp src/core/ControlServerSocket.cpp)
NEW_TU=src/core/ControlServerWin32.cpp
WINDOWS_ONLY_SYMBOLS='win32\|CreateNamedPipe\|PIPE_REJECT\|listenWin32\|closeWin32'

if [[ ! -f "$FOREIGN_BUILD/compile_commands.json" ]]; then
	echo "error: no $FOREIGN_BUILD/compile_commands.json (pass the built foreign tree as \$1)" >&2
	exit 2
fi
git rev-parse --verify --quiet "$BASE_REV^{commit}" >/dev/null || {
	echo "error: '$BASE_REV' is not a commit in this repo" >&2; exit 2; }

mkdir -p "$SCRATCH"
trap 'rm -rf "$SCRATCH"' EXIT

# --- the fork's real flags, re-pointed at THIS worktree ------------------------
python3 - "$ROOT" "$FOREIGN_SRC" "$FOREIGN_BUILD" > "$SCRATCH/flags.sh" <<'PY'
import json, shlex, sys
root, foreign, foreign_build = sys.argv[1:4]
db = json.load(open(foreign_build + "/compile_commands.json"))
entry = next(e for e in db
             if e["file"].endswith("ControlServer.cpp") and "Socket" not in e["file"])
parts, out, i = shlex.split(entry["command"]), [], 0
while i < len(parts):
    p = parts[i]
    if p in ("-o", "-c"):
        i += 2
        continue
    # Source-tree paths point at this worktree. BUILD-tree paths (generated headers)
    # stay where they are: they only exist in a tree that has been configured.
    if (p.startswith("-I") or p.startswith("-isystem")) and foreign in p \
            and not p.startswith("-I" + foreign_build) and not p.startswith("-isystem" + foreign_build):
        p = p.replace(foreign, root)
    out.append(p)
    i += 1
print("FLAGS=(" + " ".join(shlex.quote(x) for x in out[1:]) + ")")
PY
# shellcheck disable=SC1090
. "$SCRATCH/flags.sh"

status=0

# --- 1. the three TUs compile --------------------------------------------------
echo "== compile check (borrowed flags, -fsyntax-only, -Werror is in the flags) =="
for f in "${FILES[@]}" "$NEW_TU"; do
	if c++ "${FLAGS[@]}" -fsyntax-only "$ROOT/$f" 2> "$SCRATCH/compile.err"; then
		echo "COMPILES   $f (EXIT=0)"
	else
		echo "FAILED     $f"
		sed -n '1,40p' "$SCRATCH/compile.err"
		status=1
	fi
done

# --- 2. the POSIX token streams are unchanged ----------------------------------
echo
echo "== POSIX token stream vs $BASE_REV (-E, line markers dropped) =="
for f in "${FILES[@]}"; do
	mkdir -p "$SCRATCH/old/$(dirname "$f")"
	git show "$BASE_REV:$f" > "$SCRATCH/old/$f" || { echo "cannot extract $f" >&2; status=1; continue; }
	( cd "$SCRATCH/old" && c++ "${FLAGS[@]}" -E "$f" 2>/dev/null ) |
		grep -v '^#' | grep -v '^[[:space:]]*$' > "$SCRATCH/old.pre"
	( cd "$ROOT" && c++ "${FLAGS[@]}" -E "$f" 2>/dev/null ) |
		grep -v '^#' | grep -v '^[[:space:]]*$' > "$SCRATCH/new.pre"
	if diff -q "$SCRATCH/old.pre" "$SCRATCH/new.pre" >/dev/null; then
		echo "IDENTICAL  $f ($(wc -l < "$SCRATCH/new.pre") token lines)"
	else
		echo "DIFFERS    $f"
		diff "$SCRATCH/old.pre" "$SCRATCH/new.pre" | head -40
		status=1
	fi
done

# --- 2b. ControlServer.cpp is the same FILE too --------------------------------
if git diff --quiet "$BASE_REV" -- src/core/ControlServer.cpp; then
	echo "SAME-FILE  src/core/ControlServer.cpp (bit-identical to $BASE_REV)"
else
	echo "CHANGED    src/core/ControlServer.cpp (the wire half must be untouched)"
	status=1
fi

# --- 3. the Windows TU is guarded ---------------------------------------------
echo
echo "== the Windows TU on POSIX =="
( cd "$ROOT" && c++ "${FLAGS[@]}" -E "$NEW_TU" 2>/dev/null ) |
	grep -v '^#' | grep -v '^[[:space:]]*$' > "$SCRATCH/win32.pre"
hits=$(grep -ci "$WINDOWS_ONLY_SYMBOLS" "$SCRATCH/win32.pre")
if [[ "$hits" -eq 0 ]]; then
	echo "GUARDED    $NEW_TU (0 of its own symbols survive on POSIX)"
else
	echo "LEAKS      $NEW_TU ($hits lines of Windows-only code survive on POSIX)"
	grep -in "$WINDOWS_ONLY_SYMBOLS" "$SCRATCH/win32.pre" | head -10
	status=1
fi

echo
echo "POSIX-PROOF EXIT=$status"
exit $status
