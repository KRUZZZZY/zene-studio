#!/usr/bin/env python3
"""End-to-end: a REAL MCP client over stdio drives a REAL headless instance.

    python3 -m unittest tests.test_mcp_e2e -v
    python3 tests/test_mcp_e2e.py

Every assertion goes through `mcp.client.stdio.stdio_client` +
`mcp.ClientSession` (initialize, tools/list, tools/call, resources/read) against
a `server.py` child process; the DAW side is the real binary started with
`--control-socket` under the headless recipe in harness.py. The error-path tests
use a stub DAW so they are deterministic rather than racy, and one of them
freezes the real instance and SIGKILLs it mid-call.

Exit code 0 only when every assertion passed.
"""
from __future__ import annotations

import asyncio
import json
import os
import shutil
import signal
import socket
import sys
import tempfile
import time
import unittest
from pathlib import Path
from typing import Any, Callable, Coroutine

HERE = Path(__file__).resolve().parent
BRIDGE_DIR = HERE.parent
sys.path.insert(0, str(BRIDGE_DIR))

try:  # package import (python3 -m unittest tests.test_mcp_e2e)
    from . import harness as H
except ImportError:  # direct run (python3 tests/test_mcp_e2e.py)
    import harness as H  # type: ignore[no-redef]

from mcp import ClientSession, StdioServerParameters  # noqa: E402
from mcp.client.stdio import stdio_client  # noqa: E402

import zene_control.config as C  # noqa: E402
import zene_control.tools as T  # noqa: E402

INSTANCE: H.Instance | None = None
STATE_DIR: str | None = None
INFO: dict[str, Any] = {}
TRANSCRIPT: list[dict] = []
FAILURES: list[str] = []

FULL_FLOW_PHASE = "full_flow"


# ---------------------------------------------------------------------------
# module fixture: one real, ready, headless instance, shared by most tests
# ---------------------------------------------------------------------------

def setUpModule() -> None:
    global INSTANCE, STATE_DIR
    H.binary_path()  # fail loudly (never silently skip) when there is no binary
    STATE_DIR = tempfile.mkdtemp(prefix="zene-control-state-", dir="/tmp")
    instance = H.Instance()
    try:
        INFO["socket_s"] = round(instance.wait_socket(), 3)
        INFO["engine_ready_s"] = round(instance.wait_engine(), 3)
        INFO["binary"] = instance.binary
        INFO["socket_path"] = instance.socket_path
        config = C.Config(socket_path=instance.socket_path, workdir=str(H.BRIDGE_DIR),
                          state_dir=STATE_DIR)
        source = T.Bridge(config).fetch_live()
        INFO["daw_commands"] = source.count
        INFO["command_list_source"] = source.source
    except Exception:
        instance.cleanup()
        raise
    INSTANCE = instance


def tearDownModule() -> None:
    if INSTANCE is not None:
        INFO["instance_exit"] = INSTANCE.quit()
        INSTANCE.cleanup()
    if STATE_DIR:
        out = os.path.join(STATE_DIR, "mcp-transcript.json")
        try:
            with open(out, "w", encoding="utf-8") as handle:
                json.dump({"info": INFO, "transcript": TRANSCRIPT}, handle, indent=2, default=str)
            INFO["transcript_path"] = out
        except OSError:
            pass
    print("\n---- module fixture ----")
    print(json.dumps(INFO, indent=2, default=str))
    print("\n---- MCP transcript (every request/response) ----")
    for entry in TRANSCRIPT:
        print(json.dumps(entry, indent=2, default=str))
    no = [t for t in TRANSCRIPT if t.get("kind") == "tools_list" and t.get("generated_count") == 0]
    if no:
        FAILURES.append("a tools/list returned zero generated tools")
    print(f"\nMCP E2E: {len(TRANSCRIPT)} recorded exchanges; fixture socket "
          f"{INFO.get('socket_s')}s, engine ready {INFO.get('engine_ready_s')}s, "
          f"{INFO.get('daw_commands')} DAW commands")


# ---------------------------------------------------------------------------
# helpers
# ---------------------------------------------------------------------------

def bridge_env() -> dict[str, str]:
    env = dict(os.environ)
    if STATE_DIR:
        env["ZENE_CONTROL_STATE"] = STATE_DIR
    return env


def record(**entry: Any) -> None:
    entry["step"] = len(TRANSCRIPT) + 1
    TRANSCRIPT.append(entry)


