#!/usr/bin/env python3
"""The control surface's input validation and connection bounds, proved on the wire.

FOUR DEFECTS, four cases, each with the assertion that goes red without its fix.
Everything here is driven through a REAL instance over `--control-socket`: the
claims are about what a client sees on the wire, so a socket transcript is the
only honest instrument.

  BUG-CTL-2 (src/core/ControlDeviceSupport.cpp, resolveChannelTarget).
    A channel target id that is not of the ch-<index> form used to fall through
    the "is this channel there?" test and be answered not_found, which is a lie:
    `ch-foo` names nothing and could never name anything. It is now invalid_args.
    A well-formed but absent id (`ch-9999`) stays not_found - the two failures are
    different and a fix that merged them would be a different bug. Driven through
    two channel-targeting commands that resolve the target before they touch a
    device (`plugin.param_get`, `chain.save`), so the refusal under test is the
    resolver's own and not a downstream "no such plugin".

  BUG-SRV-1 (src/core/ControlServer.cpp, dispatchLine).
    A request `id` was cast straight to int. 1e300, -1e300, 2.5 and INT_MAX+1 all
    became some int and the request was SERVED. Now the id must be finite,
    integral and inside the signed 32-bit range, or the request is refused
    invalid_args - and the instance keeps serving afterwards, because a bad id is
    one request's problem and not a connection's.

  BUG-SRV-2a (MaxClients). Past 64 simultaneous local peers the excess
    connections are accepted and closed at once. Leaving them in the listener
    backlog let one local user hold it indefinitely. The peer sees a close, not
    silence: a "refusal" that hung would be the same bug wearing a hat.

  BUG-SRV-2b (ClientIdleTimeoutMs). A request line carries a 30 s deadline from
    the moment its peer was accepted. A PARTIAL read does not extend it; a
    completed exchange does. Both halves are measured on ONE timeline (see
    check_idle_deadline) so each is evidence for the other rather than two
    separate stopwatch readings.

Usage:
    QT_QPA_PLATFORM=offscreen python3 control-surface-hardening.py <zene-binary>
Exit codes: 0 every assertion passed; 1 a failed assertion; 2 cannot run.
"""

from __future__ import annotations

import json
import os
import socket
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent

sys.path.insert(0, str(HERE))

from control_socket_harness import (  # noqa: E402
    Client, Problems, Timeout, finish, start_instance, wait_for_socket, wait_ready,
)

#: The two bounds this test measures and the header that declares them. The
#: numbers are ASSERTED against that text (declared_bounds) rather than trusted:
#: a change to either constant is a change to this test's premise, and a run that
#: silently kept measuring the old number would be worse than a red one.
MAX_CLIENTS = 64
IDLE_TIMEOUT_MS = 30000
HEADER = REPO / "include" / "ControlServer.h"

#: One probe read's bound. A healthy instance answers in milliseconds; this is
#: generous enough for a loaded build box and small enough that a hang costs
#: seconds. A HANG IS A FAILURE, never a skip.
PROBE_TIMEOUT = 15.0

#: How far past the declared cap the cap case goes. 16 is the listener's own
#: backlog (`listen(fd, 16)`), so the excess cannot all be sitting in the kernel
#: queue at once: the refusal has to happen on the server side.
CAP_OVERFLOW = 16

#: The deadline case's timeline. The deadline is 30 s and the sweep runs every
#: second (ControlServer::m_clientSweep, 1000 ms), so a peer whose deadline has
#: passed is gone by 31 s; the assertions are taken at ~35 s. The refreshed
#: peer's second exchange is at 20 s, which pushes ITS deadline to 50 s.
MID_WAIT = 20.0
FINAL_WAIT = 15.0


def declared_bounds(header_text):
    """Problems if the header no longer declares the two bounds this test uses."""
    problems = Problems()
    for needle in ("MaxClients = %d" % MAX_CLIENTS,
                   "ClientIdleTimeoutMs = %d" % IDLE_TIMEOUT_MS):
        problems.require(needle in header_text,
                         "include/ControlServer.h no longer declares '%s': this test measures "
                         "a bound the header does not state" % needle)
    return problems


def read_line(sock, timeout):
    """One reply line, or None when the peer closed. Raises Timeout on the bound."""
    sock.settimeout(timeout)
    buffer = b""
    while b"\n" not in buffer:
        try:
            chunk = sock.recv(65536)
        except socket.timeout:
            raise Timeout("no reply line inside %.1fs" % timeout)
        except OSError:
            return None
        if not chunk:
            return None
        buffer += chunk
    return buffer.split(b"\n", 1)[0].decode("utf-8", "replace")


def exchange(sock, payload, timeout=PROBE_TIMEOUT):
    """Send one line, read one line. Returns (reply_or_None, outcome).

    outcome is "reply" (a line came back), "closed" (the peer closed without
    answering - what the server does to a refused peer) or "hang" (neither inside
    the bound - the failure mode a refusal must NOT have).
    """
    try:
        sock.sendall(payload.encode("utf-8") + b"\n")
    except OSError:
        return None, "closed"
    try:
        line = read_line(sock, timeout)
    except Timeout:
        return None, "hang"
    if line is None:
        return None, "closed"
    return json.loads(line), "reply"


