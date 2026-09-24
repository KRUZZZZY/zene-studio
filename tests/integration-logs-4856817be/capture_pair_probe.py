#!/usr/bin/env python3
"""DEFECT-D4b capture-pair probe.

The certification sweep compared save1/save2 with a provenance-stripping
normaliser and never kept the BYTES, so the delta beyond <z:provenance> could not
be read.  This driver replays a sweep window's exact command sequence up to the
failing checkpoint, then does `project.save` -> copy bytes -> `project.save` ->
copy bytes and keeps BOTH files (and the raw diff) under docs/d4b-logs/.

Launch recipe is the repo's own (tests/control_socket_harness.py:20-22):
QT_QPA_PLATFORM=offscreen, a --config whose <audioengine audiodev="Dummy (no
sound output)"/> is EXACT, HOME/XDG_* and cwd in a fresh temp tree,
LMMS_PLUGIN_DIR pointing at the built plugins.

Usage: python3 capture_pair_probe.py <lmms-binary> <plugin-dir> <seed> <steps-file> <workdir>
"""
import difflib
import json
import os
import pathlib
import re
import shutil
import socket
import subprocess
import sys
import time

BIN = pathlib.Path(sys.argv[1])
PLUGIN = pathlib.Path(sys.argv[2])
SEED = int(sys.argv[3])
STEPS = sys.argv[4] if len(sys.argv) > 4 else "1-20"
RUN = pathlib.Path(sys.argv[5])
OPEN_FIRST = sys.argv[6] if len(sys.argv) > 6 else None
if RUN.exists():
    shutil.rmtree(RUN)
for sub in ("workspace", "home", "config", "data"):
    (RUN / sub).mkdir(parents=True, exist_ok=True)

CONFIG = """<?xml version="1.0"?>
<!DOCTYPE lmms-config-file>
<lmmsconfig version="0.2.0-alpha" configversion="3">
  <app configured="1"/>
  <audioengine audiodev="Dummy (no sound output)"/>
  <paths workingdir="%s"/>
</lmmsconfig>
""" % str(RUN / "workspace")


class Client:
    def __init__(self, path):
        self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.s.settimeout(30)
        self.s.connect(path)
        self.buf = b""

    def call(self, i, cmd, args=None, timeout=30):
        req = {"id": i, "cmd": cmd, "args": args or {}, "proto": 1}
        line = json.dumps(req, separators=(",", ":")) + "\n"
        self.s.sendall(line.encode())
        end = time.time() + timeout
        while b"\n" not in self.buf:
            left = end - time.time()
            if left <= 0:
                raise RuntimeError("timeout " + cmd)
            self.s.settimeout(left)
            c = self.s.recv(65536)
            if not c:
                raise RuntimeError("EOF " + cmd)
            self.buf += c
        raw, self.buf = self.buf.split(b"\n", 1)
        return json.loads(raw.decode("utf-8", "replace"))

    def close(self):
        try:
            self.s.close()
        except OSError:
            pass


cfg = RUN / "config.xml"
cfg.write_text(CONFIG)
env = dict(os.environ)
env.update({
    "QT_QPA_PLATFORM": "offscreen",
    "HOME": str(RUN / "home"),
    "XDG_CONFIG_HOME": str(RUN / "config"),
    "XDG_DATA_HOME": str(RUN / "data"),
    "LMMS_PLUGIN_DIR": str(PLUGIN),
})
proc = subprocess.Popen([str(BIN), "--config", str(cfg), "--control-socket", str(RUN / "control.sock")],
                        cwd=str(RUN), env=env,
                        stdout=open(RUN / "stdout.log", "wb"), stderr=open(RUN / "stderr.log", "wb"))
c = None
try:
    deadline = time.time() + 60
    while time.time() < deadline:
        if (RUN / "control.sock").exists():
            try:
                c = Client(str(RUN / "control.sock"))
                break
            except OSError:
                pass
        time.sleep(0.05)
    assert c is not None, "socket never connectable"
    while time.time() < deadline:
        a = c.call(0, "control.ping", {})
        if a.get("ok") and (a.get("result") or {}).get("engine_ready"):
            break
        time.sleep(0.2)
    i = 1
    c.call(i, "control.set_undo_coalescing", {"window_ms": 0}); i += 1

    lo, _, hi = STEPS.partition("-")
    lo, hi = int(lo), int(hi or lo)
    if SEED:
        steps = json.load(open(RUN.parent / ("seed-%d/steps.json" % SEED))) if (RUN.parent / ("seed-%d/steps.json" % SEED)).exists() else None
    else:
        steps = None
    replay = []
    if steps:
        for r in steps:
            if lo <= r["step"] <= hi:
                replay.append((r["cmd"], r.get("args") or {}))
    log = open(RUN / "replay.log", "w")
    for cmd, args in replay:
        a = c.call(i, cmd, args); i += 1
        log.write("step %s %s %s -> ok=%s\n" % (i, cmd, json.dumps(args)[:120], a.get("ok")))
    log.close()

    save_path = str(RUN / "workspace" / "checkpoint.mmp")
    if OPEN_FIRST:
        import shutil as _sh
        _sh.copyfile(OPEN_FIRST, save_path)
        o = c.call(i, "project.open", {"path": save_path}); i += 1
        print("open fixture ok=%s" % o.get("ok"))
    a = c.call(i, "project.save", {"path": save_path}); i += 1
    first = pathlib.Path(save_path).read_bytes()
    (RUN / "save1.mmp").write_bytes(first)
    b = c.call(i, "project.save", {"path": save_path}); i += 1
    second = pathlib.Path(save_path).read_bytes()
    (RUN / "save2.mmp").write_bytes(second)
    c.call(i, "control.quit", {"save": False}, timeout=30); i += 1
finally:
    if c:
        c.close()
    try:
        proc.wait(timeout=30)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()

prov = re.compile(r"<z:provenance(?:\s[^>]*)?>.*?</z:provenance>", re.S)
n1 = prov.sub("", first.decode("utf-8", "replace"))
n2 = prov.sub("", second.decode("utf-8", "replace"))
report = {
    "seed": SEED,
    "steps": STEPS,
    "save_a_ok": a.get("ok"),
    "save_b_ok": b.get("ok"),
    "raw_equal": first == second,
    "normalized_equal": n1 == n2,
    "len_first": len(first),
    "len_second": len(second),
}
(RUN / "report.json").write_text(json.dumps(report, indent=2, sort_keys=True))
print(json.dumps(report, indent=2, sort_keys=True))
if n1 != n2:
    d = list(difflib.unified_diff(n1.splitlines(), n2.splitlines(), "save1-normalized", "save2-normalized", lineterm=""))
    (RUN / "normalized.diff").write_text("\n".join(d) + "\n")
    print("\n".join(d[:120]))
