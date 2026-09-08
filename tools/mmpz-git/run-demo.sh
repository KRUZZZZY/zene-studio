#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# run-demo.sh -- end-to-end proof for the git-friendly .mmpz workflow.
#
# Everything below runs in throwaway repositories under /tmp and
# tools/mmpz-git/testbed/.  Every command is printed before it runs and its
# exit code is reported, so the transcript is the evidence.
#
#   bash tools/mmpz-git/run-demo.sh 2>&1 | tee tools/mmpz-git/demo-output.txt

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
PY="${PYTHON:-python3}"
TOOL="$HERE/mmpz_git.py"
EDIT="$HERE/demo_edits.py"
ATTR="$HERE/gitattributes.sample"
BED="$HERE/testbed"
BASE_REPO=/tmp/mmpz-baseline-repo
SRC="$ROOT/data/projects/shorties/sv-DnB-Startup.mmpz"
LMMS="$ROOT/build/lmms"
QT="env QT_QPA_PLATFORM=offscreen"

say() { printf '\n\n################################################################\n# %s\n################################################################\n' "$*"; }
run() {
  printf '\n$ %s\n' "$*"
  "$@"
  local rc=$?
  printf '[exit %d]\n' "$rc"
  return $rc
}
show() { printf '\n$ %s\n' "$*"; }

# ---------------------------------------------------------------- 0 baseline
say "0. the problem: a .mmpz without this tooling is an opaque binary blob"
rm -rf "$BASE_REPO"; mkdir -p "$BASE_REPO"; cd "$BASE_REPO"
run git init -q -b main .
run git config user.name "baseline demo"
run git config user.email "baseline@example.invalid"
run cp "$SRC" project.mmpz
run git add project.mmpz
run git commit -q -m "import project"
run $PY "$EDIT" set-bpm project.mmpz 128
run git diff --stat
run git diff
run git commit -qam "main: bpm 175 -> 128"
run git switch -qc other
run $PY "$EDIT" add-note project.mmpz --track Bass --pattern I --key 43 --pos 384
run git commit -qam "other: add a bass note"
run git switch -q main
run $PY "$EDIT" add-note project.mmpz --track Kick --pattern Kick --key 36 --pos 96
run git commit -qam "main: add a kick note"
run git merge --no-edit other
run git status --short

# ------------------------------------------------------- 1 install drivers
say "1. install the filter / diff / merge drivers into a real repo"
rm -rf "$BED"; mkdir -p "$BED"; cd "$BED"
run git init -q -b main .
run git config user.name "mmpz-git demo"
run git config user.email "mmpz-git@example.invalid"
run git config core.autocrlf false
run $PY "$TOOL" install --repo . --python /usr/bin/python3
run cp "$ATTR" .gitattributes
show "cat .gitattributes"
cat .gitattributes
run git add .gitattributes
run git commit -q -m "Add .gitattributes: .mmpz stored, diffed and merged as XML"

# ------------------------------------------------------------- 2 round-trip
say "2. import a real project; prove .mmpz -> git -> .mmpz is byte-identical"
run cp "$SRC" project.mmpz
run $PY "$TOOL" info project.mmpz
show "sha256sum project.mmpz            # BEFORE git ever sees it"
sha256sum project.mmpz
run git add project.mmpz
run git commit -q -m "Import sv-DnB-Startup.mmpz (real LMMS demo project)"
show "git cat-file blob :project.mmpz | head -3   # stored form is readable XML"
git cat-file blob :project.mmpz | head -3
show "git cat-file -s :project.mmpz; wc -c project.mmpz"
printf 'stored blob: %s bytes\n' "$(git cat-file -s :project.mmpz)"
printf 'worktree   : %s bytes\n' "$(wc -c < project.mmpz)"
run rm project.mmpz
run git checkout -- project.mmpz
show "sha256sum project.mmpz            # AFTER git checkout (smudge filter ran)"
sha256sum project.mmpz
BEFORE="$(sha256sum "$SRC" | cut -d' ' -f1)"
AFTER="$(sha256sum project.mmpz | cut -d' ' -f1)"
printf '\nsource sha256 : %s\ncheckout sha256: %s\n' "$BEFORE" "$AFTER"
if [ "$BEFORE" = "$AFTER" ]; then
  echo "ROUND-TRIP: byte-identical"