def ping(request_id):
    return json.dumps({"id": request_id, "cmd": "control.ping", "args": {}, "proto": 1},
                      separators=(",", ":"))


def connect(instance, deadline=None):
    """One connection, retried against a full listener backlog.

    `listen(fd, 16)` is the listener's backlog, and the cap case opens more
    sockets than that on purpose: a connect that lands while 16 are queued and
    unaccepted fails with EAGAIN (BlockingIOError) rather than blocking. That is
    the kernel refusing to queue more, NOT the product refusing the peer - the
    product's refusal is the close this test measures - so the client retries
    until the server's accept loop has drained the queue. Bounded: a backlog that
    never drains is a failure, not a wait.
    """
    stop = time.time() + (PROBE_TIMEOUT if deadline is None else deadline)
    while True:
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.settimeout(PROBE_TIMEOUT)
        try:
            sock.connect(instance.socket_path)
            return sock
        except BlockingIOError:
            sock.close()
            if time.time() > stop:
                raise Timeout("the listener backlog never drained within %.1fs" % PROBE_TIMEOUT)
            time.sleep(0.02)


#: The channel-targeting commands the malformed/absent id is driven through. Each
#: resolves its target first, so a channel refusal is the resolver's own answer.
CHANNEL_TARGETS = (
    ("plugin.param_get", lambda target: {"target": target, "plugin": "inst"}),
    ("chain.save", lambda target: {"target": target, "name": "hardening-probe"}),
)


def check_channel_targets(client):
    """BUG-CTL-2: `ch-foo` is invalid_args, `ch-9999` is not_found, on both commands."""
    problems = Problems()
    for command, args_of in CHANNEL_TARGETS:
        for target, expected in (("ch-foo", "invalid_args"), ("ch-9999", "not_found")):
            reply = client.call(1, command, args_of(target))
            kind = (reply.get("error") or {}).get("kind")
            problems.require(reply.get("ok") is False and kind == expected,
                             "%s target %s must be %s, got %r" % (command, target, expected, reply))
    return problems


#: The malformed ids the fix must refuse, as they appear on the wire.
#: 1e300 / -1e300 / 2.5 / INT_MAX+1 reach the new id check. `NaN` and `"1"` are
#: refused EARLIER - Qt's JSON parser rejects the NaN literal ("illegal number")
#: and a string id fails the isDouble() test - so their kind is the same
#: invalid_args but their message is a different one. The kind is asserted for
#: every id; the id-check MESSAGE only where the id check is the branch that
#: produced it, so the test cannot claim the new code ran when the parser had
#: already refused the line. (The same measurement is why the fix's
#: `!std::isfinite` clause is unreachable from the wire: see the report.)
MALFORMED_IDS = ("1e300", "-1e300", "2.5", "2147483648", "NaN", '"1"')
ID_CHECKED = ("1e300", "-1e300", "2.5", "2147483648")


def check_request_ids(instance, client):
    """BUG-SRV-1: a non-integral or out-of-range id is refused; the instance keeps serving."""
    problems = Problems()
    for index, literal in enumerate(MALFORMED_IDS):
        sock = connect(instance)
        try:
            payload = '{"id":%s,"cmd":"control.ping","args":{},"proto":1}' % literal
            reply, outcome = exchange(sock, payload)
        finally:
            sock.close()
        if outcome != "reply":
            problems.add("id %s: the instance answered nothing (%s)" % (literal, outcome))
            continue
        error = reply.get("error") or {}
        problems.require(reply.get("ok") is False and error.get("kind") == "invalid_args",
                         "id %s must be refused invalid_args, got %r" % (literal, reply))
        if literal in ID_CHECKED:
            problems.require("signed 32-bit range" in (error.get("message") or ""),
                             "id %s must be refused by the id check, got %r" % (literal, reply))
    # A bad id is one request's problem, not the connection's: the instance serves on.
    reply = client.call(99, "control.ping")
    problems.require(reply.get("ok") is True,
                     "the instance stopped serving after the refusals: %r" % reply)
    return problems


def classify(sockets):
    """(accepted, refused, hung) for a batch of connected sockets, each pinged once."""
    counts = {"reply": 0, "closed": 0, "hang": 0}
    for index, sock in enumerate(sockets):
        reply, outcome = exchange(sock, ping(index + 1))
        if outcome == "reply" and reply.get("ok") is True:
            counts["reply"] += 1
        else:
            counts[outcome] += 1
    return counts["reply"], counts["closed"], counts["hang"]


