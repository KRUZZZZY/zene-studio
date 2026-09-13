#!/usr/bin/env python3
"""W7 lane: drive the new export.* commands on a live instance over the socket.

A committed control-surface transcript, the second of the two proof forms the
0.3.0 scope contract allows (the first is the registered ctest). It is written
here as a small standalone client rather than through tests/control_socket_harness.py
so it can also be read as the protocol's shape.

Every call is bounded; a hang is a failure.
"""
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time

BINARY = sys.argv[1]
PROJECT = sys.argv[2]
# A PRIVATE run directory per invocation, not two fixed /tmp names. This file is
# a registered ctest (ControlExportSettings) since the 2026-09-13 coverage
# audit, and two ctest runs on one box - this lane's build beside a sibling
# lane's - must not share a socket path or clobber each other's log; the rule
# WAVE-1-BRIEFS.md records after a shared /tmp name cost a lane a false gate
# result. Every assertion below is unchanged.
RUN_DIR = tempfile.mkdtemp(prefix="zctl-export-")
SOCKET = os.path.join(RUN_DIR, "zene.sock")
LOG = os.path.join(RUN_DIR, "app.log")
WORKSPACE = os.path.join(RUN_DIR, "workspace")
CONFIG = os.path.join(RUN_DIR, "lmmsrc.xml")
os.makedirs(WORKSPACE, exist_ok=True)

# The headless start recipe the other registered transcripts use
# (tests/control_socket_harness.py): `audiodev` must be exactly
# AudioDummy::name(), because on a machine with no sound card the engine
# otherwise answers the startup with a modal "Audio device setup failed" dialog
# and never becomes ready - and a transcript that cannot reach the engine is a
# failure of the test, not of the product.
with open(CONFIG, "w") as handle:
    handle.write(
        '<?xml version="1.0"?>\n'
        '<!DOCTYPE lmms-config-file>\n'
        '<lmmsconfig version="0.2.0-alpha" configversion="3">\n'
        '  <app configured="1"/>\n'
        '  <audioengine audiodev="Dummy (no sound output)"/>\n'
        '  <paths workingdir="%s"/>\n'
        '</lmmsconfig>\n' % WORKSPACE)

env = dict(os.environ, QT_QPA_PLATFORM="offscreen")
env["HOME"] = RUN_DIR
env["XDG_CONFIG_HOME"] = os.path.join(RUN_DIR, "config")
env["XDG_DATA_HOME"] = os.path.join(RUN_DIR, "data")
os.makedirs(env["XDG_CONFIG_HOME"], exist_ok=True)
os.makedirs(env["XDG_DATA_HOME"], exist_ok=True)
proc = subprocess.Popen(
    [BINARY, PROJECT, "--config", CONFIG, "--control-socket", SOCKET],
    stdout=open(LOG, "w"), stderr=subprocess.STDOUT, env=env, cwd=RUN_DIR)

deadline = time.time() + 90
while not os.path.exists(SOCKET) and time.time() < deadline:
    if proc.poll() is not None:
        print("FAIL: instance exited early; log tail:")
        print(open(LOG).read()[-2000:])
        sys.exit(1)
    time.sleep(0.2)
if not os.path.exists(SOCKET):
    proc.kill()
    print("FAIL: the socket never appeared")
    print("app log tail:")
    print(open(LOG).read()[-2000:])
    print("the run directory is kept for inspection: %s" % RUN_DIR)
    sys.exit(1)

sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.settimeout(30)
sock.connect(SOCKET)
stream = sock.makefile("rwb")
next_id = 0
failures = []


def call(method, params=None):
    global next_id
    next_id += 1
    request = {"id": next_id, "cmd": method}
    if params is not None:
        request["args"] = params
    stream.write((json.dumps(request) + "\n").encode())
    stream.flush()
    line = stream.readline()
    if not line:
        raise RuntimeError("no reply to %s" % method)
    return json.loads(line.decode())


def report(label, reply):
    print("%-34s %s" % (label, json.dumps(reply, sort_keys=True)))
    return reply


