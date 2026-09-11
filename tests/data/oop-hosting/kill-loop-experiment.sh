#!/usr/bin/env bash
# kill-loop-experiment.sh - kill the Zyn remote client repeatedly for the whole
# render and see whether the rendered audio changes.
#
# Usage: bash kill-loop-experiment.sh <worktree> <project> <out.wav> <log>
set -u

WORKTREE="$1"
PROJECT="$2"
OUT="$3"
LOG="$4"

export LMMS_PLUGIN_DIR="$WORKTREE/build/plugins"
export QT_QPA_PLATFORM=offscreen

cd "$WORKTREE" || exit 1

"$WORKTREE/build/lmms" render "$PROJECT" -o "$OUT" -f wav -a > "$LOG" 2>&1 &
HOST=$!
echo "host pid=$HOST"

KILLS=0
SEEN=0
for _ in $(seq 1 6000); do
	if ! kill -0 "$HOST" 2>/dev/null; then break; fi
	# scan /proc/<pid>/exe, never `pgrep -f`: a command line that merely
	# mentions the client name matches too, and that is how a shell becomes
	# 'evidence'.
	for d in /proc/[0-9]*; do
		exe=$(readlink "$d/exe" 2>/dev/null) || continue
		case "$exe" in
			*RemoteZynAddSubFx) ;;
			*) continue ;;
		esac
		p="${d#/proc/}"
		ppid_of=$(awk '/^PPid:/{print $2}' "$d/status" 2>/dev/null)
		if [ "$ppid_of" = "$HOST" ]; then
			SEEN=$((SEEN + 1))
			kill -9 "$p" 2>/dev/null && KILLS=$((KILLS + 1))
			echo "killed client pid=$p ppid=$ppid_of (kill #$KILLS, seen #$SEEN)"
		fi
	done
	sleep 0.02
done

wait "$HOST"
RC=$?
echo "clients seen=$SEEN killed=$KILLS"
echo "render host exit=$RC"
tail -c 200 "$LOG"
echo
grep -c "Remote plugin crashed" "$LOG" || true
ls -l "$OUT"
exit "$RC"
