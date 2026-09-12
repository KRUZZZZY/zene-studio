#!/usr/bin/env python3
"""Unit tests for the bridge: naming, generation, taxonomy, timeouts, offline.

    python3 -m unittest tests.test_units -v
    python3 tests/test_units.py

These need no DAW binary and no instance. The wire shapes are the real ones,
captured from a live instance's `control.commands_list` (`CONTROL_ENTRY` below),
so the generation tests exercise the actual payload.

The wire client's own concern — real AF_UNIX sockets and the lifecycle cases
(no instance, stale socket, mid-flight disconnect, protocol mismatch) — is
`tests/test_wire_client.py`. The end-to-end suites are `tests/test_mcp_e2e.py`
(the live instance's contract) and `tests/test_mcp_errors.py` (the bounded
typed failures).
"""
from __future__ import annotations

import errno
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
BRIDGE_DIR = HERE.parent
sys.path.insert(0, str(BRIDGE_DIR))

import zene_control.config as C  # noqa: E402
import zene_control.registry as R  # noqa: E402
import zene_control.tools as T  # noqa: E402
from zene_control.protocol import _describe_oserror  # noqa: E402

#: A verbatim entry from a live instance's control.commands_list (2026-09-11,
#: binary 0.2.0-alpha, the agentctl lane's build/lmms).
CONTROL_ENTRY = {
    "id": "mixer.set_volume",
    "group": "mixer",
    "description": "Set a mixer channel's fader.",
    "requires": [],
    "mutating": True,
    "args_schema": {
        "type": "object",
        "properties": {"channel": {"type": "string"},
                       "volume": {"type": "number", "minimum": 0, "maximum": 2}},
        "required": ["channel", "volume"],
        "additionalProperties": False,
    },
    "result_schema": {
        "type": "object",
        "properties": {"channel": {"type": "string"}, "volume": {"type": "number"}},
        "required": [], "additionalProperties": False,
    },
}


def config_for(**kwargs: object) -> C.Config:
    base: dict = {"socket_path": "/tmp/none/zene.sock", "workdir": "/tmp/none",
                  "state_dir": tempfile.mkdtemp(prefix="zene-unit-state-", dir="/tmp")}
    base.update(kwargs)
    return C.Config(**base)


class TestNaming(unittest.TestCase):
    def test_dot_becomes_underscore_and_prefix(self):
        self.assertEqual(R.tool_name("mixer.set_volume"), "zene_mixer_set_volume")
        self.assertEqual(R.tool_name("render.render"), "zene_render_render")
        self.assertEqual(R.tool_name("control.commands_list"), "zene_control_commands_list")

    def test_stable(self):
        for _ in range(3):
            self.assertEqual(R.tool_name("transport.set_tempo"), "zene_transport_set_tempo")

    def test_dashes_and_odd_characters(self):
        self.assertEqual(R.tool_name("session.slot-fill"), "zene_session_slot_fill")
        self.assertEqual(R.tool_name("a.b/c d"), "zene_a_b_c_d")
        self.assertNotIn("--", R.tool_name("x.a--b"))

    def test_injective_over_the_daw_list(self):
        ids = ["mixer.set_volume", "render.render", "control.ping", "track.get_state",
               "transport.set_tempo", "project.save", "mixer.set_pan"]
        names = [R.tool_name(i) for i in ids]
        self.assertEqual(len(names), len(set(names)))

    def test_bridge_tools_are_not_in_the_generated_namespace(self):
        for name in R.BRIDGE_TOOL_NAMES:
            self.assertTrue(name.startswith(R.TOOL_PREFIX))


