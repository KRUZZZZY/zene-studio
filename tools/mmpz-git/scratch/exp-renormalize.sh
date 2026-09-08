#!/usr/bin/env bash
# Controlled experiment: does merge.renormalize make git run the clean filter
# on the merge driver's output?
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
PY="${PYTHON:-python3}"
TOOL="$HERE/mmpz_git.py"
EDIT="$HERE/demo_edits.py"
SRC="$ROOT/data/projects/shorties/sv-DnB-Startup.mmpz"

for RENORM in false true; do
  BED=/tmp/mrexp-$RENORM
  rm -rf "$BED"; mkdir -p "$BED"; cd "$BED"
  git init -q -b main .
  git config user.name t; git config user.email t@example.invalid
  git config merge.renormalize "$RENORM"
  "$PY" "$TOOL" install --repo . >/dev/null
  cp "$HERE/gitattributes.sample" .gitattributes
  cp "$SRC" project.mmpz
  git add -A >/dev/null 2>&1; git commit -qm base
  git switch -qc a
  "$PY" "$EDIT" add-note project.mmpz --track Kick --pattern Kick --key 57 --pos 96
  git commit -qam a
  git switch -q main; git switch -qc b
  "$PY" "$EDIT" add-note project.mmpz --track Bass --pattern I --key 43 --pos 384
  git commit -qam b
  git switch -q a
  echo "===== merge.renormalize=$RENORM ====="
  git merge --no-edit b 2>&1 | sed 's/^/  merge: /'
  echo "  status: $(git status --short | tr '\n' ' ')"
  echo "  blob : $(git cat-file blob HEAD:project.mmpz | head -c 5 | xxd -p)"
  echo "  worktree: $(head -c 5 project.mmpz | xxd -p)  size=$(wc -c < project.mmpz)"
  echo "  blob size: $(git cat-file -s HEAD:project.mmpz)"
  echo "  git diff --stat: $(git diff --stat | tail -1)"
done
