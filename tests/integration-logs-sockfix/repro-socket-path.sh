#!/usr/bin/env bash
# The data-loss reproduction, run against ONE build, both before and after the fix.
#
# The report's own command line was:
#
#   $ printf 'IMPORTANT PROJECT DATA\n' > /tmp/socktest/song.mmp
#   $ QT_QPA_PLATFORM=offscreen ./build/zene --control-socket /tmp/socktest/song.mmp
#   $ ls -l /tmp/socktest/song.mmp
#     -rw-rw-r-- 0 bytes  kind=socket
#
# This script runs exactly that, with three changes that make it comparable and
# re-runnable rather than incidental:
#   * HOME/XDG_* and the fixture live under /tmp (a sun_path is 107 bytes: the
#     worktree's own path plus tests/integration-logs-sockfix/ is already 126, so
#     a socket cannot live beside this log). The LOG stays here in the repo -- that
#     is what must survive -- and it records the fixture's mode, size and sha256
#     before and after, which is the evidence;
#   * a --config with the documented headless recipe (dummy audio device,
#     configured=1, a working directory that exists) is passed, so the run cannot
#     be parked in the first-run setup dialog -- the run must be able to *start*,
#     which in the pre-fix case it does: that is why the process still holds the
#     socket when the bound expires;
#   * `timeout` bounds it. PRE-FIX the process starts and KEEPS RUNNING (exit 124):
#     it bound a socket over the project file and said nothing. POST-FIX it exits
#     immediately (exit 1) with the typed refusal on stderr.
#
# Unpiped exit codes throughout: every command's status is captured with $? into
# the caller's log, never through a pipe.
#
# Usage: bash tests/integration-logs-sockfix/repro-socket-path.sh <zene> <label>
set -u

BIN="${1:?usage: repro-socket-path.sh <zene> <label>}"
LABEL="${2:-run}"
FIX="/tmp/sockfix-repro-${UID:-0}"
BOUND=30

rm -rf "$FIX"
mkdir -p "$FIX/config" "$FIX/data" "$FIX/workspace" "$FIX/run"
chmod 700 "$FIX/run"
printf 'IMPORTANT PROJECT DATA\n' > "$FIX/song.mmp"

cat > "$FIX/lmmsrc.xml" <<XML
<?xml version="1.0"?>
<!DOCTYPE lmms-config-file>
<lmmsconfig version="0.2.0-alpha" configversion="3">
  <app configured="1"/>
  <audioengine audiodev="Dummy (no sound output)"/>
  <paths workingdir="$FIX/workspace"/>
</lmmsconfig>
XML

echo "== $LABEL =="
echo "binary      : $BIN"
echo "socket path : $FIX/song.mmp ($(printf %s "$FIX/song.mmp" | wc -c) bytes)"
echo "before      : $(stat -c 'mode=%A size=%s' "$FIX/song.mmp") sha256=$(sha256sum "$FIX/song.mmp" | cut -d' ' -f1)"

timeout "$BOUND" env QT_QPA_PLATFORM=offscreen HOME="$FIX" XDG_CONFIG_HOME="$FIX/config" \
	XDG_DATA_HOME="$FIX/data" XDG_RUNTIME_DIR="$FIX/run" "$BIN" --config "$FIX/lmmsrc.xml" \
	--control-socket "$FIX/song.mmp" > "$FIX/stdout.log" 2> "$FIX/stderr.log"
STATUS=$?
echo "exit        : $STATUS$([ "$STATUS" = 124 ] && echo ' (timeout: the process was still running)')"

if [ -e "$FIX/song.mmp" ]; then
	echo "after       : $(stat -c 'mode=%A size=%s' "$FIX/song.mmp") sha256=$(sha256sum "$FIX/song.mmp" | cut -d' ' -f1)"
	echo "kind        : $(stat -c %F "$FIX/song.mmp")"
else
	echo "after       : the path does not exist any more"
fi
echo "-- stderr --"
cat "$FIX/stderr.log"
echo "-- stdout --"
cat "$FIX/stdout.log"
echo "== end $LABEL =="
