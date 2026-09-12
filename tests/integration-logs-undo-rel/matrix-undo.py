#!/usr/bin/env python3
"""The 4-cell discriminator matrix: {empty song, loaded project} x {probe shape}.

Two candidate variables decide where to look for the release-configuration
SIGSEGV, and this script separates them:

  * **empty song vs loaded project** - an empty song has no pattern tracks at
    all, so a crash there is NOT the pattern-track registry mechanism the
    earlier lane fixed (docs/CONTROL-UNDO-CONNECTION-DROP.md);
  * **probe shape** - the earlier lane's probe also asks whether the process is
    still alive ONE SECOND later and whether a FRESH connection is answered
    (its own evidence annotates `process_alive_after_undo True` as "a race, not
    survival"); the minimal probe asks only for the next request on the same
    connection.

Usage:

    ZENE_CONTROL_BINARY=<build>/zene python3 matrix-undo.py <label> [cells]

`cells` is a comma-separated subset of
    empty-minimal, fixture-minimal, empty-probe, fixture-probe
(default: all four). One JSON object per cell is appended to
matrix-<label>.jsonl in this directory; the summary line printed last is the
verdict per cell.

The project is resolved from the tree, never from a quoted path:
`git ls-files` is asked for a bundled project, and the fixture is the bridge's
own (an absolute path - a relative one is what made an earlier reproduction
report "no project loaded" while it was actually opening nothing).
"""
from __future__ import annotations

import json
import os
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
BRIDGE = REPO / "tools" / "mcp-zene-control"
sys.path.insert(0, str(BRIDGE))
sys.path.insert(0, str(BRIDGE / "tests"))

import harness as H  # noqa: E402

ALL_CELLS = ["empty-minimal", "fixture-minimal", "empty-probe", "fixture-probe"]


def bundled_project() -> str:
    """A project that ships in the tree, chosen from the index rather than quoted."""
    out = subprocess.run(["git", "ls-files", "data/projects/**/*.mmp*"],
                         cwd=REPO, capture_output=True, text=True, check=False).stdout
    for line in out.splitlines():
        if line.endswith(".mmp"):
            return str(REPO / line)
    first = out.splitlines()[0] if out.splitlines() else ""
    return str(REPO / first)


FIXTURE = str(H.FIXTURE)          # bridge fixture: one pattern track, tempo 140
DEMO = bundled_project()          # a project that ships in the tree


def call(client: H.RawDawClient, cmd: str, args: dict | None = None) -> dict:
    try:
        return client.call(cmd, args or {})
    except Exception as exc:  # noqa: BLE001 - a dropped connection is the finding
        return {"__exception__": f"{type(exc).__name__}: {exc}"}


def run_cell(cell: str) -> dict:
    source, shape = cell.split("-", 1)          # cells are named <source>-<shape>
    project = {"empty": None, "fixture": FIXTURE, "demo": DEMO}[source]
    report: dict = {"cell": cell, "shape": shape, "source": source, "project": project}
    instance = H.Instance()
    try:
        report["engine_ready_s"] = round(instance.wait_engine(), 2)
        client = H.RawDawClient(instance.socket_path)
        report["ping"] = call(client, "control.ping")
        if project is not None:
            report["project_open"] = call(client, "project.open", {"path": project})
            report["track_list"] = call(client, "track.list")
        else:
            report["track_list"] = call(client, "track.list")
        report["set_tempo"] = call(client, "transport.set_tempo", {"bpm": 128})
        report["transactions_before"] = call(client, "control.transactions")
        report["undo"] = call(client, "control.undo")
        report["alive_after_undo"] = instance.process.poll() is None
        if report["alive_after_undo"]:
            report["same_connection_ping"] = call(client, "control.ping")
        client.close()
        if shape == "probe":
            time.sleep(1.0)
            report["socket_file_exists"] = os.path.exists(instance.socket_path)
            report["process_alive_after_1s"] = instance.process.poll() is None
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
            report["fresh_connection"] = fresh
        else:
            report["next_request"] = call(client, "control.ping") \
                if report["alive_after_undo"] else {"__skipped__": "process gone"}
        time.sleep(0.5)
        report["process_alive_after_end"] = instance.process.poll() is None
        try:
            instance.process.wait(timeout=10)
        except Exception:  # noqa: BLE001
            pass
        report["process_returncode"] = instance.process.poll()
        report["log_tail"] = instance.log_tail(3000)
    finally:
        if instance.process.poll() is None:
            instance.cleanup()
    undo = report.get("undo", {})
    report["verdict"] = "PASS" if (
        undo.get("ok") is True
        and report.get("process_alive_after_end") is True
        and report.get("process_returncode") is None
    ) else "FAIL"
    return report


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    label = sys.argv[1]
    cells = ALL_CELLS
    if len(sys.argv) > 2 and sys.argv[2]:
        cells = [c for c in sys.argv[2].split(",") if c]
    out = HERE / f"matrix-{label}.jsonl"
    failures = 0
    for cell in cells:
        report = run_cell(cell)
        with out.open("a", encoding="utf-8") as handle:
            handle.write(json.dumps(report, default=str) + "\n")
        line = (f"{label:22s} {cell:18s} {report['verdict']:4s} "
                f"undo_ok={report.get('undo', {}).get('ok')} "
                f"alive_1s={report.get('process_alive_after_1s', report.get('process_alive_after_end'))} "
                f"rc={report.get('process_returncode')}")
        print(line, flush=True)
        if report["verdict"] != "PASS":
            failures += 1
    print(f"# {out}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
