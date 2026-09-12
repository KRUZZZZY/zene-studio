#!/usr/bin/env python3
"""The release-blocking repro, verbatim and minimal.

    start: zene --control-socket <path>          (QT_QPA_PLATFORM=offscreen)
    poll control.ping until result.engine_ready == true
    transport.set_tempo {"bpm": 128}   -> expect ok, {"tempo": 128}
    control.undo                      -> expect ok, {"undone": true, ...}
    the NEXT request                  -> Broken pipe / ECONNRESET is the defect
    process exit status               -> 139 (SIGSEGV) is the defect

No project is loaded: an empty song, one tempo change, one undo. That is the
whole repro - there is no fixture in it on purpose, because the defect was
reported against this exact sequence.

    ZENE_CONTROL_BINARY=<build>/zene python3 repro-undo-release.py [label]

Exit 0 only when the undo replied AND the process survived AND the next request
on the SAME connection was answered. Anything else is a failure, and the exit
status of the instance is printed either way.
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


def call(client: H.RawDawClient, cmd: str, args: dict | None = None) -> dict:
    try:
        return client.call(cmd, args or {})
    except Exception as exc:  # noqa: BLE001 - a dropped connection is the defect
        return {"__exception__": f"{type(exc).__name__}: {exc}"}


def main() -> int:
    label = sys.argv[1] if len(sys.argv) > 1 else "run"
    bpm = int(os.environ.get("ZENE_REPRO_BPM", "128"))
    instance = H.Instance()
    report: dict = {"label": label, "binary": instance.binary, "steps": {}}
    failed = False
    try:
        t_socket = instance.wait_socket()
        t_engine = instance.wait_engine()
        report["socket_ready_s"] = round(t_socket, 2)
        report["engine_ready_s"] = round(t_engine, 2)

        client = H.RawDawClient(instance.socket_path)
        report["steps"]["ping"] = call(client, "control.ping")
        report["steps"][f"set_tempo_{bpm}"] = call(client, "transport.set_tempo", {"bpm": bpm})
        report["steps"]["undo"] = call(client, "control.undo")

        undo = report["steps"]["undo"]
        if undo.get("ok") is not True:
            failed = True
        if undo.get("result", {}).get("undone_command") != "transport.set_tempo":
            failed = True

        # The next request on the SAME connection - the call after the one that
        # reported success. This is what an agent experiences.
        report["steps"]["next_request"] = call(client, "control.ping")
        report["steps"]["tempo_after_undo"] = call(client, "project.get_state")
        time.sleep(0.5)
        report["alive_after_undo"] = instance.process.poll() is None
        report["process_returncode"] = instance.process.poll()
        if report["steps"]["next_request"].get("ok") is not True:
            failed = True
        if not report["alive_after_undo"]:
            failed = True
        client.close()
        try:
            instance.process.wait(timeout=10)
        except Exception:  # noqa: BLE001
            pass
        report["process_returncode"] = instance.process.poll()
        report["log_tail"] = instance.log_tail(2000)
    finally:
        if instance.process.poll() is None:
            instance.cleanup()
    report["failed"] = failed
    print(json.dumps(report, indent=2, default=str))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