class TestGeneration(unittest.TestCase):
    def test_spec_from_wire(self):
        spec = R.spec_from_wire(CONTROL_ENTRY)
        self.assertEqual(spec.id, "mixer.set_volume")
        self.assertEqual(spec.group, "mixer")
        self.assertEqual(spec.verb, "set_volume")
        self.assertTrue(spec.mutating)
        self.assertTrue(spec.requires_engine)  # mixer is not engine-free
        self.assertEqual(spec.tool_name, "zene_mixer_set_volume")
        self.assertEqual(spec.args_schema["required"], ["channel", "volume"])

    def test_control_group_is_engine_free(self):
        for command_id in ("control.ping", "control.commands_list", "control.quit"):
            entry = dict(CONTROL_ENTRY, id=command_id, group="control", mutating=False)
            self.assertFalse(R.spec_from_wire(entry).requires_engine)

    def test_mcp_tool_keeps_the_daw_schema(self):
        spec = R.spec_from_wire(CONTROL_ENTRY)
        tool = R.mcp_tool(spec, 30.0)
        schema = tool["inputSchema"]
        self.assertEqual(schema["required"], ["channel", "volume"])
        self.assertEqual(schema["additionalProperties"], False)
        self.assertEqual(schema["properties"]["volume"]["maximum"], 2)
        # the only addition is the bridge knob, and it is optional
        self.assertEqual(set(schema["properties"]) - {"channel", "volume"}, {"timeout_s"})
        self.assertNotIn("timeout_s", schema["required"])
        self.assertIn("LONG OPERATION", R.mcp_tool(
            R.spec_from_wire(dict(CONTROL_ENTRY, id="render.render", group="render")), 900.0
        )["description"])
        self.assertNotIn("LONG OPERATION", tool["description"])

    def test_mcp_tool_does_not_assert_an_output_schema(self):
        tool = R.mcp_tool(R.spec_from_wire(CONTROL_ENTRY), 30.0)
        self.assertNotIn("outputSchema", tool)
        self.assertEqual(tool["meta"]["zene/result_schema"], CONTROL_ENTRY["result_schema"])

    def test_annotations_follow_mutating(self):
        mutating = R.mcp_tool(R.spec_from_wire(CONTROL_ENTRY), 30.0)["annotations"]
        read_only = R.mcp_tool(
            R.spec_from_wire(dict(CONTROL_ENTRY, id="mixer.get_state", mutating=False)), 30.0
        )["annotations"]
        self.assertFalse(mutating["read_only_hint"])
        self.assertTrue(mutating["destructive_hint"])
        self.assertTrue(read_only["read_only_hint"])
        self.assertFalse(read_only["destructive_hint"])

    def test_collision_is_refused_not_dropped(self):
        with self.assertRaises(C.ZeneControlError) as ctx:
            R.specs_from_commands([
                {"id": "a.b_c", "group": "a", "args_schema": {}, "result_schema": {}},
                {"id": "a.b.c", "group": "a", "args_schema": {}, "result_schema": {}},
            ])
        self.assertEqual(ctx.exception.kind, C.ErrorKind.BRIDGE_ERROR)
        self.assertIn("both map to MCP tool name", ctx.exception.message)

    def test_bridge_tool_name_collision_is_refused(self):
        with self.assertRaises(C.ZeneControlError):
            R.specs_from_commands([{"id": "commands", "group": "commands", "args_schema": {},
                                    "result_schema": {}}])

    def test_entry_without_id_is_refused(self):
        with self.assertRaises(C.ZeneControlError):
            R.spec_from_wire({"group": "x"})

    def test_bundle_round_trip_and_rejects_rubbish(self):
        tmp = tempfile.mkdtemp(prefix="zene-unit-", dir="/tmp")
        path = os.path.join(tmp, "commands.json")
        result = {"commands": [CONTROL_ENTRY], "count": 1, "proto": 1}
        bundle = R.bundle_from_result(result, {"version": "0.2.0-alpha"})
        self.assertEqual(bundle["count"], 1)
        self.assertEqual(R.save_bundle(path, bundle), path)
        loaded = R.load_bundle(path)
        self.assertIsNotNone(loaded)
        assert loaded is not None
        self.assertEqual(loaded["commands"][0]["id"], "mixer.set_volume")
        with open(path, "w", encoding="utf-8") as handle:
            handle.write("{ not json")
        self.assertIsNone(R.load_bundle(path))
        self.assertIsNone(R.load_bundle(os.path.join(tmp, "missing.json")))

    def test_bundle_from_result_requires_commands(self):
        with self.assertRaises(C.ZeneControlError):
            R.bundle_from_result({"count": 0}, {})


