#!/usr/bin/env python3
"""Which step actually kills the release-configuration build?

The 4-cell matrix says the crash does not depend on the project: it fires on an
EMPTY song too, so it is not the pattern-track registry the earlier lane fixed.
This narrows the *trigger* instead: each variant does one prefix of the repro,
then waits, then asks the same connection for one more reply.

  ping-only          ping, wait, ping
  tempo-only         set_tempo, wait, ping
  undo-only          undo (nothing recorded), wait, ping
  tempo-then-undo    set_tempo, undo, wait, ping        <- the reported repro
  undo-slow          set_tempo, undo, wait 3 s, ping    <- is it timing?

Usage: ZENE_CONTROL_BINARY=<build>/zene python3 probe-variants.py <label> [variants]
"""
from __future__ import annotations

import json
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
BRIDGE = REPO / "tools" / "mcp-zene-control"
sys.path.insert(0, str(BRIDGE))
sys.path.insert(0, str(BRIDGE / "tests"))

import harness as H  # noqa: E402

ALL = ["ping-only", "tempo-only", "undo-only", "tempo-then-undo", "undo-slow"]


def call(client: H.RawDawClient, cmd: str, args: dict | None = None) -> dict:
    try:
        return client.call(cmd, args or {})
    except Exception as exc:  # noqa: BLE001
        return {"__exception__": f"{type(exc).__name__}: {exc}"}


def run(variant: str) -> dict:
    report: dict = {"variant": variant, "steps": {}}
    instance = H.Instance()
    try:
        instance.wait_engine()
        client = H.RawDawClient(instance.socket_path)
        report["steps"]["ping0"] = call(client, "control.ping")
        if variant in ("tempo-only", "tempo-then-undo", "undo-slow"):
            report["steps"]["set_tempo"] = call(client, "transport.set_tempo", {"bpm": 128})
        if variant in ("undo-only", "tempo-then-undo", "undo-slow"):
            report["steps"]["undo"] = call(client, "control.undo")
        wait = 3.0 if variant == "undo-slow" else 1.0
        report["alive_after_wait_step"] = instance.process.poll() is None
        time.sleep(wait)
        report["process_alive_after_wait"] = instance.process.poll() is None
        if report["process_alive_after_wait"]:
            report["steps"]["ping1"] = call(client, "control.ping")
        client.close()
        try:
            instance.process.wait(timeout=5)
        except Exception:  # noqa: BLE001
            pass
        report["process_returncode"] = instance.process.poll()
    finally:
        if instance.process.poll() is None:
            instance.cleanup()
    undo_ok = report["steps"].get("undo", {"ok": True}).get("ok")
    report["verdict"] = "PASS" if (
        undo_ok is not False and report["process_returncode"] is None
    ) else "FAIL"
    return report


def main() -> int:
    label = sys.argv[1] if len(sys.argv) > 1 else "release"
    variants = ALL if len(sys.argv) < 3 else [v for v in sys.argv[2].split(",") if v]
    out = HERE / f"variants-{label}.jsonl"
    bad = 0
    for variant in variants:
        report = run(variant)
        with out.open("a", encoding="utf-8") as handle:
            handle.write(json.dumps(report, default=str) + "\n")
        print(f"{label:10s} {variant:18s} {report['verdict']:4s} "
              f"alive_after_wait={report['process_alive_after_wait']} "
              f"rc={report['process_returncode']}", flush=True)
        if report["verdict"] != "PASS":
            bad += 1
    print(f"# {out}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
