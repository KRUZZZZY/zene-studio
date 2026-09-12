#!/usr/bin/env bash
# Run the shipped binary's real instrument-window path under Xvfb, once, and
# report whether the process survives it.
#
# Usage: run-app-instrwindow.sh <tag>
#   e.g. run-app-instrwindow.sh prefix   # binary built from the pre-fix source
#        run-app-instrwindow.sh postfix  # binary built with the guard
set -u
WT=/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-pa-instrview
D=:96
OUT="$WT/logs/gui-click"
TAG="${1:-run}"
X="${2:-145}"
Y="${3:-215}"

ps -eo pid,args | grep "[v]st3-instrument.mmp" | awk '{print $1}' | xargs -r kill 2>/dev/null
sleep 2

# a fresh offscreen X server for this run (no pkill: another lane may own :96)
if ! DISPLAY=$D xdpyinfo >/dev/null 2>&1; then
	Xvfb $D -screen 0 1280x1024x24 > "$OUT/xvfb-$TAG.log" 2>&1 &
	sleep 3
fi
DISPLAY=$D xdpyinfo >/dev/null 2>&1 || { echo "FAIL: no X server on $D"; exit 2; }

cd "$WT/build" || exit 2
DISPLAY=$D QT_QPA_PLATFORM=xcb ./lmms ../logs/gui-click/vst3-instrument.mmp \
	-c ../logs/gui-click/zene-instrview.xml > "$OUT/app-$TAG.log" 2>&1 &
APP=$!
echo "app pid=$APP  log=$OUT/app-$TAG.log"

WIN=""
for i in $(seq 1 40); do
	WIN=$(DISPLAY=$D xdotool search --name "LMMS 0\.1" 2>/dev/null | head -1)
	[ -n "$WIN" ] && break
	sleep 1
done
echo "main window=$WIN after ${i}s"
[ -z "$WIN" ] && { echo "FAIL: no main window"; exit 2; }

sleep 6
DISPLAY=$D xdotool key --clearmodifiers Escape 2>/dev/null   # dismiss the start-up Settings dialog
sleep 2

DISPLAY=$D import -window "$WIN" "$OUT/$TAG-1-before.png"
echo "title before: $(DISPLAY=$D xdotool getwindowname "$WIN")"
DISPLAY=$D xdotool mousemove "$X" "$Y" click 1
sleep 5

if kill -0 "$APP" 2>/dev/null; then
	echo "ALIVE after clicking the VST3 instrument track's button: yes (pid $APP)"
else
	wait "$APP"; echo "ALIVE after clicking the VST3 instrument track's button: NO - exit=$?"
fi
DISPLAY=$D import -window "$WIN" "$OUT/$TAG-2-after.png" 2>/dev/null
echo "title after: $(DISPLAY=$D xdotool getwindowname "$WIN" 2>/dev/null)"
echo "--- app log (stderr) ---"
cat "$OUT/app-$TAG.log"
