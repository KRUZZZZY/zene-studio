#!/usr/bin/env python3
"""What actually decides whether the undo kills the instance?

The 4-cell matrix says the *session*, not the build configuration: the default
project (instrument + sample + pattern + automation) dies; a project opened from
the tree (one pattern track) does not. This walks bundled projects and reports,
per project, the Song container's track types and whether `control.undo` kills
the process — one row per run, appended to `<out>.jsonl`.

    ZENE_CONTROL_BINARY=<build>/zene python3 probe-tracktypes.py <label> [paths...]
"""
from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
BRIDGE = REPO / "tools" / "mcp-zene-control"
sys.path.insert(0, str(BRIDGE))
sys.path.insert(0, str(BRIDGE / "tests"))

import harness as H  # noqa: E402


def default_projects(limit: int = 4) -> list[str]:
    out = subprocess.run(["git", "ls-files", "data/projects/**/*.mmp*"],
                         cwd=REPO, capture_output=True, text=True, check=False).stdout
    return [str(REPO / line) for line in out.splitlines()][:limit]


def call(client: H.RawDawClient, cmd: str, args: dict | None = None) -> dict:
    try:
        return client.call(cmd, args or {})
    except Exception as exc:  # noqa: BLE001
        return {"__exception__": f"{type(exc).__name__}: {exc}"}


def run(project: str | None) -> dict:
    report: dict = {"project": project or "(none: the default project)"}
    instance = H.Instance()
    try:
        instance.wait_engine()
        client = H.RawDawClient(instance.socket_path)
        if project:
            report["open"] = call(client, "project.open", {"path": project}).get("ok")
        tl = call(client, "track.list").get("result", {})
        report["track_count"] = tl.get("count")
        report["track_types"] = [t.get("type") for t in tl.get("tracks", [])]
        report["set_tempo"] = call(client, "transport.set_tempo", {"bpm": 128}).get("ok")
        report["undo"] = call(client, "control.undo").get("ok")
        client.close()
        import time
        time.sleep(1.0)
        report["alive_after_1s"] = instance.process.poll() is None
        try:
            instance.process.wait(timeout=5)
        except Exception:  # noqa: BLE001
            pass
        report["process_returncode"] = instance.process.poll()
    finally:
        if instance.process.poll() is None:
            instance.cleanup()
    report["verdict"] = "PASS" if report.get("process_returncode") is None else "DEAD"
    return report


def main() -> int:
    label = sys.argv[1] if len(sys.argv) > 1 else "release"
    projects = sys.argv[2:] or default_projects()
    out = HERE / f"tracktypes-{label}.jsonl"
    dead = 0
    for project in [None, *projects]:
        report = run(project)
        with out.open("a", encoding="utf-8") as handle:
            handle.write(json.dumps(report, default=str) + "\n")
        print(f"{report['verdict']:5s} rc={report['process_returncode']} "
              f"tracks={report['track_count']} types={report['track_types']} "
              f"{report['project'][-60:]}", flush=True)
        if report["verdict"] == "DEAD":
            dead += 1
    print(f"# {dead} DEAD of {len(projects) + 1}   ({out})")
    return 1 if dead else 0


if __name__ == "__main__":
    sys.exit(main())
