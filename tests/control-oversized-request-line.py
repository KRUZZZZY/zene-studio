#!/usr/bin/env python3
"""A COMPLETE but over-cap request line is refused, never dispatched.

Sibling coverage: tests/control-socket-path-safety.py sends junk with NO newline at
all, which `ControlServer::dispatchPendingLines`'s never-ended branch already caught.
That left one shape unreached: a valid request PADDED past
`ControlServer::MaxRequestLineBytes` with whitespace inside the JSON and then
TERMINATED, so the line is COMPLETE at the moment the read loop stops appending.

Both transports dispatched such a line: the read loop's `MaxRequestLineBytes` guard
bounds the BUFFER, not the line, so the terminal newline made an over-cap line look
ordinary and it ran as a real command. This pins that both refuse it instead, typed
`invalid_args` naming the cap with id -1, and that the padded request is never
executed (its own id never answers ok).

Its own file, not a case in control-socket-path-safety.py: that script is at the
per-file length ratchet, and the harness/lib split it already uses keeps this to the
imports it shares.

Usage:
  QT_QPA_PLATFORM=offscreen python3 control-oversized-request-line.py <lmms>
Exit code 0 only when the case passed.
"""

import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from control_socket_harness import (  # noqa: E402
    Blocked, Problems, Timeout, Transcript, connect, finish, start_instance, wait_ready,
)
from control_socket_flows import read_cap_refusal  # noqa: E402

# The cap in include/ControlServer.h (ControlServer::MaxRequestLineBytes).
REQUEST_LINE_CAP = 1024 * 1024
READY_TIMEOUT = 90.0

# The padded request's own id: if it were dispatched this is what would answer ok.
PADDED_ID = 4242


def padded_request():
    """A valid control.ping, padded INSIDE the JSON past the cap, then terminated."""
    return json.dumps({"id": PADDED_ID, "cmd": "control.ping", "args": {}, "proto": 1,
                       "pad": " " * REQUEST_LINE_CAP}, separators=(",", ":"))


def assert_not_dispatched(reply, problems):
    """Typed invalid_args refusal, id -1, and no sign the padded request ran."""
    error = reply.get("error") or {}
    if reply.get("ok") is not False or error.get("kind") != "invalid_args":
        problems.add("an over-cap COMPLETE request line answered %r, expected a typed "
                     "invalid_args refusal: the line was dispatched instead of refused"
                     % reply)
    if str(REQUEST_LINE_CAP) not in (error.get("message") or ""):
        problems.add("the refusal does not name the %d-byte cap: %r"
                     % (REQUEST_LINE_CAP, error))
    if reply.get("id") == PADDED_ID or reply.get("ok") is True:
        problems.add("the over-cap request was EXECUTED and answered %r: it must never "
                     "reach dispatch" % reply)


def assert_retired_not_buffered(client, problems):
    """Retired, not buffered: after the cap a well-formed request gets no reply."""
    try:
        quiet = client.call(7, "control.ping", timeout=5.0)
    except Timeout as no_reply:
        print("no reply to a request sent after the over-cap line (expected): %s" % no_reply)
        return
    problems.add("the connection is still serving requests after an over-cap line: it "
                 "answered %r, so the line was buffered rather than capped" % quiet)


def case_oversized_complete_line(binary):
    """An over-cap line that ENDS (newline in the same read) is refused, not dispatched."""
    name = "an over-cap COMPLETE request line is refused, not dispatched"
    problems = Problems()
    instance = start_instance(binary)
    try:
        client = connect(instance)
        try:
            wait_ready(instance, client, Transcript(), seconds=READY_TIMEOUT)
            # Needs a server DRAINING this socket: the socket is serviced only while the
            # event loop runs, and a sendall against a non-reading server blocks.
            client.sock.sendall((padded_request() + "\n").encode("utf-8"))
        except (Blocked, Timeout, OSError) as error:
            problems.add("the over-cap complete line could not be exercised: %s" % error)
            return name, problems.report(name), problems.items
        reply = read_cap_refusal(client, problems)
        if reply is not None:
            assert_not_dispatched(reply, problems)
            assert_retired_not_buffered(client, problems)
        client.close()
    finally:
        instance.close()
    return name, problems.report(name), problems.items


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    binary = os.path.abspath(sys.argv[1])
    if not os.path.exists(binary):
        print("FAIL: no such file: %s" % binary)
        return 1
    return finish([case_oversized_complete_line(binary)])


if __name__ == "__main__":
    sys.exit(main())