class TestConfig(unittest.TestCase):
    def test_default_socket_is_in_the_working_directory(self):
        cfg = C.parse_args(["--workdir", "/tmp/wd"])
        self.assertEqual(cfg.socket_path, "/tmp/wd/" + C.DEFAULT_SOCKET_NAME)
        self.assertTrue(os.path.isabs(cfg.socket_path))

    def test_precedence_cli_over_env(self):
        os.environ["ZENE_CONTROL_SOCKET"] = "/tmp/from-env.sock"
        try:
            self.assertEqual(C.parse_args([]).socket_path, "/tmp/from-env.sock")
            self.assertEqual(C.parse_args(["--socket", "/tmp/from-cli.sock"]).socket_path,
                             "/tmp/from-cli.sock")
        finally:
            del os.environ["ZENE_CONTROL_SOCKET"]

    def test_budgets_and_proto(self):
        cfg = C.parse_args(["--ready-timeout", "2", "--call-timeout", "1.5", "--proto", "2"])
        self.assertEqual(cfg.ready_timeout, 2.0)
        self.assertEqual(cfg.call_timeout, 1.5)
        self.assertEqual(cfg.proto, 2)
        self.assertEqual(cfg.timeout_for("mixer.get_state"), 1.5)
        self.assertEqual(cfg.timeout_for("render.render"), C.DEFAULT_LONG_CALL_TIMEOUT)
        self.assertEqual(cfg.timeout_for("render.render", 7), 7.0)

    def test_timeout_is_clamped(self):
        cfg = C.parse_args([])
        self.assertEqual(cfg.timeout_for("x", 10_000), C.MAX_CALL_TIMEOUT)
        # an explicit override is respected, then clamped into the safe band
        self.assertEqual(cfg.timeout_for("x", 0), C.MIN_CALL_TIMEOUT)
        self.assertEqual(C.clamp_timeout(0.0), C.MIN_CALL_TIMEOUT)

    def test_env_budgets_and_offline(self):
        os.environ["ZENE_CONTROL_CALL_TIMEOUT"] = "3"
        os.environ["ZENE_CONTROL_OFFLINE"] = "1"
        try:
            cfg = C.parse_args([])
            self.assertEqual(cfg.call_timeout, 3.0)
            self.assertTrue(cfg.offline)
        finally:
            del os.environ["ZENE_CONTROL_CALL_TIMEOUT"]
            del os.environ["ZENE_CONTROL_OFFLINE"]

    def test_bad_env_number_is_a_typed_error(self):
        os.environ["ZENE_CONTROL_CALL_TIMEOUT"] = "not-a-number"
        try:
            with self.assertRaises(C.ZeneControlError):
                C.parse_args([])
        finally:
            del os.environ["ZENE_CONTROL_CALL_TIMEOUT"]

    def test_engine_free_groups(self):
        self.assertTrue(C.engine_free("control"))
        self.assertFalse(C.engine_free("mixer"))
        self.assertFalse(C.engine_free("render"))


