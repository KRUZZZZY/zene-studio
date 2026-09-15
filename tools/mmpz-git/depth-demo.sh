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
#   4. large-asset handling: both sides embed a DIFFERENT 8 KB sample in the
#      same track's <audiofileprocessor>; the merged document must keep ours in
#      place, keep theirs out of the file, and preserve both full values in the
#      .mmpz-git-conflicts.json sidecar.
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

# ------------------------------------------- 4 large-asset handling
say "4. LARGE ASSET: both sides embed a DIFFERENT 8 KB sample on the same track"
run git merge --abort
run git switch -q main
BLOB_A="$("$PY" -c 'import base64,sys; sys.stdout.write(base64.b64encode(bytes(range(256))*32).decode())')"
BLOB_B="$("$PY" -c 'import base64,sys; sys.stdout.write(base64.b64encode(bytes(range(255,-1,-1))*32).decode())')"
run git switch -qc feat/sample-ours
run "$PY" "$EDIT" set-track-attr project.mmpz --track Kick --attr sampledata \
	--element audiofileprocessor --value "$BLOB_A"
run git commit -qam "kick: embed sample A (8192 bytes) in the audiofileprocessor"
run git switch -q main
run git switch -qc feat/sample-theirs
run "$PY" "$EDIT" set-track-attr project.mmpz --track Kick --attr sampledata \
	--element audiofileprocessor --value "$BLOB_B"
run git commit -qam "kick: embed sample B (8192 bytes) in the audiofileprocessor"
run git switch -q feat/sample-ours
run git merge --no-edit feat/sample-theirs
SAMPLE_RC=$?
printf '\n--> git merge exit=%d (want 1: one field, two values)\n' "$SAMPLE_RC"
printf '\n--> the report summarises the blobs by hash instead of printing them:\n'
run "$PY" "$TOOL" conflicts project.mmpz
printf '\n--> the merged document is checked, not eyeballed:\n'
PYTHONPATH="$HERE" "$PY" - "$PWD/project.mmpz" "$BLOB_A" "$BLOB_B" <<'PYEOF'
import json, os, re, sys
import mmpz_git as M

merged, blob_a, blob_b = sys.argv[1], sys.argv[2], sys.argv[3]
xml = M.load_any(merged).decode("utf-8")
fails = []


def check(ok, label):
    print("  %s   %s" % ("OK  " if ok else "FAIL", label))
    if not ok:
        fails.append(label)


comments = [c for c in re.findall(r"<!--.*?-->", xml, re.S) if M.CONFLICT_BANNER in c]
check(xml.count(blob_a) == 1,
      "ours' sample is in the file exactly once (the attribute), not repeated in a comment")
check(xml.count(blob_b) == 0, "theirs' sample is not inlined anywhere in the file")
check(len(comments) == 1, "exactly one conflict comment is marked")
check(comments and max(len(c) for c in comments) < 2000,
      "the comment is small: the 10,924-char blob is summarised (len %d)"
      % (max((len(c) for c in comments), default=0)))
check(comments and "chars, sha256=" in comments[0], "the comment carries the hash summary")
sidecar = merged + ".mmpz-git-conflicts.json"
check(os.path.exists(sidecar), "the sidecar is written next to the project (%s)"
      % os.path.basename(sidecar))
data = json.load(open(sidecar)) if os.path.exists(sidecar) else {"conflicts": []}
check(any(r.get("theirs") == blob_b and r.get("ours") == blob_a for r in data["conflicts"]),
      "both FULL values are recoverable from the sidecar")
import mmpz_git
doc = mmpz_git.parse(mmpz_git.load_any(merged))
check(len(mmpz_git.marker_records(doc)) == 1, "the marker data reads back from the merged file")
print("  %s %s" % ("FAIL" if fails else "PASS", "large-asset handling"))
sys.exit(1 if fails else 0)
PYEOF
SAMPLE_CHECK_RC=$?
printf '[EXIT=%d] the large-asset checks above\n' "$SAMPLE_CHECK_RC"

say "summary"
printf 'different-track merge exit : %d (want 0)\n' "$MERGE_RC"
printf 'same-note merge exit        : %d (want 1)\n' "$CONFLICT_RC"
printf 'delete-vs-nested-edit exit  : %d (want 1)\n' "$DEEP_RC"
printf 'embedded-sample merge exit  : %d (want 1)\n' "$SAMPLE_RC"
printf 'large-asset document checks : %d (want 0)\n' "$SAMPLE_CHECK_RC"
printf 'work dir                    : %s\n' "$WORK"
[ "$MERGE_RC" -eq 0 ] && [ "$CONFLICT_RC" -eq 1 ] && [ "$DEEP_RC" -eq 1 ] \
	&& [ "$SAMPLE_RC" -eq 1 ] && [ "$SAMPLE_CHECK_RC" -eq 0 ] \
	&& { echo "ALL FOUR BEHAVE AS SPECIFIED"; exit 0; }
echo "SOMETHING DID NOT BEHAVE AS SPECIFIED"
exit 1
