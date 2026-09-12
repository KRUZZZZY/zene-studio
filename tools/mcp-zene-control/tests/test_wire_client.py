#!/usr/bin/env python3
"""Unit tests for the wire client: real AF_UNIX sockets, no DAW binary, no instance.

    python3 -m unittest tests.test_wire_client -v
    python3 tests/test_wire_client.py

This module is the **wire client's own concern** (`zene_control.protocol`): the
four lifecycle cases the task calls out (no instance, stale socket, mid-flight
disconnect, protocol mismatch) plus the bounded-timeout, readiness-probe and
error-kind passthrough behaviour, each against a real socket. The broken servers
are `harness.StubDaw` policies, so every case is deterministic rather than racy.

Naming, tool generation, configuration, the error taxonomy and the offline
surfaces (no socket involved) are `tests/test_units.py`.
"""
from __future__ import annotations

import os
import socket
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
BRIDGE_DIR = HERE.parent
sys.path.insert(0, str(BRIDGE_DIR))

try:
    from . import harness as H
except ImportError:
    import harness as H  # type: ignore[no-redef]

import zene_control.config as C  # noqa: E402
from zene_control.protocol import ControlClient  # noqa: E402


class TestWireClient(unittest.TestCase):
    """Real sockets, no DAW: the lifecycle cases the task calls out."""

    def test_no_instance(self):
        path = os.path.join(tempfile.mkdtemp(prefix="zene-unit-", dir="/tmp"), "nope.sock")
        with self.assertRaises(C.ZeneControlError) as ctx:
            ControlClient(path, 1.0).connect()
        self.assertEqual(ctx.exception.kind, C.ErrorKind.NO_INSTANCE)

    def test_stale_socket(self):
        tmp = tempfile.mkdtemp(prefix="zene-unit-", dir="/tmp")
        path = os.path.join(tmp, "stale.sock")
        listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        listener.bind(path)
        listener.close()
        self.assertTrue(os.path.exists(path))
        with self.assertRaises(C.ZeneControlError) as ctx:
            ControlClient(path, 1.0).connect()
        self.assertEqual(ctx.exception.kind, C.ErrorKind.STALE_SOCKET)
        self.assertIn("nothing is listening", ctx.exception.message)

    def test_plain_file_is_not_a_socket(self):
        tmp = tempfile.mkdtemp(prefix="zene-unit-", dir="/tmp")
        path = os.path.join(tmp, "plain.sock")
        with open(path, "w", encoding="utf-8") as handle:
            handle.write("i am not a socket")
        with self.assertRaises(C.ZeneControlError) as ctx:
            ControlClient(path, 1.0).connect()
        self.assertEqual(ctx.exception.kind, C.ErrorKind.STALE_SOCKET)

    def test_relative_path_is_refused_before_connecting(self):
        with self.assertRaises(C.ZeneControlError) as ctx:
            ControlClient("relative.sock", 1.0).connect()
        self.assertEqual(ctx.exception.kind, C.ErrorKind.BRIDGE_ERROR)

    def test_mid_request_disconnect(self):
        with H.StubDaw(policy="close") as stub:
            with ControlClient(stub.path, 5.0) as client:
                with self.assertRaises(C.ZeneControlError) as ctx:
                    client.request("control.ping")
            self.assertEqual(ctx.exception.kind, C.ErrorKind.DISCONNECTED)

    def test_bounded_timeout_on_a_silent_instance(self):
        with H.StubDaw(policy="silent") as stub:
            client = ControlClient(stub.path, 0.5)
            client.connect()
            try:
                started = __import__("time").monotonic()
                with self.assertRaises(C.ZeneControlError) as ctx:
                    client.request("control.ping")
                elapsed = __import__("time").monotonic() - started
            finally:
                client.close()
            self.assertEqual(ctx.exception.kind, C.ErrorKind.TIMEOUT)
            self.assertLess(elapsed, 3.0)
            self.assertTrue(stub.commands_seen())

    def test_proto_mismatch_from_ping(self):
        with H.StubDaw(policy="ping", proto=7) as stub:
            with ControlClient(stub.path, 2.0, proto=1) as client:
                with self.assertRaises(C.ZeneControlError) as ctx:
                    client.handshake(wait_ready=False)
            self.assertEqual(ctx.exception.kind, C.ErrorKind.PROTO_MISMATCH)
            self.assertIn("proto 7", ctx.exception.message)

    def test_readiness_probe_is_sent_without_a_proto_member(self):
        with H.StubDaw(policy="ping", proto=1) as stub:
            with ControlClient(stub.path, 2.0, proto=1) as client:
                client.handshake(wait_ready=False)
            seen = stub.seen
            self.assertTrue(seen)
            for request in seen:
                self.assertNotIn("proto", request,
                                 "the probe must be proto-less so any version answers it")

    def test_proto_mismatch_is_detected_despite_the_daws_refusal(self):
        """The DAW refuses a foreign proto; the proto-less probe still learns it."""
        with H.StubDaw(policy="daw_like", proto=3) as stub:
            with ControlClient(stub.path, 2.0, proto=1) as client:
                with self.assertRaises(C.ZeneControlError) as ctx:
                    client.handshake(wait_ready=False)
            self.assertEqual(ctx.exception.kind, C.ErrorKind.PROTO_MISMATCH)
            self.assertIn("proto 3", ctx.exception.message)
            self.assertIn("proto 1", ctx.exception.message)
            self.assertIsNone(ctx.exception.detail.get("daw_error"))
            # and the client is not left thinking it can proceed
            self.assertEqual(int(ctx.exception.detail["instance_proto"]), 3)

    def test_matching_proto_still_works_with_the_daw_like_stub(self):
        with H.StubDaw(policy="daw_like", proto=3) as stub:
            with ControlClient(stub.path, 2.0, proto=3) as client:
                ping = client.handshake(wait_ready=False)
            self.assertEqual(int(ping["proto"]), 3)
            self.assertTrue(ping["pong"])

    def test_engine_never_ready_is_bounded_and_sends_only_pings(self):
        with H.StubDaw(policy="ping", engine_ready=False) as stub:
            started = __import__("time").monotonic()
            with ControlClient(stub.path, 5.0, proto=1) as client:
                with self.assertRaises(C.ZeneControlError) as ctx:
                    client.handshake(wait_ready=True, ready_timeout=0.6)
            elapsed = __import__("time").monotonic() - started
            self.assertEqual(ctx.exception.kind, C.ErrorKind.NOT_READY)
            self.assertLess(elapsed, 3.0)
            self.assertEqual(set(stub.commands_seen()), {"control.ping"})

    def test_engine_free_handshake_does_not_wait(self):
        with H.StubDaw(policy="ping", engine_ready=False) as stub:
            started = __import__("time").monotonic()
            with ControlClient(stub.path, 5.0, proto=1) as client:
                ping = client.handshake(wait_ready=False)
            self.assertFalse(ping["engine_ready"])
            self.assertLess(__import__("time").monotonic() - started, 1.0)

    def test_daw_error_kind_passes_through(self):
        with H.StubDaw(policy="ping") as stub:
            with ControlClient(stub.path, 5.0) as client:
                with self.assertRaises(C.ZeneControlError) as ctx:
                    client.request("mixer.get_state")
            self.assertEqual(ctx.exception.kind, C.ErrorKind.BUSY)

    def test_unknown_error_kind_is_not_trusted(self):
        with H.StubDaw(policy="weird") as stub:
            with ControlClient(stub.path, 5.0) as client:
                with self.assertRaises(C.ZeneControlError) as ctx:
                    client.request("mixer.get_state")
            self.assertEqual(ctx.exception.kind, C.ErrorKind.BRIDGE_ERROR)


if __name__ == "__main__":
    unittest.main(verbosity=2)