class TestTaxonomy(unittest.TestCase):
    def test_every_kind_has_a_hint_and_a_payload(self):
        kinds = [v for k, v in vars(C.ErrorKind).items() if k.isupper()]
        self.assertGreaterEqual(len(kinds), 12)
        for kind in kinds:
            self.assertIn(kind, C.HINTS, f"{kind} has no actionable hint")
            error = C.ZeneControlError(kind, "boom")
            payload = error.to_payload(command="a.b", elapsed_s=1.234)
            self.assertFalse(payload["ok"])
            self.assertEqual(payload["error"]["kind"], kind)
            self.assertTrue(payload["error"]["message"])
            self.assertTrue(payload["error"]["hint"])
            self.assertIn(payload["error"]["origin"], ("daw", "bridge"))
            self.assertIn("ERROR [", payload["summary"])

    def test_daw_kinds_are_the_documented_closed_set(self):
        self.assertEqual(C.DAW_ERROR_KINDS,
                         {"not_found", "requires", "invalid_args", "busy", "refused"})
        self.assertEqual(C.ZeneControlError("not_found", "x").to_payload()["error"]["origin"], "daw")
        self.assertEqual(C.ZeneControlError("no_instance", "x").to_payload()["error"]["origin"],
                         "bridge")

    def test_retryable_flags(self):
        self.assertTrue(C.ZeneControlError("timeout", "x").to_payload()["error"]["retryable"])
        self.assertFalse(C.ZeneControlError("invalid_args", "x").to_payload()["error"]["retryable"])

    def test_errno_mapping(self):
        self.assertEqual(_describe_oserror("/tmp/x", OSError(errno.ENOENT, "nope")).kind,
                         C.ErrorKind.NO_INSTANCE)
        self.assertEqual(_describe_oserror("/tmp/x", OSError(errno.ECONNREFUSED, "refused")).kind,
                         C.ErrorKind.STALE_SOCKET)
        self.assertEqual(_describe_oserror("/tmp/x", OSError(errno.ENOTSOCK, "not a socket")).kind,
                         C.ErrorKind.STALE_SOCKET)
        self.assertEqual(_describe_oserror("/tmp/x", OSError(errno.EACCES, "denied")).kind,
                         C.ErrorKind.BRIDGE_ERROR)



