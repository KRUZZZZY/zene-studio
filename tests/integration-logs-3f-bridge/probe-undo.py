#!/usr/bin/env python3
"""Scratch probe: what does `control.undo` do to a live instance in this tree?

test_11 of the bridge's e2e suite reports `disconnected` after calling
`zene_control_undo` (the bridge's generated tool for `control.undo`). This probe
drives the DAW directly with the harness's own RawDawClient, so the bridge is out
of the picture, and reports: the raw reply, whether the process is still alive,
its exit code, and the tail of the instance log.

    ZENE_CONTROL_BINARY=<build>/zene python3 probe-undo.py

Not a test: evidence for docs/TOOLS-SCOPE-AND-GATE6-FIX.md.
"""
from __future__ import annotations

import json
import os
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
BRIDGE = REPO / "tools" / "mcp-zene-control"
sys.path.insert(0, str(BRIDGE))
sys.path.insert(0, str(BRIDGE / "tests"))

import harness as H  # noqa: E402

FIXTURE = H.FIXTURE


def call(client: H.RawDawClient, cmd: str, args: dict | None = None) -> dict:
    try:
        reply = client.call(cmd, args or {})
    except Exception as exc:  # noqa: BLE001 - the point is to report whatever happens
        return {"__exception__": f"{type(exc).__name__}: {exc}"}
    return reply


def main() -> int:
    instance = H.Instance()
    try:
        instance.wait_engine()
        client = H.RawDawClient(instance.socket_path)
        steps = []
        steps.append(("project.open", call(client, "project.open", {"path": str(FIXTURE)})))
        steps.append(("transport.set_tempo", call(client, "transport.set_tempo", {"bpm": 128})))
        steps.append(("control.transactions", call(client, "control.transactions")))
        steps.append(("control.undo", call(client, "control.undo")))
        alive_after_undo = instance.process.poll() is None
        steps.append(("process_alive_after_undo", alive_after_undo))
        if alive_after_undo:
            steps.append(("same_connection_ping", call(client, "control.ping")))
        client.close()
        # A fresh connection answers "did the SERVER close only this client, or is
        # the instance going away?" without the bridge in the picture.
        time.sleep(1.0)
        steps.append(("socket_file_exists", os.path.exists(instance.socket_path)))
        steps.append(("process_alive_after_1s", instance.process.poll() is None))
        fresh: dict = {}
        try:
            probe = H.RawDawClient(instance.socket_path, timeout=5.0)
            try:
                fresh["control.ping"] = probe.call("control.ping")
                fresh["control.transactions"] = probe.call("control.transactions")
            finally:
                probe.close()
        except Exception as exc:  # noqa: BLE001
            fresh["__exception__"] = f"{type(exc).__name__}: {exc}"
        steps.append(("fresh_connection", fresh))
        report = {
            "binary": instance.binary,
            "socket_path": instance.socket_path,
            "steps": [{"step": name, "reply": reply} for name, reply in steps],
            "process_returncode": instance.process.poll(),
            "log_tail": instance.log_tail(3000),
        }
        print(json.dumps(report, indent=2, default=str))
        return 0
    finally:
        instance.cleanup()


if __name__ == "__main__":
    sys.exit(main())
