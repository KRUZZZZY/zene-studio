#!/usr/bin/env python3
"""The shared plumbing of the two-instance session-sync proof
(tests/control-link-sync.py): the evidence transcript, the hash helper, and the
Peer wrapper around one real instance.

Split out of the test for this fork's file-length ratchet (Gate 7, 500 lines per
file) - the same split, and the same reason, as tests/control_socket_harness.py
and tests/control_socket_flows.py. Nothing here asserts anything: the checks live
in the test script, so a reader of that file sees the whole proof without the
plumbing around it.

Usage: imported by tests/control-link-sync.py, which owns its own argv.
"""

import hashlib
import json
import os

import control_socket_harness as H

#: ctest's "Skipped" code. The test returns it - never 0 - when this host cannot
#: carry announcements, so a skip can never be mistaken for a pass.
SKIP_EXIT = 77


class Evidence:
    """The transcript: every exchange, then the derived findings."""

    def __init__(self):
        self.lines = []

    def line(self, text=""):
        self.lines.append(text)
        print(text, flush=True)

    def section(self, title):
        self.line("")
        self.line("=" * 78)
        self.line(title)
        self.line("=" * 78)

    def finding(self, label, text):
        self.line("  %-30s %s" % (label, text))

    def write(self, path, verdict):
        if not path:
            return
        with open(path, "w", encoding="utf-8") as handle:
            handle.write("\n".join(self.lines) + "\n")
            handle.write("\nVERDICT: %s\n" % verdict)


def sha256_of(path):
    try:
        with open(path, "rb") as handle:
            return hashlib.sha256(handle.read()).hexdigest()
    except OSError:
        return ""


class Peer:
    """One real instance, its control client, and the last state read from it.

    Every assertion in the test is made against `state`, which is only ever what
    the instance itself answered for link.get_state - never against the reply of
    the command under test.
    """

    def __init__(self, label, instance):
        self.label = label
        self.instance = instance
        self.client = None
        self.state = {}

    def call(self, request_id, cmd, args=None):
        return self.client.call(request_id, cmd, args)

    def ok(self, request_id, cmd, args=None):
        return H.ok_result(self.call(request_id, cmd, args), request_id)

    def refresh(self, request_id):
        """Read link.get_state and keep it, so `sees`/`peer_id` are its state."""
        self.state = self.ok(request_id, "link.get_state")
        return self.state

    def peer_id(self):
        return self.state.get("peer_id", "")

    def sees(self, other_peer_id):
        return any(peer.get("id") == other_peer_id for peer in self.state.get("peers", []))


def json_line(value):
    """One compact line for the transcript, so the evidence is readable."""
    return json.dumps(value, separators=(",", ":"))


def binary_is_there(path):
    return os.path.exists(path)


#: How long a loopback probe may take before this host is declared unable to
#: receive an announcement. Generous next to the measured loopback time (well
#: under a millisecond) and short enough that a broken host costs one probe.
LOOPBACK_BOUND_MS = 250


def host_multicast_loopback(group="224.76.78.75", port=20808, bound_ms=None):
    """MEASURE this host: does a datagram sent to `group` arrive at a SECOND
    socket here that bound the port and joined the group?

    Deliberately a SECOND socket and not a send-to-self: a kernel that loops a
    datagram back to its own SENDER has not shown that any other socket gets it,
    and the two-instance proof needs exactly that. The socket recipe matches
    LinkUdpTransport::joinGroup() - SO_REUSEADDR + SO_REUSEPORT, bind(INADDR_ANY),
    IP_ADD_MEMBERSHIP(group, INADDR_ANY).

    Returns (delivered, elapsed_ms, problem). `problem` is non-empty when a socket
    could not be CONFIGURED at all, which is the carry half rather than the
    receive half.
    """
    import socket
    import struct
    import time

    bound = LOOPBACK_BOUND_MS if bound_ms is None else bound_ms

    def make():
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        if hasattr(socket, "SO_REUSEPORT"):
            try:
                sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
            except OSError:
                pass
        sock.bind(("", port))
        membership = struct.pack("4s4s", socket.inet_aton(group), socket.inet_aton("0.0.0.0"))
        sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, membership)
        sock.settimeout(bound / 1000.0)
        return sock

    sender = None
    receiver = None
    try:
        sender = make()
        receiver = make()
    except OSError as error:
        for sock in (sender, receiver):
            if sock is not None:
                sock.close()
        return False, -1, "%s" % error

    payload = b"zene-link-host-loopback-probe"
    started = time.monotonic()
    sender.sendto(payload, (group, port))
    delivered = False
    try:
        while not delivered:
            data, _ = receiver.recvfrom(2048)
            delivered = data == payload
    except OSError:
        delivered = False
    elapsed = int((time.monotonic() - started) * 1000)
    sender.close()
    receiver.close()
    return delivered, (elapsed if delivered else -1), ""
