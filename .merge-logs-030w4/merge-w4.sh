#!/usr/bin/env bash
# merge-w4.sh <label> <branch> <one-line description>
#
# One merge of the wave-4 integration train:
#   1. record per-file entry counts on BOTH sides (the before column)
#   2. git merge --no-ff
#   3. resolve every conflicted append-only registry by the recorded UNION rule
#   4. prove no silent loss (verify-merge.py, three-way, on the STAGED tree)
#   5. prove no conflict markers, no duplicate entries, trailing newline intact
#   6. commit, then record the after column
# All exit codes are measured UNPIPED.  A failure aborts the merge, not the train.
set -u
L=".merge-logs-030w4"
LABEL="$1"; BRANCH="$2"; DESC="$3"
OURS=$(git rev-parse HEAD)
THEIRS=$(git rev-parse "$BRANCH")
MB=$(git merge-base HEAD "$BRANCH")
printf '%s\n' "$MB" "$OURS" "$THEIRS" > "$L/stages-$LABEL.txt"
echo "### merge $BRANCH   (base $(git rev-parse --short=9 "$MB") ours $(git rev-parse --short=9 "$OURS") theirs $(git rev-parse --short=9 "$THEIRS"))"

git merge --no-ff -m "merge(030): $BRANCH into release/0.3.0 - $DESC" "$BRANCH" > "$L/automerge-$LABEL.log" 2>&1
MEXIT=$?
echo "git-merge EXIT=$MEXIT"
git diff --name-only --diff-filter=U > "$L/conflicted-$LABEL.txt"
CONF=$(wc -l < "$L/conflicted-$LABEL.txt")
echo "conflicted files: $CONF"
cat "$L/conflicted-$LABEL.txt"

# the union-count table: ours / theirs / merged
python3 "$L/w4-counts.py" "$L/conflicted-$LABEL.txt" "$OURS" "$THEIRS" > "$L/counts-$LABEL-before.txt" 2>&1
echo "before-column counts:"; cat "$L/counts-$LABEL-before.txt"

if [ "$CONF" -eq 0 ]; then
  echo "clean auto-merge (git made the commit)"
  python3 "$L/w4-counts.py" "$L/conflicted-$LABEL.txt" HEAD > "$L/counts-$LABEL-after.txt" 2>&1
  echo "tip=$(git rev-parse HEAD)"; git show --stat --oneline HEAD | head -20
  exit 0
fi

echo "--- union resolution ---"
python3 "$L/union-resolve.py" "$LABEL" > "$L/resolve-$LABEL.log" 2>&1
REXIT=$?
echo "resolver EXIT=$REXIT"
tail -25 "$L/resolve-$LABEL.log" | sed 's/^/    /'
if [ "$REXIT" -ne 0 ]; then echo "MERGE-W4 FAILED (resolver rc=$REXIT) - see $L/resolve-$LABEL.log"; exit 4; fi

echo "--- marker + duplicate sweep over the conflicted set ---"
# A DUPLICATE is only a defect in a file whose lines ARE entries: the three scope
# manifests and the upstream ledger. In a markdown doc or a CMakeLists a repeated
# line (a bullet, a closing paren) is structure, not a lost-set collision.
ENTRY_FILES="tests/fork-sources.txt tests/all-sources.txt tests/tools-sources.txt tests/upstream-modifications.txt"
BAD=0
while IFS= read -r f; do
  if grep -qE '^(<<<<<<<|>>>>>>>|=======$)' "$f"; then echo "  MARKER LEFT: $f"; BAD=1; fi
  case " $ENTRY_FILES " in *" $f "*)
    n=$(grep -vE '^[[:space:]]*(#|$)' "$f" | sort | uniq -d | head -3)
    if [ -n "$n" ]; then echo "  DUPLICATE ENTRY in $f:"; printf '%s\n' "$n" | sed 's/^/    /'; BAD=1; fi
    ;;
  esac
  if [ -s "$f" ] && [ "$(tail -c1 "$f" | od -An -c | tr -d ' ')" != '\n' ]; then echo "  NO TRAILING NEWLINE: $f"; BAD=1; fi
done < "$L/conflicted-$LABEL.txt"
[ "$BAD" -eq 0 ] || { echo "MERGE-W4 FAILED (markers/dups/newline)"; exit 4; }
echo "  clean: $CONF file(s), 0 markers, 0 duplicate entries in the entry registries, trailing newline ok"

: > "$L/staged-$LABEL.txt"
while IFS= read -r f; do git add -- "$f"; printf '%s\n' "$f" >> "$L/staged-$LABEL.txt"; done < "$L/conflicted-$LABEL.txt"
echo "staged:"; cat "$L/staged-$LABEL.txt"

TREE=$(git write-tree)
echo "staged tree=$TREE"
python3 "$L/verify-merge.py" "$LABEL" "$MB" "$OURS" "$THEIRS" "$TREE" > "$L/noloss-$LABEL.log" 2>&1
VEXIT=$?
cat "$L/noloss-$LABEL.log"
echo "no-silent-loss EXIT=$VEXIT"
[ "$VEXIT" -eq 0 ] || { echo "MERGE-W4 FAILED (silent loss) - NOT committed"; exit 6; }

python3 "$L/w4-counts.py" "$L/conflicted-$LABEL.txt" "$TREE" > "$L/counts-$LABEL-merged.txt" 2>&1

git commit -F - > "$L/commit-$LABEL.log" 2>&1 <<EOF
merge(030): $BRANCH into release/0.3.0 - $DESC

Verified union merge of the append-only registries ($CONF conflicted file(s)).
Per-file lines/entries, ours -> theirs -> merged (union proof):

$(cat "$L/counts-$LABEL-before.txt")

-- merged tip --
$(cat "$L/counts-$LABEL-merged.txt")

Union rules applied (see .merge-logs-030w4/union-resolve.py):
  block-union / section-union / entry-union / ledger-union / keep-ours(measurement)
Assertions: 0 conflict markers, 0 duplicate entries, trailing newline intact,
no-added-line-loss from either side (verify-merge.py on the staged tree).
EOF
CEXIT=$?
echo "git-commit EXIT=$CEXIT"
[ "$CEXIT" -eq 0 ] || { echo "MERGE-W4 FAILED (commit)"; exit 5; }
python3 "$L/w4-counts.py" "$L/conflicted-$LABEL.txt" HEAD > "$L/counts-$LABEL-after.txt" 2>&1
echo "--- tip ---"; git show --stat --oneline HEAD | head -30
echo "MERGE-W4 OK  tip=$(git rev-parse HEAD)"
exit 0
