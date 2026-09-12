#!/usr/bin/env python3
"""End-to-end: the bridge's BOUNDED TYPED FAILURE paths, over real stdio MCP.

    python3 -m unittest tests.test_mcp_errors -v
    python3 tests/test_mcp_errors.py

This is the half of the contract that says a failure is always a typed, bounded
result rather than a hang, a silent no-op or a fake success: no instance
(test_05), a leftover socket file (test_06), an instance SIGKILLed mid-call
(test_07), a wedged instance that never replies (test_08) and the readiness rule
that keeps an engine command off the wire until `control.ping` says
`engine_ready` (test_10). test_07 freezes the real shared instance and SIGKILLs
it; the rest use `harness.StubDaw`, a deliberately broken DAW, so they are
deterministic rather than racy.

The live instance's own contract (generated tool list, full flow, resources,
the DAW's typed errors, protocol check, undo) is `tests/test_mcp_e2e.py`; the one
real instance, its state directory (where the live command-list cache that
test_05 reads back is written) and the transcript are shared through
`tests/mcp_fixture.py`, so this module can be run alone.

Exit code 0 only when every assertion passed.
"""
from __future__ import annotations

import asyncio
import os
import shutil
import socket
import tempfile
import time
import unittest

try:  # package import (python3 -m unittest tests.test_mcp_errors)
    from . import harness as H
    from . import mcp_fixture as F
except ImportError:  # direct run (python3 tests/test_mcp_errors.py)
    import harness as H  # type: ignore[no-redef]
    import mcp_fixture as F  # type: ignore[no-redef]


def setUpModule() -> None:
    # The shared instance still has to exist here: test_05 reads back the live
    # command-list cache that tests/test_mcp_e2e.py's fixture run wrote into the
    # shared state directory. Idempotent, so it is the same instance either way.
    F.ensure()


# ---------------------------------------------------------------------------
# tests
# ---------------------------------------------------------------------------

