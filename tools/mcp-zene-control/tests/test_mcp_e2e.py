#!/usr/bin/env python3
"""End-to-end: a REAL MCP client over stdio drives a REAL headless instance.

    python3 -m unittest tests.test_mcp_e2e -v
    python3 tests/test_mcp_e2e.py

This module is the **live instance's own contract**, driven end to end: the tool
list generated from the instance's `control.commands_list` (test_01), the full
flow through the bridge (test_02), the resources (test_03), the DAW's typed
errors arriving verbatim (test_04), the protocol check (test_09) and
mutating-command transactions with undo (test_11). Every assertion goes through
`mcp.client.stdio.stdio_client` + `mcp.ClientSession` (initialize, tools/list,
tools/call, resources/read) against a `server.py` child process; the DAW side is
the real binary started with `--control-socket` under the headless recipe in
harness.py.

The other half of the same contract — the **bounded typed failures** (no
instance, stale socket, killed mid-call, wedged stub, the readiness gate) — is
`tests/test_mcp_errors.py`. The one real instance, its state directory (where the
live command-list cache is written) and the verbatim transcript are shared
through `tests/mcp_fixture.py`, so either module can be run alone and still gets
the fixture it needs.

Exit code 0 only when every assertion passed.
"""
from __future__ import annotations

import json
import os
import shutil
import tempfile
import unittest

try:  # package import (python3 -m unittest tests.test_mcp_e2e)
    from . import harness as H
    from . import mcp_fixture as F
except ImportError:  # direct run (python3 tests/test_mcp_e2e.py)
    import harness as H  # type: ignore[no-redef]
    import mcp_fixture as F  # type: ignore[no-redef]


def setUpModule() -> None:
    # One real, ready, headless instance shared with tests.test_mcp_errors;
    # idempotent, so whichever suite unittest reaches first starts it.
    F.ensure()


# ---------------------------------------------------------------------------
# tests
# ---------------------------------------------------------------------------

