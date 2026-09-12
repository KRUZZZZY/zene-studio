#!/usr/bin/env python3
"""Explicit evidence for docs/AGENT-SURFACE-TELEMETRY-FIX.md.

Starts the real binary exactly the way tests/agent-surface-gate.py does (offscreen,
dummy audio, throwaway HOME), then asks the live surface two questions and prints
the answers:

  1. control.commands_list -> the telemetry.* entries, with their `requires`;
  2. control.surface_report -> the Help menu item whose text is
     "Telemetry - what we send...", with the command id it declares.

The gate's own verdict only reports counts; this says WHICH action resolved, so the
report can quote it. Read-only: nothing is mutated, and the instance is asked to quit.
"""

import importlib.util
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
GATE = os.path.join(REPO, "tests", "agent-surface-gate.py")

spec = importlib.util.spec_from_file_location("agent_surface_gate", GATE)
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)

BINARY = os.path.join(REPO, "build", "zene")
FIXTURE = os.path.join(REPO, "tests", "data", "agent-control-fixture.mmp")


def main():
    options = gate.parse_args([BINARY, FIXTURE, "--check"])
    options.binary = os.path.abspath(BINARY)
    options.fixture = os.path.abspath(FIXTURE)
    tmp = gate.tempfile.mkdtemp(prefix="zsurface-evidence-", dir="/tmp")
    log_file = process = None
    try:
        process, log_file, socket_path, log_path, client = gate.connect_session(options, tmp)
        listing = gate.fetch(client, 1, "control.commands_list")
        telemetry = [c for c in listing["commands"] if c["id"].startswith("telemetry.")]
        print("commands_list: %d commands; telemetry.* ->" % listing["count"])
        for entry in sorted(telemetry, key=lambda e: e["id"]):
            print("  %-20s requires=%s mutating=%s" % (entry["id"], entry["requires"], entry["mutating"]))
            print("      %s" % entry["description"][:110])

        surface = gate.fetch(client, 2, "control.surface_report")
        items = [a for a in surface["actions"] if a["text"].startswith("Telemetry - what we send")]
        print("surface_report: %d actions, %d unregistered; consent action ->"
              % (surface["action_count"], surface["unregistered_count"]))
        for item in items:
            print("  surface=%s container=%s class=%s" % (item["surface"], item["container_path"],
                                                          item["container_class"]))
            print("  text=%r object_name=%r command=%r declared_unknown=%s"
                  % (item["text"], item["object_name"], item["command"], item["declared_unknown"]))
        if not items:
            print("NO TELEMETRY ACTION REFLECTED")
            return 1
        if any(i["command"] != "telemetry.consent" for i in items):
            print("  the action does not resolve to telemetry.consent")
            return 1
        print("EVIDENCE OK: the Help menu consent action resolves to telemetry.consent")
        # Ask the instance to exit the normal way, so the log ends with a clean
        # shutdown instead of the gate's "the app is still running" kill.
        print("control.quit: %s" % gate.quit_and_wait(client, process, 3)["status"])
        return 0
    finally:
        gate.cleanup(log_file, process, log_path, tmp)


if __name__ == "__main__":
    sys.exit(main())
