#!/usr/bin/env python3
"""Q050-334.1: ordered, bounded project-change events over a REAL control socket.

Drives a real `zene` through three connections: the subscribers read the stream, an editor (never subscribed) makes
the edits. Proves: subscribers receive successful project changes in order, failed edits and reads make no event,
unsubscribe stops further events; a client that stops reading is bounded and gets a detectable overflow notice that
names what it missed; a disconnect is detectable from the seq a new subscription answers; in both cases rereading the
project shows the current state and a new subscription resumes. An idle subscriber outlives the idle timeout.

Usage: QT_QPA_PLATFORM=offscreen python3 tests/control-project-events.py <zene>
Exit code 0 only when every check passed.
"""

import json
import sys
import time

import control_socket_harness as H

IDLE_TIMEOUT_S = 30  # ControlServer::ClientIdleTimeoutMs


class Conn(H.Client):
    """A client that keeps event lines apart from replies, so the order on the wire can be asserted."""

    def __init__(self, path):
        super().__init__(path)
        self.next_id = 1000
        self.events = []

    def send(self, cmd, args=None):
        self.next_id += 1
        raw = json.dumps({"id": self.next_id, "cmd": cmd, "args": args or {}, "proto": 1}, separators=(",", ":"))
        self.sock.sendall(raw.encode() + b"\n")
        return self.next_id

    def read(self, timeout=30.0):
        return json.loads(self._read_line(timeout).decode())

    def reply(self, request_id, timeout=60.0):
        """The reply to request_id; any event line before it is kept in self.events."""
        while True:
            line = self.read(timeout)
            if "event" in line:
                self.events.append(line)
            elif line.get("id") == request_id:
                return line
            else:
                raise AssertionError("unexpected line: %r" % (line,))

    def ok(self, cmd, args=None):
        answer = self.reply(self.send(cmd, args))
        if answer.get("ok") is not True:
            raise AssertionError("%s failed: %r" % (cmd, answer))
        return answer["result"]

    def error(self, cmd, args, kind):
        answer = self.reply(self.send(cmd, args))
        H.typed_error(answer, answer.get("id"), kind)
        return answer

    def wait_events(self, count):
        """Read until `count` events were kept; a barrier ping afterwards proves nothing else is on its way."""
        deadline = time.time() + 30
        while len(self.events) < count:
            if time.time() > deadline:
                raise AssertionError("only %d of %d events arrived: %r" % (len(self.events), count, self.events))
            self.events.append(self.read())
        return self.events


def drain_barrier(conn):
    """Lets every event already queued arrive: the instance runs one turn for each ping, then flushes."""
    for _ in range(3):
        conn.ok("control.ping")
        time.sleep(0.05)
    conn.ok("control.ping")


def track_count(conn):
    return len(conn.ok("project.get_state")["tracks"])


def add_track(editor, name):
    return editor.ok("track.add", {"type": "instrument", "name": name})


def check(condition, text):
    if not condition:
        raise AssertionError(text)
    H.ok(text)


def check_subscribe_replies(a, b, first, editor):
    check(a["queue_limit"] == 256 and a["session"] == b["session"] and a["seq"] == b["seq"],
          "subscribe reports the default bound, the session and the current seq")
    first.error("events.subscribe", {}, "busy")
    editor.error("events.subscribe", {"queue_limit": 0}, "invalid_args")


def make_ordered_changes(first, editor):
    """Five successful changes, with a refused edit and a read between them. Returns the track they used."""
    added = add_track(editor, "event one")
    editor.ok("track.rename", {"track": added["track"], "name": "event two"})
    # A refused edit and a read change nothing, so neither is an event.
    editor.error("track.rename", {"track": "trk-999999", "name": "x"}, "not_found")
    editor.ok("project.get_state")
    # A subscriber's OWN edit is an event too, and it follows that edit's reply.
    first.wait_events(2)
    request = first.send("track.rename", {"track": added["track"], "name": "event three"})
    check(first.read().get("id") == request, "a subscriber's own edit: the reply comes before its event")
    first.wait_events(3)
    editor.ok("control.undo")
    editor.ok("control.redo")
    return added["track"]