class ZeneControlBridgeErrorPaths(unittest.TestCase):
    maxDiff = None

    # -- 5. no instance: typed, bounded, and discovery still works --------

    def test_05_no_instance_is_typed_and_discoverability_still_works(self):
        missing = os.path.join(tempfile.mkdtemp(prefix="zene-empty-", dir="/tmp"), "zene.sock")

        async def body(session, init):
            out = {}
            result, payload, elapsed = await F.call(session, "zene_mixer_get_state", {},
                                                   "no_instance", read_timeout=60)
            out["call"] = (result.is_error, payload, elapsed)
            result, payload, _ = await F.call(session, "zene_status", {}, "no_instance")
            out["status"] = (result.is_error, payload)
            result, payload, _ = await F.call(session, "zene_commands", {}, "no_instance")
            out["commands"] = (result.is_error, payload)
            listed = await session.list_tools()
            out["tools"] = sorted(t.name for t in listed.tools)
            return out

        out = F.run_bridge(missing, {"call_timeout": 5}, body)
        self.assertTrue(out["call"][0], "a missing instance must be an isError result")
        self.assertEqual(F.error_kind(out["call"][1]), "no_instance")
        self.assertLess(out["call"][2], 5.0, "the typed error must arrive inside the timeout")
        self.assertEqual(out["status"][1]["reachable"], False)
        self.assertEqual(out["status"][1]["error"]["kind"], "no_instance")
        self.assertIn("no control socket", out["status"][1]["error"]["message"])
        # discoverability must not depend on an instance
        self.assertFalse(out["commands"][0])
        self.assertEqual(out["commands"][1]["count"], F.INFO["daw_commands"])
        self.assertIn(out["commands"][1]["source"], ("cache", "snapshot"))
        self.assertFalse(out["commands"][1]["live"])
        self.assertEqual(out["commands"][1]["instance_error"]["kind"], "no_instance")
        generated = [n for n in out["tools"] if n not in ("zene_commands", "zene_status")]
        self.assertEqual(len(generated), F.INFO["daw_commands"])
        F.record(kind="no_instance_matrix", phase="no_instance",
                 call_kind=F.error_kind(out["call"][1]), call_elapsed_s=out["call"][2],
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
            return await F.call(session, "zene_mixer_get_state", {}, "stale_socket",
                                read_timeout=60)

        result, payload, elapsed = F.run_bridge(path, {"call_timeout": 5}, body)
        self.assertTrue(result.is_error)
        self.assertEqual(F.error_kind(payload), "stale_socket")
        self.assertLess(elapsed, 5.0)
        self.assertIn("nothing is listening", payload["error"]["message"])
        F.record(kind="stale_socket", phase="stale_socket", error_kind=F.error_kind(payload),
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
            result, structured = F.run_bridge(
                instance.socket_path, {"call_timeout": 20, "offline": True}, body)
            elapsed = time.monotonic() - started
            self.assertTrue(result.is_error, structured)
            self.assertEqual(F.error_kind(structured), "disconnected", structured)
            self.assertLess(elapsed, 20.0, "the disconnect must be reported inside the budget")
            frozen_result["mid_call"] = F.error_kind(structured)
            F.record(kind="killed_mid_call", phase="kill", error_kind=F.error_kind(structured),
                     elapsed_s=round(elapsed, 3),
                     inner_elapsed_s=(structured.get("error") or {}).get("elapsed_s"),
                     timeout_s=structured.get("timeout_s"),
                     message=structured["error"]["message"])

            # the SIGKILL left the socket file behind -> stale_socket on the next call
            self.assertTrue(os.path.exists(instance.socket_path),
                            "SIGKILL should leave the socket file for the stale case")

            async def body2(session, init):
                return await F.call(session, "zene_mixer_get_state", {}, "kill",
                                    read_timeout=60)

            result2, payload2, elapsed2 = F.run_bridge(instance.socket_path,
                                                       {"call_timeout": 5}, body2)
            self.assertTrue(result2.is_error)
            self.assertEqual(F.error_kind(payload2), "stale_socket")
            self.assertLess(elapsed2, 5.0)
            F.record(kind="killed_idle", phase="kill", error_kind=F.error_kind(payload2),
                     elapsed_s=round(elapsed2, 3))
        finally:
            instance.cleanup()

    # -- 8. a wedged instance is bounded by timeout_s --------------------

    def test_08_silent_instance_hits_the_bounded_timeout(self):
        with H.StubDaw(policy="silent") as stub:
            async def body(session, init):
                return await F.call(session, "zene_mixer_get_state", {}, "timeout",
                                    read_timeout=60)

            started = time.monotonic()
            result, payload, elapsed = F.run_bridge(stub.path, {"call_timeout": 1}, body)
            wall = time.monotonic() - started
            self.assertTrue(result.is_error)
            self.assertEqual(F.error_kind(payload), "timeout", payload)
            self.assertLess(wall, 10.0, "a silent instance must not wedge the bridge")
            self.assertTrue(stub.commands_seen(), "the stub saw nothing: the call was a no-op")
            F.record(kind="timeout", phase="timeout", error_kind=F.error_kind(payload),
                     bridge_wall_s=round(wall, 3), stub_saw=stub.commands_seen())

    # -- 10. the readiness rule: nothing is sent before engine_ready ----

    def test_10_readiness_rule_gates_engine_commands(self):
        with H.StubDaw(policy="ping", engine_ready=False) as stub:
            async def body(session, init):
                out = {}
                result, payload, elapsed = await F.call(session, "zene_mixer_get_state", {},
                                                        "readiness", read_timeout=60)
                out["engine_command"] = (result.is_error, payload, elapsed)
                result, payload, elapsed2 = await F.call(session, "zene_control_ping", {},
                                                         "readiness", read_timeout=60)
                out["engine_free_command"] = (result.is_error, payload, elapsed2)
                result, payload, _ = await F.call(session, "zene_commands", {}, "readiness")
                out["commands"] = (result.is_error, payload)
                return out

            out = F.run_bridge(stub.path,
                               {"ready_timeout": 2, "call_timeout": 10, "long_call_timeout": 10},
                               body)
            is_error, payload, elapsed = out["engine_command"]
            self.assertTrue(is_error)
            self.assertEqual(F.error_kind(payload), "not_ready", payload)
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
            F.record(kind="readiness_gate", phase="readiness", seen_after_engine_call=seen,
                     kind_reported=F.error_kind(payload), elapsed_s=round(elapsed, 3))

            # an engine-free command is NOT gated (it answers before readiness)
            self.assertFalse(out["engine_free_command"][0], out["engine_free_command"][1])
            self.assertFalse(out["engine_free_command"][1]["result"]["engine_ready"])
            self.assertLess(out["engine_free_command"][2], 2.0,
                            "an engine-free command must not wait for readiness")
            # ...and the command list falls back to the last-known copy
            self.assertFalse(out["commands"][0])
            self.assertIn(out["commands"][1]["source"], ("cache", "snapshot"))
            self.assertIn(out["commands"][1]["instance_error"]["kind"], ("busy", "bridge_error"))


if __name__ == "__main__":
    unittest.main(verbosity=2)
