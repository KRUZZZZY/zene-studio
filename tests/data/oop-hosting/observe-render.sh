#!/usr/bin/env bash
# observe-render.sh - render a project and report, from /proc (never from a
# command line match), whether a RemoteZynAddSubFx client process exists while
# the render runs.
#
# Usage: bash observe-render.sh <worktree> <project> <out.wav> <log> <label>
set -u

WORKTREE="$1"
PROJECT="$2"
OUT="$3"
LOG="$4"
LABEL="$5"

export LMMS_PLUGIN_DIR="$WORKTREE/build/plugins"
export QT_QPA_PLATFORM=offscreen

cd "$WORKTREE" || exit 1

# Wave R renamed the built binary from lmms to zene; prefer it and keep the
# pre-rename name as a fallback so this script runs against either build tree.
RENDERER="$WORKTREE/build/zene"
[ -x "$RENDERER" ] || RENDERER="$WORKTREE/build/lmms"

"$RENDERER" render "$PROJECT" -o "$OUT" -f wav -a > "$LOG" 2>&1 &
HOST=$!
echo "[$LABEL] host pid=$HOST project=$PROJECT"

SAMPLE=0
while kill -0 "$HOST" 2>/dev/null; do
	SAMPLE=$((SAMPLE + 1))
	found=0
	for d in /proc/[0-9]*; do
		exe=$(readlink "$d/exe" 2>/dev/null) || continue
		case "$exe" in
			*RemoteZynAddSubFx)
				pid="${d#/proc/}"
				ppid=$(awk '/^PPid:/{print $2}' "$d/status" 2>/dev/null)
				if [ "$ppid" = "$HOST" ]; then
					echo "[$LABEL] sample $SAMPLE: client pid=$pid ppid=$ppid (child of the render host)"
					found=1
				fi
				;;
		esac
	done
	[ "$found" = 0 ] && echo "[$LABEL] sample $SAMPLE: no client process"
	sleep 0.4
done

wait "$HOST"
RC=$?
echo "[$LABEL] render host exit=$RC"
grep -c "Remote plugin crashed" "$LOG" 2>/dev/null | sed "s/^/[$LABEL] 'Remote plugin crashed' lines: /"
ls -l "$OUT" | sed "s/^/[$LABEL] output: /"
exit "$RC"