class TestOfflineSurfaces(unittest.TestCase):
    """Discoverability with no instance at all: it must still list commands."""

    def test_committed_snapshot_lists_the_daws_commands(self):
        bridge = T.Bridge(config_for())
        self.assertTrue(os.path.exists(bridge.snapshot_path()),
                        "the committed snapshot is what makes zene_commands work before an "
                        "instance exists; regenerate it with snapshot_commands.py")
        bundle = R.load_bundle(bridge.snapshot_path())
        self.assertIsNotNone(bundle)
        assert bundle is not None
        ids = {c["id"] for c in bundle["commands"]}
        for required in ("control.ping", "control.commands_list", "mixer.set_volume",
                         "project.get_state", "render.render"):
            self.assertIn(required, ids)
        self.assertEqual(bundle["instance"].get("proto"), 1)

    def test_commands_payload_works_with_no_instance_no_cache(self):
        cfg = config_for(state_dir=tempfile.mkdtemp(prefix="zene-unit-", dir="/tmp"))
        payload = T.Bridge(cfg).commands_payload({})
        self.assertTrue(payload["ok"])
        self.assertEqual(payload["source"], "snapshot")
        self.assertFalse(payload["live"])
        self.assertEqual(payload["instance_error"]["kind"], "no_instance")
        self.assertEqual(payload["naming"]["prefix"], "zene_")
        self.assertGreater(payload["count"], 0)

    def test_commands_payload_compact_mode(self):
        cfg = config_for(state_dir=tempfile.mkdtemp(prefix="zene-unit-", dir="/tmp"))
        payload = T.Bridge(cfg).commands_payload({"include_schemas": False})
        self.assertTrue(payload["commands"])
        self.assertNotIn("args_schema", payload["commands"][0])
        self.assertIn("tool_name", payload["commands"][0])

    def test_commands_payload_rejects_an_unknown_source(self):
        payload = T.Bridge(config_for()).commands_payload({"source": "nowhere"})
        self.assertFalse(payload["ok"])
        self.assertEqual(payload["error"]["kind"], C.ErrorKind.INVALID_ARGS)

    def test_explicit_live_source_fails_when_offline(self):
        cfg = config_for()
        with self.assertRaises(C.ZeneControlError) as ctx:
            T.Bridge(cfg).commands_payload({"source": "live"})
        self.assertEqual(ctx.exception.kind, C.ErrorKind.NO_INSTANCE)

    def test_status_always_answers(self):
        cfg = config_for()
        payload = T.Bridge(cfg).status_payload({})
        self.assertTrue(payload["ok"])
        self.assertFalse(payload["reachable"])
        self.assertIsNone(payload["engine_ready"])
        self.assertEqual(payload["error"]["kind"], C.ErrorKind.NO_INSTANCE)
        self.assertIn("zene-control", payload["summary"])
        self.assertEqual(payload["budgets"]["call_timeout_s"], C.DEFAULT_CALL_TIMEOUT)
        self.assertIn("command_list", payload)

    def test_offline_config_never_touches_the_socket(self):
        cfg = config_for(offline=True)
        payload = T.Bridge(cfg).commands_payload({"source": "auto"})
        self.assertTrue(payload["ok"])
        self.assertEqual(payload["source"], "snapshot")
        # offline means the socket was never probed, so there is no instance error
        self.assertNotIn("instance_error", payload)

    def test_find_spec_by_tool_reports_the_known_names(self):
        bridge = T.Bridge(config_for())
        with self.assertRaises(C.ZeneControlError) as ctx:
            bridge.find_spec_by_tool("zene_nope")
        self.assertEqual(ctx.exception.kind, C.ErrorKind.NOT_FOUND)
        self.assertIn("zene_mixer_set_volume", ctx.exception.message)

    def test_find_spec_engine_free_flag(self):
        bridge = T.Bridge(config_for())
        self.assertFalse(bridge.find_spec("control.ping").requires_engine)
        self.assertTrue(bridge.find_spec("mixer.get_state").requires_engine)

    def test_long_command_policy_is_named(self):
        self.assertIn("render.render", T.long_commands())
        self.assertEqual(T.long_commands()["render.render"], C.LONG_COMMANDS["render.render"])

    def test_resource_payloads_and_unknown_uri(self):
        from zene_control.server import BridgeServer
        server = BridgeServer(config_for(state_dir=tempfile.mkdtemp(prefix="zene-unit-", dir="/tmp")))
        uris = [r["uri"] for r in BridgeServer.resource_payloads()]
        self.assertEqual(uris, ["zene://project/state", "zene://instance/status"])
        for entry in BridgeServer.resource_payloads():
            self.assertEqual(entry["mimeType"], "application/json")
        with self.assertRaises(ValueError) as ctx:
            server.read_resource("zene://nope")
        self.assertIn("zene://project/state", str(ctx.exception))

    def test_instance_status_resource_works_offline(self):
        from zene_control.server import BridgeServer
        server = BridgeServer(config_for(state_dir=tempfile.mkdtemp(prefix="zene-unit-", dir="/tmp")))
        mime, text = server.read_resource("zene://instance/status")
        self.assertEqual(mime, "application/json")
        payload = json.loads(text)
        self.assertFalse(payload["reachable"])

    def test_tool_payloads_are_bridge_tools_plus_generated(self):
        from zene_control.server import BridgeServer
        server = BridgeServer(config_for(state_dir=tempfile.mkdtemp(prefix="zene-unit-", dir="/tmp")))
        payloads = server.tool_payloads()
        names = [p["name"] for p in payloads]
        self.assertEqual(names[:2], ["zene_commands", "zene_status"])
        generated = [p for p in payloads if p["meta"].get("zene/command")]
        self.assertGreater(len(generated), 0)
        for entry in generated:
            self.assertIn("timeout_s", entry["inputSchema"]["properties"])
            self.assertEqual(entry["meta"]["zene/command"],
                             entry["annotations"]["title"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