def check_client_cap(instance, readiness_client):
    """BUG-SRV-2a: past MaxClients the excess peers are closed at once, the rest serve.

    The readiness client is still connected and holds one slot, so `accepted` is
    measured against MaxClients - 1 when the server still counts it and
    MaxClients when it does not. The assertions allow exactly that one slot and no
    more: the cap is enforced at the declared number, never above it, and the
    refusal is a close rather than a hang.
    """
    problems = Problems()
    sockets = []
    try:
        for _ in range(MAX_CLIENTS + CAP_OVERFLOW):
            sockets.append(connect(instance))
        # The accept loop drains the whole backlog in one activation, so the burst
        # needs no per-socket handshake; this bound only waits for the notifier.
        time.sleep(1.0)
        accepted, refused, hung = classify(sockets)
        problems.require(hung == 0,
                         "%d excess peer(s) were left hanging instead of refused" % hung)
        problems.require(refused >= 1,
                         "no peer was refused: the %d-client cap is not enforced" % MAX_CLIENTS)
        problems.require(accepted <= MAX_CLIENTS,
                         "%d peers were accepted, above the declared cap of %d"
                         % (accepted, MAX_CLIENTS))
        problems.require(accepted >= MAX_CLIENTS - 1,
                         "only %d peers were accepted: the cap is smaller than the declared %d"
                         % (accepted, MAX_CLIENTS))
        # A session established before the burst is unaffected by the refusals.
        reply = readiness_client.call(4242, "control.ping")
        problems.require(reply.get("ok") is True,
                         "the session connected before the burst stopped answering: %r" % reply)
    finally:
        for sock in sockets:
            sock.close()
    return problems


def stalled_outcome(stalled):
    """Read-only probe of the stalled peer: "closed", "reply", or "hang" on the bound."""
    try:
        return "closed" if read_line(stalled, PROBE_TIMEOUT) is None else "reply"
    except Timeout:
        return "hang"


def check_idle_deadline(instance):
    """BUG-SRV-2b: the deadline is absolute per request line, and an exchange refreshes it.

    Two peers on ONE timeline, so each half of the claim is the other's control:

      * `stalled` sends a partial line at t=0 and another at t=20 and NOTHING
        else. Its deadline was accept+30 s and no partial read extends it, so the
        sweep retires it by ~31 s and it must be gone at t=35. If a partial read
        DID extend the deadline, its t=20 write would push expiry to t=50 and the
        peer would still be alive here.
      * `active` completes an exchange at t=0 and another at t=20. The t=20 reply
        refreshes its deadline to t=50, so at t=35 it is still there and answers
        again. Without that refresh it would have died at t=30 beside `stalled`.

    This case costs ~35 s of wall clock: the bound is 30 s, the product has no
    shortened override, and the only honest way to observe expiry is to wait for
    it. A hang is a failure, never a skip.
    """
    problems = Problems()
    stalled = connect(instance)
    active = connect(instance)
    try:
        stalled.sendall(b'{"id":11,"cmd":"control.ping","args":{},')
        reply, outcome = exchange(active, ping(1))
        problems.require(outcome == "reply" and reply.get("ok") is True,
                         "the active peer's first exchange failed: %r (%s)" % (reply, outcome))
        time.sleep(MID_WAIT)
        stalled.sendall(b'"proto":1}')
        reply, outcome = exchange(active, ping(2))
        problems.require(outcome == "reply" and reply.get("ok") is True,
                         "the active peer's second exchange failed: %r (%s)" % (reply, outcome))
        time.sleep(FINAL_WAIT)
        outcome = stalled_outcome(stalled)
        problems.require(outcome == "closed",
                         "the stalled peer was not retired by the %.0fs deadline (%s)"
                         % (IDLE_TIMEOUT_MS / 1000.0, outcome))
        reply, outcome = exchange(active, ping(3))
        problems.require(outcome == "reply" and reply.get("ok") is True,
                         "the refreshed peer was retired too - a completed exchange does not "
                         "extend the deadline (%s, %r)" % (outcome, reply))
    finally:
        stalled.close()
        active.close()
    return problems


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    if len(args) != 1:
        print(__doc__)
        return 2
    binary = os.path.abspath(args[0])
    if not os.path.exists(binary):
        print("FAIL: no zene binary at %s" % binary)
        return 2

    results = []
    problems = declared_bounds(HEADER.read_text(encoding="utf-8"))
    results.append(("declared bounds (include/ControlServer.h)", not problems, problems.items))
    if problems:
        # Every case measures against these numbers: a premise that no longer
        # holds must stop the run rather than produce a misleading red.
        return finish(results)

    instance = start_instance(binary)
    try:
        wait_for_socket(instance, 120)
        client = Client(instance.socket_path)
        wait_ready(instance, client, None)
        cases = (
            ("channel targets (BUG-CTL-2)", lambda: check_channel_targets(client)),
            ("request id validation (BUG-SRV-1)",
             lambda: check_request_ids(instance, client)),
            ("client cap (BUG-SRV-2)", lambda: check_client_cap(instance, client)),
            ("idle deadline (BUG-SRV-2)", lambda: check_idle_deadline(instance)),
        )
        for name, check in cases:
            print("\n-- %s" % name)
            problems = check()
            results.append((name, not problems, problems.items))
        client.close()
    finally:
        instance.close()
    return finish(results)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
