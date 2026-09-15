#!/usr/bin/env bash
# merge-w4-finish.sh <label> <branch> <description>
# Steps 3-6 of a merge-w4.sh run, for a merge that is ALREADY in progress:
# resolve -> marker/dup sweep -> stage -> no-silent-loss on the staged tree ->
# commit with the union-count table -> after column.
set -u
L=".merge-logs-030w4"
LABEL="$1"; BRANCH="$2"; DESC="$3"
# GUARD: this script finishes a merge that is IN PROGRESS. Without MERGE_HEAD the
# resolved files are just the pre-merge versions and the sweep would "pass".
if [ ! -f .git/MERGE_HEAD ] && [ ! -f "$(git rev-parse --git-dir)/MERGE_HEAD" ]; then
  echo "FINISH REFUSED: no merge in progress (MERGE_HEAD absent) - run merge-w4.sh"
  exit 3
fi
read -r MB < <(sed -n 1p "$L/stages-$LABEL.txt")
read -r OURS < <(sed -n 2p "$L/stages-$LABEL.txt")
read -r THEIRS < <(sed -n 3p "$L/stages-$LABEL.txt")
CONF=$(wc -l < "$L/conflicted-$LABEL.txt")
echo "### finish $BRANCH  ($CONF conflicted file(s), base $(git rev-parse --short=9 "$MB") ours $(git rev-parse --short=9 "$OURS") theirs $(git rev-parse --short=9 "$THEIRS"))"

python3 "$L/w4-counts.py" "$L/conflicted-$LABEL.txt" "$OURS" "$THEIRS" > "$L/counts-$LABEL-before.txt" 2>&1

UNRES=$(git diff --name-only --diff-filter=U | wc -l)
if [ "$UNRES" -eq 0 ]; then
  echo "--- union resolution: SKIPPED (the conflicted set is already resolved and staged) ---"
  REXIT=0
  grep -E '^(==|   result|   CLAUSE|   [a-z-]+ union)' "$L/resolve-$LABEL.log" 2>/dev/null | sed -e 's/^\(.\{200\}\).*/\1.../'
else
  echo "--- union resolution ($UNRES unmerged path(s)) ---"
  python3 "$L/union-resolve.py" "$LABEL" > "$L/resolve-$LABEL.log" 2>&1
  REXIT=$?
  echo "resolver EXIT=$REXIT"
  grep -E '^(==|   result|   CLAUSE|   !!|   [a-z-]+ union|     (reason|added|dropped)|MANUAL|RE-MEASURE)' "$L/resolve-$LABEL.log" | sed -e 's/^\(.\{200\}\).*/\1.../'
fi
if [ "$REXIT" -ne 0 ]; then echo "FINISH FAILED (resolver rc=$REXIT)"; exit 4; fi

echo "--- marker + duplicate sweep over the conflicted set ---"
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
[ "$BAD" -eq 0 ] || { echo "FINISH FAILED (markers/dups/newline)"; exit 4; }
echo "  clean: $CONF file(s), 0 markers, 0 duplicate entries in the entry registries, trailing newline ok"

: > "$L/staged-$LABEL.txt"
while IFS= read -r f; do git add -- "$f"; printf '%s\n' "$f" >> "$L/staged-$LABEL.txt"; done < "$L/conflicted-$LABEL.txt"
TREE=$(git write-tree)
echo "staged tree=$TREE"
python3 "$L/verify-merge.py" "$LABEL" "$MB" "$OURS" "$THEIRS" "$TREE" > "$L/noloss-$LABEL.log" 2>&1
VEXIT=$?
cat "$L/noloss-$LABEL.log"
echo "no-silent-loss EXIT=$VEXIT"
[ "$VEXIT" -eq 0 ] || { echo "FINISH FAILED (silent loss) - NOT committed"; exit 6; }

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
[ "$CEXIT" -eq 0 ] || { echo "FINISH FAILED (commit)"; exit 5; }
python3 "$L/w4-counts.py" "$L/conflicted-$LABEL.txt" HEAD > "$L/counts-$LABEL-after.txt" 2>&1
git show --stat --oneline HEAD | head -25
echo "FINISH OK  tip=$(git rev-parse HEAD)"
exit 0