class ZeneControlBridgeE2E(unittest.TestCase):
    maxDiff = None

    # -- 1. the tool list is generated and matches the DAW exactly --------

    def _listed_tools(self, instance):
        """Initialize, list tools, record the exchange, return (listed, names, generated)."""

        async def body(session, init):
            listed = await session.list_tools()
            return init, listed

        init, listed = F.run_bridge(instance.socket_path, {}, body)
        names = sorted(t.name for t in listed.tools)
        bridge_owned = ("zene_commands", "zene_status")
        generated = sorted(n for n in names if n not in bridge_owned)
        F.record(kind="tools_list", phase="tool_generation",
                 server_info={"name": init.server_info.name, "version": init.server_info.version},
                 protocol=getattr(init, "protocol_version", None),
                 tool_count=len(names), generated_count=len(generated),
                 generated=generated, bridge_tools=[n for n in names if n.startswith("zene_") and n in
                                                    bridge_owned])
        return listed, names, generated

    def _assert_daw_args_survive(self, daw_commands: list[dict], by_name: dict) -> None:
        """Every DAW args property survives, plus the bridge-owned optional timeout_s."""
        for command in daw_commands:
            tool = by_name[F.daw_tool_name(command["id"])]
            daw_props = set((command.get("args_schema") or {}).get("properties") or {})
            props = set((tool.input_schema or {}).get("properties") or {})
            self.assertTrue(daw_props <= props, f"{command['id']}: lost {daw_props - props}")
            self.assertIn("timeout_s", props)
            self.assertNotIn("timeout_s", (tool.input_schema or {}).get("required") or [])
            self.assertNotIn(command["id"], json.dumps(tool.input_schema),
                             "the schema must come from the DAW, not be synthesised")

    def test_01_tools_list_set_equals_daw_commands_list(self):
        instance = F.live_instance()
        daw_commands = instance.commands()
        daw_ids = sorted(c["id"] for c in daw_commands)
        expected = sorted(F.daw_tool_name(i) for i in daw_ids)

        listed, names, generated = self._listed_tools(instance)

        self.assertEqual(len(daw_ids), len(daw_commands), "duplicate command ids in the DAW list")
        self.assertEqual(generated, expected,
                         "bridge tool names must be exactly prefix+id for every DAW command")
        self.assertEqual(len(names), len(set(names)), "duplicate tool names")
        self.assertIn("zene_commands", names)
        self.assertIn("zene_status", names)
        by_name = {t.name: t for t in listed.tools}
        self._assert_daw_args_survive(daw_commands, by_name)
        # a known long operation is documented as such
        render = by_name["zene_render_render"]
        self.assertIn("LONG OPERATION", render.description)
        self.assertIn("900", json.dumps(render.input_schema["properties"]["timeout_s"]))

    # -- 2. the full flow through the bridge -----------------------------

    def test_02_full_flow_open_mixer_set_volume_render_save(self):
        instance = F.live_instance()
        work = tempfile.mkdtemp(prefix="zene-flow-", dir="/tmp")
        project = os.path.join(work, "agent-control-fixture.mmp")
        shutil.copyfile(H.FIXTURE, project)
        out_wav = os.path.join(work, "render.wav")
        saved = os.path.join(work, "saved.mmp")
        try:
            async def body(session, init):
                steps = {}
                result, opened, _ = await F.call(session, "zene_project_open", {"path": project},
                                                 F.FULL_FLOW_PHASE)
                self.assertFalse(result.is_error, opened)
                steps["opened"] = opened
                self.assertEqual(opened["result"]["file"], project)

                result, mixer, _ = await F.call(session, "zene_mixer_get_state", {},
                                                F.FULL_FLOW_PHASE)
                self.assertFalse(result.is_error, mixer)
                steps["mixer_before"] = mixer
                self.assertTrue(mixer["result"]["channels"], "the fixture loaded no mixer channels")

                # the fixture ships the master channel only; master volume is not
                # addressable, so add a channel and drive that one.
                result, added, _ = await F.call(session, "zene_mixer_add_channel", {},
                                                F.FULL_FLOW_PHASE)
                self.assertFalse(result.is_error, added)
                steps["added"] = added
                target = added["result"]["channel"]
                self.assertTrue(target.startswith("ch-"), target)

                result, setvol, _ = await F.call(
                    session, "zene_mixer_set_volume", {"channel": target, "volume": 0.5},
                    F.FULL_FLOW_PHASE)
                self.assertFalse(result.is_error, setvol)
                steps["set_volume"] = setvol
                self.assertAlmostEqual(float(setvol["result"]["volume"]), 0.5, places=6)

                result, mixer2, _ = await F.call(session, "zene_mixer_get_state", {},
                                                 F.FULL_FLOW_PHASE)
                self.assertFalse(result.is_error, mixer2)
                steps["mixer_after"] = mixer2
                changed = [c for c in mixer2["result"]["channels"] if c.get("id") == target]
                self.assertTrue(changed, f"{target} vanished from mixer.get_state")
                self.assertAlmostEqual(float(changed[0]["volume"]), 0.5, places=6)

                result, rendered, elapsed = await F.call(
                    session, "zene_render_render", {"out": out_wav, "format": "wav"},
                    F.FULL_FLOW_PHASE, read_timeout=600)
                self.assertFalse(result.is_error, rendered)
                steps["render"] = rendered
                self.assertEqual(rendered["result"]["path"], out_wav)
                self.assertGreater(int(rendered["result"]["frames"]), 0)
                self.assertTrue(rendered["result"]["sha256"])
                self.assertGreater(os.path.getsize(out_wav), 44)
                F.record(kind="render_artifact", phase=F.FULL_FLOW_PHASE, path=out_wav,
                         bytes=os.path.getsize(out_wav), seconds=round(elapsed, 3))

                result, savedp, _ = await F.call(session, "zene_project_save", {"path": saved},
                                                 F.FULL_FLOW_PHASE)
                self.assertFalse(result.is_error, savedp)
                steps["saved"] = savedp
                self.assertEqual(savedp["result"]["file"], saved)
                self.assertTrue(os.path.exists(saved))

                result, state, _ = await F.call(session, "zene_project_get_state", {},
                                                F.FULL_FLOW_PHASE)
                self.assertFalse(result.is_error, state)
                steps["project_state"] = state
                self.assertTrue(state["result"]["file"])
                return steps

            steps = F.run_bridge(instance.socket_path, {}, body)
            F.record(kind="full_flow_result", phase=F.FULL_FLOW_PHASE,
                     steps=sorted(steps), out_wav=out_wav, saved=saved)
        finally:
            shutil.rmtree(work, ignore_errors=True)

    # -- 3. resources ----------------------------------------------------

    def test_03_resources_expose_project_state_and_status(self):
        instance = F.live_instance()

        async def body(session, init):
            listed = await session.list_resources()
            uris = [str(r.uri) for r in listed.resources]
            state = await session.read_resource("zene://project/state")
            status = await session.read_resource("zene://instance/status")
            return uris, state, status

        uris, state, status = F.run_bridge(instance.socket_path, {}, body)
        state_text = state.contents[0].text
        status_text = status.contents[0].text
        F.record(kind="resources_list", phase="resources", uris=uris)
        F.record(kind="resource_read", phase="resources", uri="zene://project/state",
                 text=state_text)
        self.assertIn("zene://project/state", uris)
        self.assertIn("zene://instance/status", uris)
        project_state = json.loads(state_text)
        self.assertTrue(project_state["ok"], project_state)
        self.assertTrue(project_state["result"]["file"])
        bridge_status = json.loads(status_text)
        self.assertTrue(bridge_status["reachable"])
        self.assertTrue(bridge_status["engine_ready"])
        self.assertIn("zene-control", bridge_status["summary"])

    # -- 4. the DAW's typed errors pass through unchanged -----------------

    def test_04_daw_typed_errors_pass_through(self):
        instance = F.live_instance()

        async def body(session, init):
            out = {}
            result, payload, _ = await F.call(session, "zene_mixer_set_volume",
                                              {"channel": "ch-9999", "volume": 0.5}, "daw_errors")
            out["unknown_channel"] = (result.is_error, payload)
            result, payload, _ = await F.call(session, "zene_mixer_set_pan",
                                              {"channel": "ch-0", "pan": 0.5}, "daw_errors")
            out["pan_refused"] = (result.is_error, payload)
            result, payload, _ = await F.call(session, "zene_transport_seek",
                                              {"ticks": "not-a-number"}, "daw_errors")
            out["bad_args"] = (result.is_error, payload)
            result, payload, _ = await F.call(session, "zene_no_such_tool", {}, "daw_errors")
            out["unknown_tool"] = (result.is_error, payload)
            return out

        out = F.run_bridge(instance.socket_path, {}, body)
        self.assertTrue(out["unknown_channel"][0])
        self.assertEqual(out["unknown_channel"][1]["error"]["kind"], "not_found")
        self.assertEqual(out["unknown_channel"][1]["error"]["origin"], "daw")
        self.assertEqual(out["pan_refused"][1]["error"]["kind"], "refused")
        self.assertEqual(out["bad_args"][1]["error"]["kind"], "invalid_args")
        self.assertEqual(out["unknown_tool"][1]["error"]["kind"], "not_found")
        self.assertIn("zene_mixer_get_state", out["unknown_tool"][1]["error"]["message"])

    # -- 9. protocol mismatch -------------------------------------------

    def test_09_proto_mismatch_is_typed(self):
        instance = F.live_instance()
        # the instance itself answers `refused` when the request declares a
        # foreign proto; the bridge catches the mismatch earlier and by kind.
        raw = instance.ping(proto=99)

        async def body(session, init):
            return await F.call(session, "zene_mixer_get_state", {}, "proto", read_timeout=60)

        result, payload, elapsed = F.run_bridge(instance.socket_path,
                                                {"proto": 2, "call_timeout": 5}, body)
        self.assertTrue(result.is_error)
        self.assertEqual(F.error_kind(payload), "proto_mismatch", payload)
        self.assertIn("proto 1", payload["error"]["message"])
        self.assertIn("proto 2", payload["error"]["message"])
        self.assertLess(elapsed, 5.0)
        F.record(kind="proto_mismatch", phase="proto", error_kind=F.error_kind(payload),
                 bridge_message=payload["error"]["message"],
                 daw_reply_to_proto99={"ok": raw.get("ok"),
                                       "error": raw.get("error")})

    # -- 11. mutating commands and undo, through the bridge -------------

    def test_11_mutating_command_undo_and_transactions(self):
        instance = F.live_instance()
        work = tempfile.mkdtemp(prefix="zene-undo-", dir="/tmp")
        project = os.path.join(work, "agent-control-fixture.mmp")
        shutil.copyfile(H.FIXTURE, project)
        try:
            async def body(session, init):
                out = {}
                result, payload, _ = await F.call(session, "zene_project_open",
                                                  {"path": project}, "undo")
                out["open"] = (result.is_error, payload)
                result, payload, _ = await F.call(session, "zene_transport_set_tempo",
                                                  {"bpm": 128}, "undo")
                out["set_tempo"] = (result.is_error, payload)
                result, payload, _ = await F.call(session, "zene_transport_get_state", {}, "undo")
                out["get_state"] = (result.is_error, payload)
                result, payload, _ = await F.call(session, "zene_control_transactions", {}, "undo")
                out["transactions"] = (result.is_error, payload)
                result, payload, _ = await F.call(session, "zene_control_undo", {}, "undo")
                out["undo"] = (result.is_error, payload)
                return out

            out = F.run_bridge(instance.socket_path, {}, body)
            self.assertFalse(out["set_tempo"][0], out["set_tempo"][1])
            self.assertEqual(int(out["set_tempo"][1]["result"]["tempo"]), 128)
            self.assertEqual(int(out["get_state"][1]["result"]["tempo"]), 128)
            transactions = out["transactions"][1]["result"]["transactions"]
            commands = {t.get("command"): t for t in transactions}
            self.assertIn("transport.set_tempo", commands)
            self.assertTrue(commands["transport.set_tempo"].get("reversible"),
                            "a mutating command must record a reversible transaction")
            self.assertTrue(out["undo"][1]["result"]["undone"], out["undo"][1])
            F.record(kind="undo", phase="undo",
                     commands=sorted(commands), undone=out["undo"][1]["result"]["undone"])
        finally:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    unittest.main(verbosity=2)