# 1. wait for readiness (the documented order: connect, poll, then command)
ready = False
reply = {}
deadline = time.time() + 60
while time.time() < deadline:
    reply = call("control.ping")
    if (reply.get("result") or {}).get("engine_ready") is True:
        ready = True
        break
    time.sleep(0.2)
report("control.ping", reply)
if not ready:
    failures.append("control.ping never reported engine_ready")

# 2. the command group is registered
ids = (report("control.commands_list", call("control.commands_list")).get("result") or {})
command_ids = [c["id"] if isinstance(c, dict) else c for c in
               (ids.get("commands") or ids.get("ids") or [])]
for wanted in ("export.get_settings", "export.set_dither", "export.set_src_quality"):
    if wanted not in command_ids:
        failures.append("%s is not registered" % wanted)

# 3. read the defaults: dither off, SRC quality linear
before = report("export.get_settings", call("export.get_settings")).get("result") or {}
if before.get("dither") is not False:
    failures.append("the default dither is not false: %r" % before.get("dither"))
if before.get("src_quality") != "linear":
    failures.append("the default src_quality is not linear: %r" % before.get("src_quality"))

# 4. turn the dither on, and check the A16 record + the undo
changed = report("export.set_dither(true)", call("export.set_dither", {"dither": True})).get("result") or {}
if changed.get("dither") is not True or changed.get("previous") is not False:
    failures.append("export.set_dither did not report the change: %r" % changed)
after = report("export.get_settings", call("export.get_settings")).get("result") or {}
if after.get("dither") is not True:
    failures.append("the dither did not stick")

transactions = call("control.transactions").get("result") or {}
records = transactions.get("transactions") or []
if not records or records[0].get("command") != "export.set_dither":
    failures.append("no transaction recorded for export.set_dither: %r" % records[:1])
else:
    print("%-34s %s" % ("transaction[0]", json.dumps(
        {k: records[0].get(k) for k in ("command", "class", "reversible")}, sort_keys=True)))
    if records[0].get("class") != "true_inverse":
        failures.append("export.set_dither is not classed true_inverse: %r"
                        % records[0].get("class"))
    if records[0].get("reversible") is not True:
        failures.append("export.set_dither is not recorded reversible: %r"
                        % records[0].get("reversible"))

undone = report("control.undo", call("control.undo")).get("result") or {}
if undone.get("undone") is not True:
    failures.append("control.undo did not undo export.set_dither: %r" % undone)
restored = report("export.get_settings", call("export.get_settings")).get("result") or {}
if restored.get("dither") is not False:
    failures.append("the undo did not restore the previous dither: %r" % restored)

# 5. the SRC quality, its wire names, and the typed refusal
quality = report("export.set_src_quality(sinc_best)",
                 call("export.set_src_quality", {"src_quality": "sinc_best"})).get("result") or {}
if quality.get("src_quality") != "sinc_best" or quality.get("previous") != "linear":
    failures.append("export.set_src_quality did not report the change: %r" % quality)

bad = report("export.set_src_quality(bogus)",
             call("export.set_src_quality", {"src_quality": "bogus"}))
if bad.get("error", {}).get("kind") != "invalid_args":
    failures.append("a bad src_quality did not answer invalid_args: %r" % bad)

report("ai.accept(unknown command)", call("export.no_such_verb"))
report("control.quit", call("control.quit"))

try:
    proc.wait(timeout=60)
except subprocess.TimeoutExpired:
    failures.append("the instance did not exit after control.quit")
    proc.kill()
print()
if failures:
    print("FAIL:")
    for f in failures:
        print("  -", f)
    # Kept on failure: the app log in it is the evidence of what happened.
    print("the run directory is kept for inspection: %s" % RUN_DIR)
    sys.exit(1)
print("PASS: export.* is registered, answers, reverses through control.undo, and refuses typed")
shutil.rmtree(RUN_DIR, ignore_errors=True)
sys.exit(0)