def run_bridge(socket_path: str, options: dict, body: Callable[..., Coroutine]):
    """Open a stdio MCP session to the bridge, initialize, run `body`."""
    params = StdioServerParameters(
        command=H.PYTHON,
        args=H.bridge_argv(socket_path, **options),
        env=bridge_env(),
        cwd=str(H.BRIDGE_DIR),
    )

    async def _main():
        async with stdio_client(params) as (read, write):
            async with ClientSession(read, write) as session:
                init = await session.initialize()
                return await body(session, init)

    return asyncio.run(_main())


async def call(session: ClientSession, tool: str, arguments: dict | None = None,
               phase: str = "call", read_timeout: float | None = None):
    """tools/call with the exchange recorded verbatim."""
    started = time.monotonic()
    result = await session.call_tool(tool, arguments or {},
                                     read_timeout_seconds=read_timeout)
    elapsed = time.monotonic() - started
    structured = getattr(result, "structured_content", None) or {}
    first = ""
    for block in (getattr(result, "content", None) or []):
        if getattr(block, "type", None) == "text":
            first = block.text
            break
    record(kind="tools_call", phase=phase, tool=tool, arguments=arguments or {},
           is_error=bool(getattr(result, "is_error", False)), elapsed_s=round(elapsed, 3),
           summary=first, structured=structured)
    return result, structured, elapsed


def error_kind(structured: dict) -> str | None:
    return ((structured or {}).get("error") or {}).get("kind")


def daw_tool_name(command_id: str) -> str:
    return "zene_" + command_id.replace(".", "_")


# ---------------------------------------------------------------------------
# tests
# ---------------------------------------------------------------------------

