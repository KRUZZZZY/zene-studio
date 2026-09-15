#!/usr/bin/env bash
# regen-snapshot-w4.sh - regenerate tools/mcp-zene-control/zene_control/commands_snapshot.json
# ONCE, from a live instance of the merged binary, then leave the instance stopped.
#
# The snapshot is DERIVED (lane brief section 5): never hand-edited, never unioned -
# it is regenerated at a merge from a live instance. The regeneration proves itself:
# the file's provenance block names the binary, and its command count is compared
# with the registry's own count from the SAME live instance (control.commands) and
# with the snapshot on disk (a number that must move when eight lanes' command
# groups land).
set -u
L=".merge-logs-030w4"
SOCK="/tmp/zene-w4-snapshot.sock"
BIN="${ZENE_BIN:-}"
if [ -z "$BIN" ]; then
  for cand in "$(pwd)/build/bin/zene" "$(pwd)/build/zene"; do
    [ -x "$cand" ] && { BIN="$cand"; break; }
  done
fi

echo "== snapshot BEFORE"
python3 - <<'PY'
import json
d = json.load(open("tools/mcp-zene-control/zene_control/commands_snapshot.json"))
print("commands:", len(d["commands"]))
print("captured_at:", d.get("captured_at"))
PY

[ -x "$BIN" ] || { echo "FATAL: no binary at $BIN - build first"; exit 3; }
echo "binary: $(ls -l "$BIN" | awk '{print $5" bytes  "$6" "$7" "$8}')"
echo "sha256: $(sha256sum "$BIN" | cut -d' ' -f1)"

rm -f "$SOCK"
echo "== starting $BIN --control-socket $SOCK"
QT_QPA_PLATFORM=offscreen "$BIN" --control-socket "$SOCK" > "$L/instance-snapshot.log" 2>&1 &
PID=$!
echo "PID=$PID"

for i in $(seq 1 120); do
  [ -S "$SOCK" ] && break
  sleep 0.5
done
if [ ! -S "$SOCK" ]; then
  echo "socket never appeared"; tail -20 "$L/instance-snapshot.log"; kill "$PID" 2>/dev/null; exit 3
fi
echo "socket up: $(ls -l "$SOCK")"

echo "== the live registry's own count (control.commands) and version"
python3 - "$SOCK" > "$L/snapshot-live.log" 2>&1 <<'PY'
import json, socket, sys
def call(method, params=None):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(sys.argv[1])
    req = {"jsonrpc": "2.0", "id": 1, "method": method}
    if params is not None:
        req["params"] = params
    s.sendall((json.dumps(req) + "\n").encode())
    buf = b""
    while not buf.endswith(b"\n"):
        chunk = s.recv(65536)
        if not chunk:
            break
        buf += chunk
    s.close()
    return json.loads(buf.decode())
r = call("control.ping")
print("ping:", json.dumps(r.get("result", r))[:200])
r = call("control.commands")
res = r.get("result", r)
cmds = res.get("commands", res) if isinstance(res, dict) else res
print("live control.commands -> %d command(s)" % len(cmds))
v = call("control.version")
print("version:", json.dumps(v.get("result", v))[:200])
PY
cat "$L/snapshot-live.log"

echo "== regenerating the snapshot from the live instance"
python3 tools/mcp-zene-control/snapshot_commands.py --socket "$SOCK" > "$L/snapshot-regen.log" 2>&1
RC=$?
echo "snapshot_commands.py EXIT=$RC"
cat "$L/snapshot-regen.log"

echo "== stopping the instance (exact PID only)"
python3 - "$SOCK" <<'PY' || true
import json, socket, sys
try:
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(sys.argv[1])
    s.sendall((json.dumps({"jsonrpc": "2.0", "id": 1, "method": "control.quit"}) + "\n").encode())
    print("control.quit sent")
except Exception as e:
    print("quit failed:", e)
PY
for i in $(seq 1 40); do kill -0 "$PID" 2>/dev/null || break; sleep 0.5; done
if kill -0 "$PID" 2>/dev/null; then kill "$PID"; fi
wait "$PID" 2>/dev/null
echo "instance stopped"

echo "== snapshot AFTER (the regenerated file)"
python3 - <<'PY'
import json
d = json.load(open("tools/mcp-zene-control/zene_control/commands_snapshot.json"))
print("commands:", len(d["commands"]))
print("captured_at:", d.get("captured_at"))
inst = d.get("instance")
if inst:
    print("instance:", json.dumps(inst)[:400])
PY
exit $RC