def check_event_sequence(first, second, start_seq):
    """The five changes arrived at both subscribers in order. Returns their seqs."""
    first.wait_events(5)
    second.wait_events(5)
    drain_barrier(first)
    check(len(first.events) == 5, "exactly the five successful changes arrived, nothing for the refusal or the read")
    commands = [event["command"] for event in first.events]
    check(commands == ["track.add", "track.rename", "track.rename", "control.undo", "control.redo"],
          "the events carry the commands in mutation order: %s" % commands)
    check([event["kind"] for event in first.events] == ["edit", "edit", "edit", "undo", "redo"],
          "edit, undo and redo are told apart")
    seqs = [event["seq"] for event in first.events]
    check(seqs == list(range(start_seq + 1, start_seq + 6)), "seq is strictly increasing with no gap: %s" % seqs)
    check([event["seq"] for event in second.events] == seqs and
          [event["command"] for event in second.events] == commands, "a second subscriber sees the same sequence")
    check(first.events[1]["args"]["name"] == "event two", "an event identifies the change (its args)")
    return seqs


def check_unsubscribe(first, second, editor, subscription, track, last_seq):
    done = first.ok("events.unsubscribe", {"subscription": subscription})
    check(done["unsubscribed"] and done["last_delivered_seq"] == last_seq and done["discarded"] == 0,
          "unsubscribe reports the last delivered seq")
    first.error("events.unsubscribe", {"subscription": subscription}, "not_found")
    second.error("events.unsubscribe", {"subscription": subscription}, "not_found")
    first.events = []
    editor.ok("track.rename", {"track": track, "name": "event four"})
    second.wait_events(6)
    drain_barrier(first)
    check(first.events == [], "after unsubscribe no further event arrives, while the other subscriber gets it")
    check(second.events[5]["seq"] == last_seq + 1, "the other subscriber continues in order")
    # And the connection can subscribe again.
    again = first.ok("events.subscribe")
    check(again["seq"] == last_seq + 1, "unsubscribing does not stop the sequence: resubscribe reports its position")


def check_order_and_unsubscribe(path, editor):
    first, second = Conn(path), Conn(path)
    a = first.ok("events.subscribe")
    b = second.ok("events.subscribe")
    check_subscribe_replies(a, b, first, editor)
    track = make_ordered_changes(first, editor)
    seqs = check_event_sequence(first, second, a["seq"])
    check_unsubscribe(first, second, editor, a["subscription"], track, seqs[-1])
    first.close()
    second.close()


def overflow_while_not_reading(slow, editor, sub, edits):
    """Stops reading with the socket buffer full, then makes `edits` changes. Returns the unread request ids and the overflow count."""
    # Replies far larger than the buffer are queued unread, so the event path has to wait and each edit below lands
    # in the bounded queue. Deterministic, no timing involved.
    requests = [slow.send("control.commands_list") for _ in range(2)]
    barrier = editor.ok("control.commands_list")
    check(len(json.dumps(barrier)) > 256 * 1024, "the unread replies exceed the socket buffer")
    for index in range(edits):
        add_track(editor, "overflow %d" % index)
    state = editor.ok("events.get_state")
    entry = [row for row in state["subscriptions"] if row["subscription"] == sub["subscription"]][0]
    check(entry["queued"] == 4 and entry["overflowed"] is True and entry["delivered"] == 0,
          "a client that stops reading holds exactly its bound in the instance: %s" % entry)
    check(state["queue_byte_limit"] == 1024 * 1024, "the byte bound is reported")
    return requests, state["overflows"]


def check_overflow_notice(slow, sub, requests, edits):
    """It reads again: the replies, the four queued events in order, then the notice. Returns the notice."""
    for request in requests:
        check(slow.reply(request).get("ok") is True, "the queued reply is whole (%d)" % request)
    slow.wait_events(5)
    delivered, notice = slow.events[:4], slow.events[4]
    seqs = [event["seq"] for event in delivered]
    check(seqs == list(range(sub["seq"] + 1, sub["seq"] + 5)), "the prefix arrives in order: %s" % seqs)
    check(notice["event"] == "events_overflow" and notice["subscription"] == sub["subscription"],
          "overflow is detectable: %s" % notice)
    check(notice["last_delivered_seq"] == seqs[-1] and notice["first_missed_seq"] == seqs[-1] + 1 and
          notice["missed"] == edits - 4 and notice["seq"] == sub["seq"] + edits,
          "the notice names what never arrived and the current position")
    check(all(event["seq"] < notice["first_missed_seq"] for event in delivered),
          "no missed event is claimed as received")
    return notice