class ZeneControlBridgeE2E(unittest.TestCase):
    maxDiff = None

    # -- 1. the tool list is generated and matches the DAW exactly --------

    def test_01_tools_list_set_equals_daw_commands_list(self):
        instance = INSTANCE
        assert instance is not None
        daw_commands = instance.commands()
        daw_ids = sorted(c["id"] for c in daw_commands)
        expected = sorted(daw_tool_name(i) for i in daw_ids)

        async def body(session, init):
            listed = await session.list_tools()
            return init, listed

        init, listed = run_bridge(instance.socket_path, {}, body)
        names = sorted(t.name for t in listed.tools)
        generated = sorted(n for n in names if n not in ("zene_commands", "zene_status"))
        record(kind="tools_list", phase="tool_generation",
               server_info={"name": init.server_info.name, "version": init.server_info.version},
               protocol=getattr(init, "protocol_version", None),
               tool_count=len(names), generated_count=len(generated),
               generated=generated, bridge_tools=[n for n in names if n.startswith("zene_") and n in
                                                  ("zene_commands", "zene_status")])

        self.assertEqual(len(daw_ids), len(daw_commands), "duplicate command ids in the DAW list")
        self.assertEqual(generated, expected,
                         "bridge tool names must be exactly prefix+id for every DAW command")
        self.assertEqual(len(names), len(set(names)), "duplicate tool names")
        self.assertIn("zene_commands", names)
        self.assertIn("zene_status", names)
        # every DAW args property survives, plus the bridge-owned timeout_s
        by_name = {t.name: t for t in listed.tools}
        for command in daw_commands:
            tool = by_name[daw_tool_name(command["id"])]
            daw_props = set((command.get("args_schema") or {}).get("properties") or {})
            props = set((tool.input_schema or {}).get("properties") or {})
            self.assertTrue(daw_props <= props, f"{command['id']}: lost {daw_props - props}")
            self.assertIn("timeout_s", props)
            self.assertNotIn("timeout_s", (tool.input_schema or {}).get("required") or [])
            self.assertNotIn(command["id"], json.dumps(tool.input_schema),
                             "the schema must come from the DAW, not be synthesised")
        # a known long operation is documented as such
        render = by_name["zene_render_render"]
        self.assertIn("LONG OPERATION", render.description)
        self.assertIn("900", json.dumps(render.input_schema["properties"]["timeout_s"]))

    # -- 2. the full flow through the bridge -----------------------------

    def test_02_full_flow_open_mixer_set_volume_render_save(self):
        instance = INSTANCE
        assert instance is not None
        work = tempfile.mkdtemp(prefix="zene-flow-", dir="/tmp")
        project = os.path.join(work, "agent-control-fixture.mmp")
        shutil.copyfile(H.FIXTURE, project)
        out_wav = os.path.join(work, "render.wav")
        saved = os.path.join(work, "saved.mmp")
        try:
            async def body(session, init):
                steps = {}
                result, opened, _ = await call(session, "zene_project_open", {"path": project},
                                               FULL_FLOW_PHASE)
                self.assertFalse(result.is_error, opened)
                steps["opened"] = opened
                self.assertEqual(opened["result"]["file"], project)

                result, mixer, _ = await call(session, "zene_mixer_get_state", {}, FULL_FLOW_PHASE)
                self.assertFalse(result.is_error, mixer)
                steps["mixer_before"] = mixer
                self.assertTrue(mixer["result"]["channels"], "the fixture loaded no mixer channels")

                # the fixture ships the master channel only; master volume is not
                # addressable, so add a channel and drive that one.
                result, added, _ = await call(session, "zene_mixer_add_channel", {},
                                              FULL_FLOW_PHASE)
                self.assertFalse(result.is_error, added)
                steps["added"] = added
                target = added["result"]["channel"]
                self.assertTrue(target.startswith("ch-"), target)

                result, setvol, _ = await call(
                    session, "zene_mixer_set_volume", {"channel": target, "volume": 0.5},
                    FULL_FLOW_PHASE)
                self.assertFalse(result.is_error, setvol)
                steps["set_volume"] = setvol
                self.assertAlmostEqual(float(setvol["result"]["volume"]), 0.5, places=6)

                result, mixer2, _ = await call(session, "zene_mixer_get_state", {}, FULL_FLOW_PHASE)
                self.assertFalse(result.is_error, mixer2)
                steps["mixer_after"] = mixer2
                changed = [c for c in mixer2["result"]["channels"] if c.get("id") == target]
                self.assertTrue(changed, f"{target} vanished from mixer.get_state")
                self.assertAlmostEqual(float(changed[0]["volume"]), 0.5, places=6)

                result, rendered, elapsed = await call(
                    session, "zene_render_render", {"out": out_wav, "format": "wav"},
                    FULL_FLOW_PHASE, read_timeout=600)
                self.assertFalse(result.is_error, rendered)
                steps["render"] = rendered
                self.assertEqual(rendered["result"]["path"], out_wav)
                self.assertGreater(int(rendered["result"]["frames"]), 0)
                self.assertTrue(rendered["result"]["sha256"])
                self.assertGreater(os.path.getsize(out_wav), 44)
                record(kind="render_artifact", phase=FULL_FLOW_PHASE, path=out_wav,
                       bytes=os.path.getsize(out_wav), seconds=round(elapsed, 3))

                result, savedp, _ = await call(session, "zene_project_save", {"path": saved},
                                               FULL_FLOW_PHASE)
                self.assertFalse(result.is_error, savedp)
                steps["saved"] = savedp
                self.assertEqual(savedp["result"]["file"], saved)
                self.assertTrue(os.path.exists(saved))

                result, state, _ = await call(session, "zene_project_get_state", {},
                                              FULL_FLOW_PHASE)
                self.assertFalse(result.is_error, state)
                steps["project_state"] = state
                self.assertTrue(state["result"]["file"])
                return steps

            steps = run_bridge(instance.socket_path, {}, body)
            record(kind="full_flow_result", phase=FULL_FLOW_PHASE,
                   steps=sorted(steps), out_wav=out_wav, saved=saved)
        finally:
            shutil.rmtree(work, ignore_errors=True)

    # -- 3. resources ----------------------------------------------------

    def test_03_resources_expose_project_state_and_status(self):
        instance = INSTANCE
        assert instance is not None

        async def body(session, init):
            listed = await session.list_resources()
            uris = [str(r.uri) for r in listed.resources]
            state = await session.read_resource("zene://project/state")
            status = await session.read_resource("zene://instance/status")
            return uris, state, status

        uris, state, status = run_bridge(instance.socket_path, {}, body)
        state_text = state.contents[0].text
        status_text = status.contents[0].text
        record(kind="resources_list", phase="resources", uris=uris)
        record(kind="resource_read", phase="resources", uri="zene://project/state",
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
        instance = INSTANCE
        assert instance is not None

        async def body(session, init):
            out = {}
            result, payload, _ = await call(session, "zene_mixer_set_volume",
                                            {"channel": "ch-9999", "volume": 0.5}, "daw_errors")
            out["unknown_channel"] = (result.is_error, payload)
            result, payload, _ = await call(session, "zene_mixer_set_pan",
                                            {"channel": "ch-0", "pan": 0.5}, "daw_errors")
            out["pan_refused"] = (result.is_error, payload)
            result, payload, _ = await call(session, "zene_transport_seek",
                                            {"ticks": "not-a-number"}, "daw_errors")
            out["bad_args"] = (result.is_error, payload)
            result, payload, _ = await call(session, "zene_no_such_tool", {}, "daw_errors")
            out["unknown_tool"] = (result.is_error, payload)
            return out

        out = run_bridge(instance.socket_path, {}, body)
        self.assertTrue(out["unknown_channel"][0])
        self.assertEqual(out["unknown_channel"][1]["error"]["kind"], "not_found")
        self.assertEqual(out["unknown_channel"][1]["error"]["origin"], "daw")
        self.assertEqual(out["pan_refused"][1]["error"]["kind"], "refused")
        self.assertEqual(out["bad_args"][1]["error"]["kind"], "invalid_args")
        self.assertEqual(out["unknown_tool"][1]["error"]["kind"], "not_found")
        self.assertIn("zene_mixer_get_state", out["unknown_tool"][1]["error"]["message"])

    # -- 5. no instance: typed, bounded, and discovery still works --------

    def test_05_no_instance_is_typed_and_discoverability_still_works(self):
        missing = os.path.join(tempfile.mkdtemp(prefix="zene-empty-", dir="/tmp"), "zene.sock")

        async def body(session, init):
            out = {}
            result, payload, elapsed = await call(session, "zene_mixer_get_state", {},
                                                  "no_instance", read_timeout=60)
            out["call"] = (result.is_error, payload, elapsed)
            result, payload, _ = await call(session, "zene_status", {}, "no_instance")
            out["status"] = (result.is_error, payload)
            result, payload, _ = await call(session, "zene_commands", {}, "no_instance")
            out["commands"] = (result.is_error, payload)
            listed = await session.list_tools()
            out["tools"] = sorted(t.name for t in listed.tools)
            return out

        out = run_bridge(missing, {"call_timeout": 5}, body)
        self.assertTrue(out["call"][0], "a missing instance must be an isError result")
        self.assertEqual(error_kind(out["call"][1]), "no_instance")
        self.assertLess(out["call"][2], 5.0, "the typed error must arrive inside the timeout")
        self.assertEqual(out["status"][1]["reachable"], False)
        self.assertEqual(out["status"][1]["error"]["kind"], "no_instance")
        self.assertIn("no control socket", out["status"][1]["error"]["message"])
        # discoverability must not depend on an instance
        self.assertFalse(out["commands"][0])
        self.assertEqual(out["commands"][1]["count"], INFO["daw_commands"])
        self.assertIn(out["commands"][1]["source"], ("cache", "snapshot"))
        self.assertFalse(out["commands"][1]["live"])
        self.assertEqual(out["commands"][1]["instance_error"]["kind"], "no_instance")
        generated = [n for n in out["tools"] if n not in ("zene_commands", "zene_status")]
        self.assertEqual(len(generated), INFO["daw_commands"])
        record(kind="no_instance_matrix", phase="no_instance",
               call_kind=error_kind(out["call"][1]), call_elapsed_s=out["call"][2],
               commands_source=out["commands"][1]["source"],
               generated_tools_offline=len(generated))

    # -- 6. stale socket file --------------------------------------------

    def test_06_stale_socket_is_typed(self):
        tmp = tempfile.mkdtemp(prefix="zene-stale-", dir="/tmp")
        path = os.path.join(tmp, "zene.sock")
        listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        listener.bind(path)
        listener.listen(1)
        listener.close()  # the file stays behind, nobody is listening
        self.assertTrue(os.path.exists(path))

        async def body(session, init):
            return await call(session, "zene_mixer_get_state", {}, "stale_socket",
                              read_timeout=60)

        result, payload, elapsed = run_bridge(path, {"call_timeout": 5}, body)
        self.assertTrue(result.is_error)
        self.assertEqual(error_kind(payload), "stale_socket")
        self.assertLess(elapsed, 5.0)
        self.assertIn("nothing is listening", payload["error"]["message"])
        record(kind="stale_socket", phase="stale_socket", error_kind=error_kind(payload),
               elapsed_s=round(elapsed, 3))
        shutil.rmtree(tmp, ignore_errors=True)

    # -- 7. a killed instance: disconnect, not a hang ---------------------

    def test_07_killed_instance_mid_call_is_disconnected_within_budget(self):
        instance = H.Instance()
        try:
            instance.wait_engine()
            frozen_result: dict = {}

            async def body(session, init):
                # FREEZE first so the instance can never answer: the call is
                # genuinely in flight when the kill lands.
                #
                # `--offline` is deliberate here and is passed below: it takes the
                # pre-call command-list probe out of the path, so the only socket
                # use is the command's own connection. Without it the probe (which
                # must absorb a disconnect to fall back to the cache) can consume
                # the window and the command's later connect reports the
                # post-kill state (stale_socket) instead. That second behaviour is
                # asserted immediately after this one.
                instance.freeze()
                task = asyncio.create_task(
                    session.call_tool("zene_mixer_get_state", {}, read_timeout_seconds=60)
                )
                await asyncio.sleep(0.5)
                assert instance.process.poll() is None, "the frozen instance died on its own"
                instance.kill()
                result = await task
                structured = getattr(result, "structured_content", None) or {}
                return result, structured

            started = time.monotonic()
            result, structured = run_bridge(
                instance.socket_path, {"call_timeout": 20, "offline": True}, body)
            elapsed = time.monotonic() - started
            self.assertTrue(result.is_error, structured)
            self.assertEqual(error_kind(structured), "disconnected", structured)
            self.assertLess(elapsed, 20.0, "the disconnect must be reported inside the budget")
            frozen_result["mid_call"] = error_kind(structured)
            record(kind="killed_mid_call", phase="kill", error_kind=error_kind(structured),
                   elapsed_s=round(elapsed, 3),
                   inner_elapsed_s=(structured.get("error") or {}).get("elapsed_s"),
                   timeout_s=structured.get("timeout_s"),
                   message=structured["error"]["message"])

            # the SIGKILL left the socket file behind -> stale_socket on the next call
            self.assertTrue(os.path.exists(instance.socket_path),
                            "SIGKILL should leave the socket file for the stale case")

            async def body2(session, init):
                return await call(session, "zene_mixer_get_state", {}, "kill",
                                  read_timeout=60)

            result2, payload2, elapsed2 = run_bridge(instance.socket_path, {"call_timeout": 5}, body2)
            self.assertTrue(result2.is_error)
            self.assertEqual(error_kind(payload2), "stale_socket")
            self.assertLess(elapsed2, 5.0)
            record(kind="killed_idle", phase="kill", error_kind=error_kind(payload2),
                   elapsed_s=round(elapsed2, 3))
        finally:
            instance.cleanup()

    # -- 8. a wedged instance is bounded by timeout_s --------------------

    def test_08_silent_instance_hits_the_bounded_timeout(self):
        with H.StubDaw(policy="silent") as stub:
            async def body(session, init):
                return await call(session, "zene_mixer_get_state", {}, "timeout",
                                  read_timeout=60)

            started = time.monotonic()
            result, payload, elapsed = run_bridge(stub.path, {"call_timeout": 1}, body)
            wall = time.monotonic() - started
            self.assertTrue(result.is_error)
            self.assertEqual(error_kind(payload), "timeout", payload)
            self.assertLess(wall, 10.0, "a silent instance must not wedge the bridge")
            self.assertTrue(stub.commands_seen(), "the stub saw nothing: the call was a no-op")
            record(kind="timeout", phase="timeout", error_kind=error_kind(payload),
                   bridge_wall_s=round(wall, 3), stub_saw=stub.commands_seen())

    # -- 9. protocol mismatch -------------------------------------------

    def test_09_proto_mismatch_is_typed(self):
        instance = INSTANCE
        assert instance is not None
        # the instance itself answers `refused` when the request declares a
        # foreign proto; the bridge catches the mismatch earlier and by kind.
        raw = instance.ping(proto=99)

        async def body(session, init):
            return await call(session, "zene_mixer_get_state", {}, "proto", read_timeout=60)

        result, payload, elapsed = run_bridge(instance.socket_path, {"proto": 2, "call_timeout": 5},
                                              body)
        self.assertTrue(result.is_error)
        self.assertEqual(error_kind(payload), "proto_mismatch", payload)
        self.assertIn("proto 1", payload["error"]["message"])
        self.assertIn("proto 2", payload["error"]["message"])
        self.assertLess(elapsed, 5.0)
        record(kind="proto_mismatch", phase="proto", error_kind=error_kind(payload),
               bridge_message=payload["error"]["message"],
               daw_reply_to_proto99={"ok": raw.get("ok"),
                                     "error": raw.get("error")})

    # -- 10. the readiness rule: nothing is sent before engine_ready ----

    def test_10_readiness_rule_gates_engine_commands(self):
        with H.StubDaw(policy="ping", engine_ready=False) as stub:
            async def body(session, init):
                out = {}
                result, payload, elapsed = await call(session, "zene_mixer_get_state", {},
                                                      "readiness", read_timeout=60)
                out["engine_command"] = (result.is_error, payload, elapsed)
                result, payload, elapsed2 = await call(session, "zene_control_ping", {},
                                                       "readiness", read_timeout=60)
                out["engine_free_command"] = (result.is_error, payload, elapsed2)
                result, payload, _ = await call(session, "zene_commands", {}, "readiness")
                out["commands"] = (result.is_error, payload)
                return out

            out = run_bridge(stub.path,
                             {"ready_timeout": 2, "call_timeout": 10, "long_call_timeout": 10},
                             body)
            is_error, payload, elapsed = out["engine_command"]
            self.assertTrue(is_error)
            self.assertEqual(error_kind(payload), "not_ready", payload)
            self.assertLess(elapsed, 8.0, "the readiness poll must be bounded")
            self.assertIn("engine was not ready", payload["error"]["message"])

            # THE gate: the stub never received the engine command. `control.*`
            # commands are engine-free, so pings (and the command-list probe)
            # are legitimate traffic; the engine command is not.
            seen = stub.commands_seen()
            self.assertTrue(seen, "the stub saw nothing at all")
            self.assertNotIn("mixer.get_state", seen,
                             f"the bridge sent an engine command before readiness: {seen}")
            self.assertTrue(set(seen) <= {"control.ping", "control.commands_list"},
                            f"unexpected traffic before readiness: {seen}")
            record(kind="readiness_gate", phase="readiness", seen_after_engine_call=seen,
                   kind_reported=error_kind(payload), elapsed_s=round(elapsed, 3))

            # an engine-free command is NOT gated (it answers before readiness)
            self.assertFalse(out["engine_free_command"][0], out["engine_free_command"][1])
            self.assertFalse(out["engine_free_command"][1]["result"]["engine_ready"])
            self.assertLess(out["engine_free_command"][2], 2.0,
                            "an engine-free command must not wait for readiness")
            # ...and the command list falls back to the last-known copy
            self.assertFalse(out["commands"][0])
            self.assertIn(out["commands"][1]["source"], ("cache", "snapshot"))
            self.assertIn(out["commands"][1]["instance_error"]["kind"], ("busy", "bridge_error"))

    # -- 11. mutating commands and undo, through the bridge -------------

    def test_11_mutating_command_undo_and_transactions(self):
        instance = INSTANCE
        assert instance is not None
        work = tempfile.mkdtemp(prefix="zene-undo-", dir="/tmp")
        project = os.path.join(work, "agent-control-fixture.mmp")
        shutil.copyfile(H.FIXTURE, project)
        try:
            async def body(session, init):
                out = {}
                result, payload, _ = await call(session, "zene_project_open",
                                                {"path": project}, "undo")
                out["open"] = (result.is_error, payload)
                result, payload, _ = await call(session, "zene_transport_set_tempo",
                                                {"bpm": 128}, "undo")
                out["set_tempo"] = (result.is_error, payload)
                result, payload, _ = await call(session, "zene_transport_get_state", {}, "undo")
                out["get_state"] = (result.is_error, payload)
                result, payload, _ = await call(session, "zene_control_transactions", {}, "undo")
                out["transactions"] = (result.is_error, payload)
                result, payload, _ = await call(session, "zene_control_undo", {}, "undo")
                out["undo"] = (result.is_error, payload)
                return out

            out = run_bridge(instance.socket_path, {}, body)
            self.assertFalse(out["set_tempo"][0], out["set_tempo"][1])
            self.assertEqual(int(out["set_tempo"][1]["result"]["tempo"]), 128)
            self.assertEqual(int(out["get_state"][1]["result"]["tempo"]), 128)
            transactions = out["transactions"][1]["result"]["transactions"]
            commands = {t.get("command"): t for t in transactions}
            self.assertIn("transport.set_tempo", commands)
            self.assertTrue(commands["transport.set_tempo"].get("reversible"),
                            "a mutating command must record a reversible transaction")
            self.assertTrue(out["undo"][1]["result"]["undone"], out["undo"][1])
            record(kind="undo", phase="undo",
                   commands=sorted(commands), undone=out["undo"][1]["result"]["undone"])
        finally:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    unittest.main(verbosity=2)
