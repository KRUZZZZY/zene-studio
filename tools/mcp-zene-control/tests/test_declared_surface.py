#!/usr/bin/env python3
"""A command group this build does not compile in is still a bridge obligation.

    python3 -m unittest tests.test_declared_surface -v
    python3 tests/test_declared_surface.py

THE CASE THIS COVERS. One command group exists in the registry sources and in no
binary on this machine: `wasm.*` is compiled in only when `WANT_WASM=ON` and the
wasmtime C API is on the find path (CMakeLists.txt:955-966), and every
configuration here degrades it to OFF with `Wasmtime_LIBRARY-NOTFOUND`. So
`tests/control-mcp-group-coverage.py` cannot drive it against a real instance in
this configuration and declares it with `--compiled-out wasm.` — a flag that is
checked in both directions, so it can never become a blindfold, but which by
itself says nothing about the bridge.

What this module adds is the bridge's half of that case: given a socket that
DECLARES the group's ids, does the bridge generate a tool per id and forward the
call? That is the property the release guard depends on — the same code path
serves every other group, and the ids are read from the DAW's own registry sources
(`ControlCommandsWasm*.cpp`), not written down here, so a rewrite of the group
that renames an id moves this test too.

WHAT IT DOES NOT CLAIM. This is a stand-in socket, not the DAW: it proves the
bridge carries a declared group, and it does NOT prove the sandbox's commands
work — that needs a wasm-enabled build, where
`tests/control-mcp-group-coverage.py` receives no `--compiled-out` flag and must
drive the group against the real binary. Stated here so the difference is on the
record rather than implied.

No real instance is started and no DAW binary is needed: the module is runnable
with any interpreter that has the `mcp` distribution (the bridge's own tests use
`tests/harness.py`, whose PYTHON default is this machine's Hermes venv).
"""
from __future__ import annotations

import os
import re
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
BRIDGE_DIR = HERE.parent
#: The lane root: <lane>/tools/mcp-zene-control/tests -> <lane>. (harness.py's own
#: REPO_DIR stops at tools/, which is where its stale DEFAULT_BINARY path came
#: from; this module needs `src/`, so it walks the three levels itself.)
REPO_ROOT = HERE.parents[2]
sys.path.insert(0, str(BRIDGE_DIR))

try:  # package import (python3 -m unittest tests.test_declared_surface)
    from . import harness as H
    from . import mcp_fixture as F
except ImportError:  # direct run (python3 tests/test_declared_surface.py)
    import harness as H  # type: ignore[no-redef]
    import mcp_fixture as F  # type: ignore[no-redef]

#: `QStringLiteral("group.verb")` in a group's registry sources.
ID_PATTERN = re.compile(r'QStringLiteral\("([a-z0-9_]+\.[a-z0-9_]+)"\)')


def source_group_ids(prefix: str) -> list[str]:
    """A group's ids as the DAW registers them, read off its own sources."""
    found: set[str] = set()
    for path in sorted((REPO_ROOT / "src" / "core").glob("ControlCommands*.cpp")):
        text = path.read_text(encoding="utf-8", errors="replace")
        found |= {match for match in ID_PATTERN.findall(text) if match.startswith(prefix)}
    return sorted(found)


class DeclaredSurface(unittest.TestCase):
    """`tools/list` and `tools/call` for a group the binary does not register."""

    maxDiff = None

    def run_session(self, body):
        """One MCP stdio session against a stub that declares `self.ids`."""
        stub = H.StubDaw(policy="surface", commands=self.ids)
        state_dir = tempfile.mkdtemp(prefix="zene-declared-state-", dir="/tmp")
        with stub:
            return F.run_bridge(stub.path, {"state_dir": state_dir}, body), stub

    def setUp(self) -> None:
        self.ids = source_group_ids("wasm.")
        self.assertTrue(self.ids, "no wasm.* id under %s: the group this case exists for has "
                                  "been renamed or removed" % (REPO_ROOT / "src" / "core"))

    def test_01_every_declared_id_becomes_a_tool_and_nothing_else_does(self):
        async def body(session, init):
            listed = await session.list_tools()
            return init, sorted(tool.name for tool in listed.tools)

        (init, names), _stub = self.run_session(body)
        expected = sorted(F.daw_tool_name(command_id) for command_id in self.ids)
        self.assertEqual(init.server_info.name, "zene-control")
        self.assertEqual([name for name in names if name not in ("zene_commands", "zene_status")],
                         expected,
                         "the generated tools must be exactly the declared group's ids")
        # Both directions: an id nobody declared has no tool either.
        self.assertNotIn(F.daw_tool_name("wasm.invented"), names)
        F.record(kind="declared_surface", phase="declared", declared=self.ids, listed=names)

    def test_02_a_declared_command_is_forwarded_verbatim(self):
        target = F.daw_tool_name(self.ids[0])

        async def body(session, init):
            result, payload, elapsed = await F.call(session, target, {}, "declared")
            return result, payload, elapsed

        (result, payload, elapsed), stub = self.run_session(body)
        self.assertFalse(result.is_error, payload)
        self.assertEqual(payload["command"], self.ids[0],
                         "the reply must name the command that was called")
        self.assertEqual(payload["result"]["command"], self.ids[0],
                         "the DAW's own answer must come back, not a bridge-side fabrication")
        # THE gate: it reached the socket. A bridge that answered from its own
        # tables would pass every assertion above and fail this one.
        self.assertIn(self.ids[0], stub.commands_seen(),
                      f"the stub never saw {self.ids[0]}: {stub.commands_seen()}")
        F.record(kind="declared_drive", phase="declared", tool=target, declared=self.ids[0],
                 stub_saw=stub.commands_seen(), elapsed_s=round(elapsed, 3))

    def test_03_the_offline_list_is_reported_stale_against_a_declared_surface(self):
        """The staleness flag fires for a group no snapshot can carry."""

        async def body(session, init):
            result, payload, elapsed = await F.call(session, "zene_status", {}, "declared")
            return result, payload, elapsed

        (result, payload, _elapsed), _stub = self.run_session(body)
        self.assertFalse(result.is_error, payload)
        drift = payload.get("offline_drift") or {}
        self.assertTrue(drift.get("checked"), payload)
        copies = {copy["source"]: copy for copy in drift.get("copies") or []}
        self.assertIn("snapshot", copies)
        snapshot = copies["snapshot"]
        self.assertEqual(snapshot["live_count"], len(self.ids),
                         "the live surface is the declared group and nothing else")
        # Every declared id the offline list cannot carry must be NAMED as missing:
        # that list is the whole point of the flag — it says which ids an agent
        # would not see, not merely that something is old.
        self.assertEqual(sorted(set(self.ids) & set(snapshot["missing_ids"])), sorted(self.ids),
                         "the snapshot carries no wasm.* id, so all of them are missing from it")
        self.assertGreaterEqual(snapshot["missing_count"], len(self.ids))
        self.assertTrue(snapshot["stale"],
                        "the committed snapshot carries no wasm.* id, so the flag must fire")
        self.assertIn("snapshot", drift.get("stale_copies") or [])
        F.record(kind="declared_staleness", phase="declared", drift=drift)


if __name__ == "__main__":
    unittest.main(verbosity=2)
