#!/usr/bin/env python3
"""Run the raw undo probe and preserve the instance's own log.

`probe-undo.py` (copied verbatim from tests/integration-logs-3f-bridge/) drives the
DAW with the harness's RawDawClient and prints `log_tail(3000)` of the instance's
app.log, but the harness deletes that log in `cleanup()`. The interesting half of
this defect is what the *server* did, so this wrapper patches `H.Instance.cleanup`
to copy app.log out first and then runs the probe unchanged.

    ZENE_CONTROL_BINARY=<build>/zene python3 capture-undo.py
    ZENE_KEEP_LOG=name.log ZENE_CONTROL_BINARY=... python3 capture-undo.py

Set ZENE_CONTROL_BINARY to tests/integration-logs-3f-undo/gdb-wrapper.sh to run
the same probe with the instance under gdb; the gdb backtrace then lands in the
copied app.log as well as in the wrapper's own log.
"""
from __future__ import annotations

import os
import runpy
import shutil
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
BRIDGE = REPO / "tools" / "mcp-zene-control"
sys.path.insert(0, str(BRIDGE))
sys.path.insert(0, str(BRIDGE / "tests"))

import harness as H  # noqa: E402

KEEP = os.environ.get("ZENE_KEEP_LOG", "instance-app.log")


class KeepLog(H.Instance):
    """An Instance that copies its app.log into this directory before cleanup."""

    def cleanup(self) -> None:
        try:
            shutil.copyfile(self.log_path, HERE / KEEP)
        except OSError as exc:  # pragma: no cover - diagnostic path
            print(f"# could not keep {self.log_path}: {exc}", file=sys.stderr)
        super().cleanup()


H.Instance = KeepLog  # the probe resolves H.Instance at call time

runpy.run_path(str(HERE / "probe-undo.py"), run_name="__main__")
