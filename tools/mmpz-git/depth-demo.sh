#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# depth-demo.sh -- the two proofs task #612 asks for, as a rerunnable transcript:
#
#   1. a REAL git three-way merge where side A edits one track and side B edits
#      another: git runs the mmpz-git merge driver, the merge is clean, and both
#      edits are in the result (checked, not assumed);
#   2. the negative control: both sides edit the SAME thing, and the tool
#      reports a musical conflict instead of silently picking a side;
#   3. the silent-loss cases that used to pass as clean merges: one side
#      deletes or renames a track while the other adds a note inside it.
#
# Every command's exit code is printed unpiped (`cmd; echo EXIT=$?`), because a
# pipeline reports the exit code of its last command and would launder a failed
# merge into a green one.
#
#   bash tools/mmpz-git/depth-demo.sh                     # work in /tmp
#   bash tools/mmpz-git/depth-demo.sh /some/scratch/dir   # work elsewhere

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
PY="${PYTHON:-python3}"
TOOL="$HERE/mmpz_git.py"
EDIT="$HERE/demo_edits.py"
CHECK="$HERE/demo_check.py"
SRC="$ROOT/data/projects/shorties/sv-DnB-Startup.mmpz"
WORK="${1:-/tmp/mmpz-git-depth-demo}"

say() { printf '\n\n===== %s =====\n' "$*"; }
run() {
	printf '\n$ %s\n' "$*"
	"$@"
	local rc=$?
	printf '[EXIT=%d]\n' "$rc"
	return $rc
}

[ -f "$SRC" ] || { echo "missing fixture: $SRC" >&2; exit 2; }
rm -rf "$WORK"
mkdir -p "$WORK"
cd "$WORK"

# ------------------------------------------------------------------ setup
say "0. a repo with the mmpz-git drivers installed"
run git init -q -b main .
run git config user.name "mmpz-git depth demo"
run git config user.email "depth@example.invalid"
run git config core.autocrlf false
run "$PY" "$TOOL" install --repo .
run cp "$HERE/gitattributes.sample" .gitattributes
run git add .gitattributes
run git commit -q -m "drivers: .mmpz stored, diffed and merged as XML"
run cp "$SRC" project.mmpz
run git add project.mmpz
run git commit -q -m "import sv-DnB-Startup.mmpz (${SRC##*/})"

# ------------------------------------------------- 1 different tracks
say "1. PROOF: side A edits track 'Bass', side B edits track 'Kick'"
run git switch -qc feat/bass
run "$PY" "$EDIT" add-note project.mmpz --track Bass --pattern I --key 60 --pos 48
run git commit -qam "bass: add C4 at pos 48"
run git switch -q main
run git switch -qc feat/drums
run "$PY" "$EDIT" add-note project.mmpz --track Kick --pattern Kick --key 36 --pos 96
run git commit -qam "drums: add a kick at pos 96"
run git switch -q feat/bass
run git merge --no-edit feat/drums
MERGE_RC=$?
printf '\n--> git merge exit=%d (0 = merged clean, both edits kept)\n' "$MERGE_RC"
run git status --short
printf '\n--> the merged work-tree file contains BOTH edits:\n'
run "$PY" "$CHECK" project.mmpz --has-note 60:48 --has-note 36:96 --notes 370

# --------------------------------------------- 2 same thing -> conflict
say "2. NEGATIVE CONTROL: both sides change the SAME note's velocity"
run git switch -q main
run git switch -qc feat/vol-ours
run "$PY" "$EDIT" set-note-vol project.mmpz --track Bass --pattern I \
	--pos 0 --key 30 --vol 40
run git commit -qam "bass note vel 40 (ours)"
run git switch -q main
run git switch -qc feat/vol-theirs
run "$PY" "$EDIT" set-note-vol project.mmpz --track Bass --pattern I \
	--pos 0 --key 30 --vol 90
run git commit -qam "bass note vel 90 (theirs)"
run git switch -q feat/vol-ours
run git merge --no-edit feat/vol-theirs
CONFLICT_RC=$?
printf '\n--> git merge exit=%d (1 = conflict, exactly what we want)\n' "$CONFLICT_RC"
run git status --short
printf '\n--> the producer-facing report (this is the primary output):\n'
"$PY" "$TOOL" conflicts project.mmpz
printf '[EXIT=%d] mmpz-git conflicts project.mmpz\n' "$?"
printf '\n--> and the file is still well-formed XML the DAW can open:\n'
run "$PY" "$TOOL" dump project.mmpz -o /tmp/depth-demo-conflicted.xml
run "$PY" "$TOOL" info project.mmpz
run git merge --abort

# --------------------------------- 3 the silent-loss class (now a conflict)
say "3. SILENT-LOSS CLASS: ours deletes a track, theirs adds a note inside it"
run git switch -q main
run git switch -qc feat/delete-kick
run "$PY" "$EDIT" remove-track project.mmpz --track Kick
run git commit -qam "remove the Kick track"
run git switch -q main
run git switch -qc feat/edit-kick
run "$PY" "$EDIT" add-note project.mmpz --track Kick --pattern Kick --key 36 --pos 96
run git commit -qam "kick: add a note"
run git switch -q feat/delete-kick
run git merge --no-edit feat/edit-kick
DEEP_RC=$?
printf '\n--> git merge exit=%d (before the deepening: 0, with theirs note gone)\n' "$DEEP_RC"
printf '\n--> theirs note must still be there:\n'
run "$PY" "$CHECK" project.mmpz --has-note 36:96
printf '\n--> and the conflict names the track in musical terms:\n'
"$PY" "$TOOL" conflicts project.mmpz

say "summary"
printf 'different-track merge exit : %d (want 0)\n' "$MERGE_RC"
printf 'same-note merge exit        : %d (want 1)\n' "$CONFLICT_RC"
printf 'delete-vs-nested-edit exit  : %d (want 1)\n' "$DEEP_RC"
printf 'work dir                    : %s\n' "$WORK"
[ "$MERGE_RC" -eq 0 ] && [ "$CONFLICT_RC" -eq 1 ] && [ "$DEEP_RC" -eq 1 ] \
	&& { echo "ALL THREE BEHAVE AS SPECIFIED"; exit 0; }
echo "SOMETHING DID NOT BEHAVE AS SPECIFIED"
exit 1
