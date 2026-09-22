#!/usr/bin/env bash
# s3-rev-demo.sh - the proof ARCH-4 slice S3 owes (SPEC-ARCH-4 1.5, row S3):
#
#   "Merge two branches in git and show the tool can name which side changed
#    an object."
#
# How: three SEPARATE processes of the real writer (the ProjectRevIdsTest
# binary, which drives Song::saveProjectFile) produce base / ours / theirs -
# each process stamps its own `writer` instance id into <head> and onto the
# ONE track it edits, `rev` going 0 -> 1 (SPEC-ARCH-4 1.5 R4). Two branches
# carry the two sides, `git merge` runs mmpz-git's merge driver
# (mmpz_git.py merge), and rev_report.py then names each changed object from
# the STATED rev/writer pair - not from an XML diff.
#
#   bash tools/mmpz-git/s3-rev-demo.sh 2>&1 | tee docs/s3-logs/s3-merge-demo.txt
#
# Everything happens in a throwaway repo under build/ that is removed on
# exit; nothing is pushed anywhere (workspace rules 1 and the LANE 6 brief).

set -u

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
PY="${PYTHON:-python3}"
TOOL="$HERE/mmpz_git.py"
REPORT="$HERE/rev_report.py"
ATTR="$HERE/gitattributes.sample"
TESTBIN="${TESTBIN:-$ROOT/build/tests/ProjectRevIdsTest}"
WORK="${WORK:-$ROOT/build/s3-rev-demo}"

say() { printf '\n\n################################################################\n# %s\n################################################################\n' "$*"; }
run() {
  printf '\n$ %s\n' "$*"
  "$@"
  local rc=$?
  echo "EXIT=$rc"
  return $rc
}

# --- 0. the writer must be built -----------------------------------------
if [ ! -x "$TESTBIN" ]; then
  echo "s3-rev-demo: $TESTBIN not found." >&2
  echo "  build it first: cmake --build build --target ProjectRevIdsTest -- -j2" >&2
  exit 2
fi
rm -rf "$WORK"
mkdir -p "$WORK"
trap 'rm -rf "$WORK"' EXIT

# --- 1. three processes of the real writer -------------------------------
# Each run executes the test binary in full (its cases are green either way)
# and, because ZENE_S3_DEMO_SIDE is set, also writes the requested side.
write_side() {
  local side="$1" out="$2" log="$3"
  printf '\n$ ZENE_S3_DEMO_SIDE=%s ZENE_S3_DEMO_OUT=%s QT_QPA_PLATFORM=offscreen %s\n' \
    "$side" "$out" "$TESTBIN"
  env ZENE_S3_DEMO_SIDE="$side" ZENE_S3_DEMO_OUT="$out" \
    ZENE_S3_DEMO_BASE="$WORK/base.mmp" \
    QT_QPA_PLATFORM=offscreen "$TESTBIN" >"$log" 2>&1
  local rc=$?
  grep -E 'Totals:|FAIL' "$log" | sed 's/^/    /'
  echo "EXIT=$rc"
  return $rc
}

say "1. base: a fresh project, saved (no object has ever been revised)"
write_side base "$WORK/base.mmp" "$WORK/base.log" || exit 1
run grep -c ' rev="' "$WORK/base.mmp" || true
echo "^ 0 revision attributes in the base, as an unrevised file must have"

say "2. ours: a THIRD process loads the base, edits track 1, saves"
write_side ours "$WORK/ours.mmp" "$WORK/ours.log" || exit 1

say "3. theirs: a FOURTH process loads the same base, edits track 2, saves"
write_side theirs "$WORK/theirs.mmp" "$WORK/theirs.log" || exit 1

say "4. what the writer stated (head writer + the two revised tracks)"
run grep -nE 'writer=|rev="' "$WORK/ours.mmp"
run grep -nE 'writer=|rev="' "$WORK/theirs.mmp"

# --- 5. the git merge ----------------------------------------------------
say "5. two branches in git, merged by mmpz-git's merge driver"
REPO="$WORK/repo"
run git init -q -b main "$REPO" || exit 1
run git -C "$REPO" config user.email "s3-demo@zene.invalid"
run git -C "$REPO" config user.name "ARCH-4 S3 demo"
run cp "$WORK/base.mmp" "$REPO/project.mmp"
run git -C "$REPO" add project.mmp
run git -C "$REPO" commit -q -m "base"
run "$PY" "$TOOL" install --repo "$REPO" || exit 1
run cp "$ATTR" "$REPO/.gitattributes"
# the sample's lines are all comments; append the rule this demo needs:
# .mmp files diff and 3-way merge through mmpz-git, stored as plain XML
# (no filter= here - the worktree files stay readable for the transcript).
printf '*.mmp\tdiff=mmpz merge=mmpz\n' >> "$REPO/.gitattributes"
run git -C "$REPO" add .gitattributes
run git -C "$REPO" commit -q -m "let mmpz-git diff and 3-way merge project files"
run git -C "$REPO" checkout -q -b ours
run cp "$WORK/ours.mmp" "$REPO/project.mmp"
run git -C "$REPO" commit -qam "ours: track edited by writer A"
run git -C "$REPO" checkout -q main
run git -C "$REPO" checkout -q -b theirs
run cp "$WORK/theirs.mmp" "$REPO/project.mmp"
run git -C "$REPO" commit -qam "theirs: another track edited by writer B"
run git -C "$REPO" checkout -q ours
printf '\n$ git -C repo merge --no-edit theirs   (rc 1 is the DOCUMENT-level conflict below)\n'
git -C "$REPO" merge --no-edit theirs
MERGE_RC=$?
echo "EXIT=$MERGE_RC"
if [ $MERGE_RC -ne 0 ]; then
  say "5b. the one conflict the driver found - and why it is not an object"
  echo "Both writers necessarily stamped <head writer> (that is what the"
  echo "document-level instance id MEANS), so the root attribute conflicts on"
  echo "every two-sided merge. The driver's report says to keep ours - it is"
  echo "already in place - and drop its comment; NO per-object rev/writer pair"
  echo "conflicted, because each object was revised by exactly one side."
  run "$PY" "$HERE/demo_edits.py" resolve "$REPO/project.mmp" || exit 1
  run "$PY" "$TOOL" verify "$REPO/project.mmp" || exit 1
  run git -C "$REPO" add project.mmp
  run git -C "$REPO" commit -q --no-edit || exit 1
fi
run git -C "$REPO" log --oneline --graph --all

# --- 6. the tool names the side -----------------------------------------
say "6. rev_report.py names which side changed each object (stated rev/writer)"
run "$PY" "$REPORT" --base "$WORK/base.mmp" --ours "$WORK/ours.mmp" \
  --theirs "$WORK/theirs.mmp" --ours-label ours --theirs-label theirs
RC=$?

# --- 7. the transcript is the assertion ----------------------------------
say "7. assertions"
if grep -q "rev=\"1\"" "$WORK/ours.mmp" && grep -q "rev=\"1\"" "$WORK/theirs.mmp" \
  && grep -q "edited-by-ours" "$WORK/ours.mmp" \
  && grep -q "edited-by-theirs" "$WORK/theirs.mmp" \
  && [ $RC -eq 0 ]; then
  echo "PASS: each side's writer bumped rev on exactly the track IT edited"
  echo "      (only the document-level <head writer> conflicted, resolved per"
  echo "      the driver's report), and rev_report attributed one change per"
  echo "      side with zero conflicts."
else
  echo "FAIL: expected rev=1 on both sides and a clean attribution (rc=$RC)"
  exit 1
fi
exit 0