def check_recovery_after_overflow(slow, editor, sub, notice, expected_tracks):
    """The defined recovery: subscribe again, reread the current state, carry on from there."""
    resumed = slow.ok("events.subscribe")
    check(track_count(slow) == expected_tracks, "rereading shows the missed edits too")
    check(resumed["seq"] == notice["seq"] and resumed["subscription"] != sub["subscription"],
          "a new subscription starts at the current position")
    slow.events = []
    add_track(editor, "after overflow")
    slow.wait_events(1)
    check(slow.events[0]["seq"] == resumed["seq"] + 1, "the resumed subscription receives the next change")


def check_overflow_and_recovery(path, editor):
    edits = 10
    slow = Conn(path)
    sub = slow.ok("events.subscribe", {"queue_limit": 4})
    check(sub["queue_limit"] == 4, "a smaller bound can be asked for")
    before = track_count(editor)
    requests, overflows = overflow_while_not_reading(slow, editor, sub, edits)
    notice = check_overflow_notice(slow, sub, requests, edits)

    state = editor.ok("events.get_state")
    check(state["overflows"] == overflows + 1 and
          sub["subscription"] not in [row["subscription"] for row in state["subscriptions"]],
          "the overflow ended the subscription and was counted")
    slow.error("events.unsubscribe", {"subscription": sub["subscription"]}, "not_found")
    check_recovery_after_overflow(slow, editor, sub, notice, before + edits)
    slow.close()


def check_disconnect_and_recovery(path, editor):
    client = Conn(path)
    sub = client.ok("events.subscribe")
    add_track(editor, "before disconnect")
    client.wait_events(1)
    last_seen = client.events[-1]["seq"]
    disconnects = editor.ok("events.get_state")["disconnects"]
    client.close()
    deadline = time.time() + 10
    while editor.ok("events.get_state")["disconnects"] == disconnects:
        if time.time() > deadline:
            raise AssertionError("the instance never noticed the subscriber left")
        time.sleep(0.05)
    check(True, "the instance frees a subscriber whose connection closed")
    before = track_count(editor)
    add_track(editor, "while away")
    add_track(editor, "while away 2")

    back = Conn(path)
    resumed = back.ok("events.subscribe")
    check(resumed["session"] == sub["session"] and resumed["seq"] == last_seen + 2,
          "after reconnecting, seq shows two changes were missed; they are not replayed")
    drain_barrier(back)
    check(back.events == [], "nothing missed is claimed as received")
    check(track_count(back) == before + 2, "rereading shows the changes made while away")
    add_track(editor, "after reconnect")
    back.wait_events(1)
    check(back.events[0]["seq"] == last_seen + 3, "the new subscription resumes with the next change")
    back.close()


def check_project_replaced(path, editor):
    client = Conn(path)
    client.ok("events.subscribe")
    editor.ok("project.new")
    client.wait_events(1)
    check(client.events[0]["command"] == "project.new", "replacing the project is an event")
    client.close()


def main(argv):
    if len(argv) != 2:
        print(__doc__)
        return 2
    with H.start_instance(argv[1]) as instance:
        probe = H.connect(instance)
        H.wait_ready(instance, probe, None)
        probe.close()
        path = instance.socket_path
        editor = Conn(path)
        check_order_and_unsubscribe(path, editor)
        check_overflow_and_recovery(path, editor)
        check_disconnect_and_recovery(path, editor)
        check_project_replaced(path, editor)
        check(editor.events == [], "a connection that never subscribed never saw an event line")
        editor.close()  # it would not sit through the idle wait
        check_idle_subscriber(path)
        editor = Conn(path)
        editor.ok("control.quit", {"save": False})
    print("PASS")
    return 0


def check_idle_subscriber(path):
    # Its own editor, connected after the wait: a fresh connection is not idle.
    subscriber, idle = Conn(path), Conn(path)
    sub = subscriber.ok("events.subscribe")
    idle.ok("control.ping")
    time.sleep(IDLE_TIMEOUT_S + 3)
    editor = Conn(path)
    add_track(editor, "after a quiet wait")
    subscriber.wait_events(1)
    check(subscriber.events[0]["seq"] == sub["seq"] + 1, "a quiet subscriber outlives the idle timeout")
    try:
        idle.ok("control.ping")
        retired = False
    except (OSError, H.Timeout, ValueError):
        retired = True
    check(retired, "a connection without a subscription is still retired by it")
    subscriber.close()
    idle.close()
    editor.close()


if __name__ == "__main__":
    sys.exit(main(sys.argv))
