#!/usr/bin/env python3
"""Run repro-undo-release.py but keep the instance's own app.log.

The harness deletes its scratch directory (and with it app.log) in cleanup(),
and the interesting half of this defect is what the *server* did after it sent
the reply. This wrapper patches H.Instance.cleanup to copy app.log out first.

    ZENE_CONTROL_BINARY=<build>/zene python3 capture-undo-release.py [label]
    ZENE_KEEP_LOG=name.log ZENE_CONTROL_BINARY=... python3 capture-undo-release.py

Point ZENE_CONTROL_BINARY at gdb-wrapper.sh to run the same repro with the
instance under gdb; gdb's output lands in the wrapper's log, and the app.log
copy shows how far the process got.
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

runpy.run_path(str(HERE / "repro-undo-release.py"), run_name="__main__")