else
  echo "ROUND-TRIP: FAILED"
fi
run file project.mmpz
show "# cross-check our stdlib codec against the real LMMS binary"
run $PY "$TOOL" dump project.mmpz -o /tmp/py_dump.xml
$QT "$LMMS" compress /tmp/py_dump.xml > /tmp/lmms_comp.mmpz 2>/dev/null
$PY "$TOOL" compress /tmp/py_dump.xml -o /tmp/py_comp.mmpz
show "cmp /tmp/lmms_comp.mmpz /tmp/py_comp.mmpz"
cmp /tmp/lmms_comp.mmpz /tmp/py_comp.mmpz && echo "IDENTICAL: real lmms and our helper agree byte-for-byte"

# ---------------------------------------------------------- 3 small diff
say "3. a small edit produces a small, readable diff (not a whole-file rewrite)"
run $PY "$EDIT" set-bpm project.mmpz 128
run git diff --stat
run git diff --numstat
run git diff
run git commit -qam "main: bpm 175 -> 128 (one-line edit)"

# ----------------------------------------------------- 4 canonical order
say "4. canonical ordering: deterministic, idempotent, content-preserving"
run $PY "$TOOL" canonicalize --check project.mmpz
run $PY "$TOOL" canonicalize project.mmpz -o /tmp/canon.mmpz
run $PY "$TOOL" canonicalize --check /tmp/canon.mmpz
show "# canonicalisation is content-preserving: the semantic diff is empty"
run $PY "$TOOL" diff project.mmpz /tmp/canon.mmpz
show "# Qt6 re-saves the same project twice: attribute order churns (hash seed)"
if [ -x "$HERE/scratch/qtsave2" ]; then
  run "$HERE/scratch/qtsave2" project.mmpz /tmp/qtA.mmpz
  run "$HERE/scratch/qtsave2" project.mmpz /tmp/qtB.mmpz
  show "cmp /tmp/qtA.mmpz /tmp/qtB.mmpz"
  cmp /tmp/qtA.mmpz /tmp/qtB.mmpz; echo "cmp exit=$?"
  show "diff <(dump qtA) <(dump qtB) | wc -l"
  diff <($PY "$TOOL" dump /tmp/qtA.mmpz) <($PY "$TOOL" dump /tmp/qtB.mmpz) | wc -l
  show "# after canonicalisation the two Qt6 saves are byte-identical"
  $PY "$TOOL" canonicalize /tmp/qtA.mmpz -o /tmp/qtA.canon.mmpz
  $PY "$TOOL" canonicalize /tmp/qtB.mmpz -o /tmp/qtB.canon.mmpz
  cmp /tmp/qtA.canon.mmpz /tmp/qtB.canon.mmpz && echo "IDENTICAL after canonicalisation"
else
  echo "(scratch/qtsave2 not built; see tools/mmpz-git/scratch/qtsave2.cpp)"
fi

# --------------------------------------------------------- 5 semantic diff
say "5. XML-aware diff: an operation list, not text lines"
run cp project.mmpz /tmp/varA.mmpz
run cp project.mmpz /tmp/varB.mmpz
run $PY "$EDIT" add-note /tmp/varB.mmpz --track Bass --pattern I --key 43 --pos 384
run $PY "$EDIT" set-note-vol /tmp/varB.mmpz --track Bass --pattern I --pos 0 --key 30 --vol 80
run $PY "$EDIT" move-note /tmp/varB.mmpz --track Bass --pattern I --pos 192 --key 29 --to-pos 240
run $PY "$EDIT" remove-note /tmp/varB.mmpz --track Bass --pattern I --pos 288 --key 28
run $PY "$TOOL" diff /tmp/varA.mmpz /tmp/varB.mmpz
show "# the same two files as a raw text diff"
run git diff --no-index --stat /tmp/varA.mmpz /tmp/varB.mmpz

