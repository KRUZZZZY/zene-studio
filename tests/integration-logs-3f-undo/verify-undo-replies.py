#!/usr/bin/env python3
"""Does `control.undo` now REPLY, and does the journal still work afterwards?

`probe-undo.py` answers "what happened" (a raw transcript of the four steps);
this file answers "is the command usable" - it drives the same four steps and
then keeps USING the connection and the journal, which is the whole point of the
fix: an agent that loses its socket on undo cannot rely on the journal at all.

Asserted here, over ONE connection, with no MCP and no bridge in the picture:

  1. `control.undo` returns a REPLY (ok, undone=true) instead of dropping the
     connection;
  2. the instance is still alive and the SAME connection answers `control.ping`;
  3. the undo did its job: `project.get_state` reports the tempo the transaction
     recorded as the inverse (140, the fixture's own tempo, against the 128 the
     probe set);
  4. the journal is still usable: `control.transactions` answers, and
     `control.undo`/`control.redo` keep answering on the same connection.

    ZENE_CONTROL_BINARY=<build>/zene python3 verify-undo-replies.py
    (exit 0 only if every assertion held; a JSON report goes to stdout)
"""
from __future__ import annotations

import json
import os
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
BRIDGE = REPO / "tools" / "mcp-zene-control"
sys.path.insert(0, str(BRIDGE))
sys.path.insert(0, str(BRIDGE / "tests"))

import harness as H  # noqa: E402

FIXTURE = H.FIXTURE
FAILURES: list[str] = []


def check(label: str, condition: bool, detail: object = None) -> None:
    print(f"  {'ok  ' if condition else 'FAIL'} {label}"
          + ("" if detail is None else f"  ({detail})"))
    if not condition:
        FAILURES.append(label)


def main() -> int:
    instance = H.Instance()
    try:
        instance.wait_engine()
        client = H.RawDawClient(instance.socket_path)
        report: dict = {"steps": {}}

        report["steps"]["project.open"] = client.call("project.open", {"path": str(FIXTURE)})
        report["steps"]["set_tempo_128"] = client.call("transport.set_tempo", {"bpm": 128})
        report["steps"]["transactions_before"] = client.call("control.transactions")
        try:
            undo = client.call("control.undo")
        except Exception as exc:  # noqa: BLE001 - a dropped connection is the defect
            undo = {"__exception__": f"{type(exc).__name__}: {exc}"}
        report["steps"]["control.undo"] = undo
        report["alive_after_undo"] = instance.process.poll() is None
        if report["alive_after_undo"]:
            report["steps"]["ping_same_connection"] = client.call("control.ping")
            report["steps"]["state_after_undo"] = client.call("project.get_state")
            report["steps"]["transactions_after"] = client.call("control.transactions")
            report["steps"]["redo"] = client.call("control.redo")
            report["steps"]["undo_again"] = client.call("control.undo")
            report["steps"]["ping_after_redo_undo"] = client.call("control.ping")
        client.close()
        report["process_returncode"] = instance.process.poll()

        print("control.undo -> "
              + json.dumps(report["steps"]["control.undo"].get("result", undo))[:200])
        check("control.undo answered a reply (no dropped connection)",
              report["steps"]["control.undo"].get("ok") is True,
              report["steps"]["control.undo"].get("__exception__"))
        check("the instance survived the undo", report["alive_after_undo"] is True)
        check("the SAME connection still answers control.ping",
              report["steps"].get("ping_same_connection", {}).get("ok") is True)
        tempo = (report["steps"].get("state_after_undo", {}).get("result") or {}).get("tempo")
        check("the undo restored the recorded inverse tempo (140)", tempo == 140, tempo)
        check("control.transactions still answers",
              report["steps"].get("transactions_after", {}).get("ok") is True)
        check("control.redo still answers",
              report["steps"].get("redo", {}).get("ok") is True)
        check("a second control.undo still answers",
              report["steps"].get("undo_again", {}).get("ok") is True)
        check("the connection is still usable after redo+undo again",
              report["steps"].get("ping_after_redo_undo", {}).get("ok") is True)
        report["failures"] = FAILURES
        print(json.dumps(report, indent=2, default=str))
        return 1 if FAILURES else 0
    finally:
        instance.cleanup()


if __name__ == "__main__":
    sys.exit(main())