# -------------------------------------------------------- 6 merge: disjoint
say "6a. two branches editing DIFFERENT tracks merge cleanly"
run git switch -qc feat/drums
run $PY "$EDIT" add-note project.mmpz --track Kick --pattern Kick --key 57 --pos 96
run git commit -qam "drums: add a kick note"
run git switch -q main
run git switch -qc feat/bass
run $PY "$EDIT" add-note project.mmpz --track Bass --pattern I --key 43 --pos 384
run git commit -qam "bass: add a bass note"
run git switch -q feat/drums
run git merge --no-edit feat/bass
run git status --short
run $PY "$TOOL" info project.mmpz
show "# both edits survived: semantic diff of main -> merge result"
git show main:project.mmpz > /tmp/merge_base.mmpz
git show feat/drums:project.mmpz > /tmp/merge_result.mmpz
run $PY "$TOOL" diff /tmp/merge_base.mmpz /tmp/merge_result.mmpz
run git log --oneline --graph --all

# ------------------------------------------------------- 7 merge: conflict
say "6b. conflicting BPM edits produce a human-resolvable conflict"
run git switch -q main
run git switch -qc feat/bpm-ours
run $PY "$EDIT" set-bpm project.mmpz 140
run git commit -qam "bpm: 140 (ours)"
run git switch -q main
run git switch -qc feat/bpm-theirs
run $PY "$EDIT" set-bpm project.mmpz 96
run git commit -qam "bpm: 96 (theirs)"
run git switch -q feat/bpm-ours
run git merge --no-edit feat/bpm-theirs
run git status --short
show "# the conflicted project file is still well-formed XML with a comment"
run $PY "$TOOL" dump project.mmpz -o /tmp/conflicted.xml
show "sed -n '1,15p' /tmp/conflicted.xml   # the whole conflict banner"
sed -n '1,15p' /tmp/conflicted.xml
show "# resolve: keep 96 (a human decision), drop the comment, complete the merge"
run $PY "$EDIT" set-bpm project.mmpz 96
run $PY "$EDIT" resolve project.mmpz
run $PY "$TOOL" verify project.mmpz
run git add project.mmpz
run git commit -q --no-edit
run git status --short
run git log --oneline --graph -8

# -------------------------------------------------- 6c minimal later merges
say "6c. once both sides are canonical, later merges are small"
run git merge --no-edit feat/drums
run git show --stat --format="%h %s" HEAD
run $PY "$TOOL" info project.mmpz

# --------------------------------------------------- 8 the project opens
say "7. the merged project still opens in real LMMS"
run file project.mmpz
run $PY "$TOOL" info project.mmpz
show "QT_QPA_PLATFORM=offscreen $LMMS dump project.mmpz"
$QT "$LMMS" dump project.mmpz > /tmp/merged_dump.xml 2>/dev/null
printf '[dump exit %d]\n' "$?"
sed -n '1,4p' /tmp/merged_dump.xml
show "QT_QPA_PLATFORM=offscreen $LMMS render project.mmpz -o /tmp/merged.wav -f wav"
$QT "$LMMS" render project.mmpz -o /tmp/merged.wav -f wav 2>&1 | tail -3
printf '[render exit %d]\n' "${PIPESTATUS[0]}"
run file /tmp/merged.wav
run ls -la /tmp/merged.wav
show "# the bpm-140-vs-96 conflict is resolved to 96 in the final file"
run $PY "$TOOL" diff /tmp/merge_base.mmpz project.mmpz

# ------------------------------------------------------------- 9 samples
say "8. audio samples: git-lfs, and what it does not cover"
show "git lfs version   # this environment has no git-lfs -> documented, not tested"
git lfs version; echo "[exit $?]"
show "cat .gitattributes   # the lfs guidance sits beside the project rules"
cat .gitattributes

say "done -- repositories left in place for inspection"
printf 'testbed repo : %s\n' "$BED"
printf 'baseline repo: %s\n' "$BASE_REPO"
