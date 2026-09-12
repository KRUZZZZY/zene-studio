#!/usr/bin/env python3
"""RAW apply -> undo -> read-back transcript for three commands of DIFFERENT
SPEC A16 classes (task #623).

This is the acceptance evidence for the reversibility contract, produced by
driving the REAL `lmms` binary headless over the control socket and printing
every request and every reply verbatim - no summarising layer, so a reader can
see exactly what the engine answered. The three commands are one of each class:

  A. `track.add`           true_inverse - an action checkpoint on the engine's
                           own undo stack (a created track has no before-state)
  B. `project.save`        snapshot     - the previous file revision is kept and
                           the recorded inverse is the command
                           `project.restore_revision`, which control.undo
                           dispatches (the file bytes are hashed here on the
                           client side, so the read-back is external evidence)
  C. `script.run`          irreversible - control.undo must FAIL, typed, naming
                           the command and its documented fallback, and must NOT
                           silently undo the reversible step underneath it

The instance is started with the documented headless recipe through the shared
harness (tests/control_socket_harness.py), so this file adds no second launch
path.

Usage: QT_QPA_PLATFORM=offscreen python3 control-reversibility-transcript.py <lmms>
Exit code 0 only when every assertion held.
"""

import hashlib
import os
import sys

import control_socket_harness as H


def sha256_of(path):
    try:
        with open(path, "rb") as handle:
            return hashlib.sha256(handle.read()).hexdigest()
    except OSError:
        return ""


def size_of(path):
    try:
        return os.path.getsize(path)
    except OSError:
        return -1


def report(label, text):
    print("")
    print("### %s" % label)
    print("    %s" % text)


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    binary = argv[1]

    problems = H.Problems()
    lines = []

    def call(client, request_id, cmd, args=None, **kwargs):
        """One JSON-RPC exchange, echoed raw and recorded for the transcript."""
        reply = client.call(request_id, cmd, args, **kwargs)
        lines.append("-> %s %s" % (cmd, args or {}))
        lines.append("<- %s" % reply)
        return reply

    with H.start_instance(binary) as instance:
        H.wait_for_socket(instance)
        client = H.connect(instance)
        H.wait_ready(instance, client, None)
        print("instance: %s" % binary)
        print("socket:   %s" % instance.socket_path)

        # ------------------------------------------------------------------
        # A. true_inverse: track.add, undone by ONE control.undo
        # ------------------------------------------------------------------
        print("")
        print("=" * 74)
        print("A. true_inverse: track.add (a created track, no before-state)")
        print("=" * 74)
        before = H.ok_result(call(client, 101, "track.list"), 101)["count"]
        added = H.ok_result(call(client, 102, "track.add", {"type": "instrument"}), 102)
        track = added.get("track")
        after = H.ok_result(call(client, 103, "track.list"), 103)["count"]
        record = last_transaction(client, call, 104, "track.add")
        report("before / after track.list", "%d -> %d (new track %s)" % (before, after, track))
        report("transaction", "class=%s reversible=%s" % (record.get("class"), record.get("reversible")))
        report("mechanism", record.get("mechanism"))
        undone = H.ok_result(call(client, 105, "control.undo"), 105)
        back = H.ok_result(call(client, 106, "track.list"), 106)["count"]
        report("control.undo", "undone=%s undone_command=%s" % (undone.get("undone"), undone.get("undone_command")))
        report("read back track.list", "%d (expected %d)" % (back, before))
        problems.require(after == before + 1, "track.add did not add exactly one track")
        problems.require(record.get("reversible") is True, "track.add did not record reversible:true")
        problems.require(undone.get("undone") is True, "control.undo undid nothing after track.add")
        problems.require(back == before, "the undo did not return the track count to its pre-command value")

        # ------------------------------------------------------------------
        # B. snapshot (file-level): project.save keeps a recoverable revision
        # ------------------------------------------------------------------
        print("")
        print("=" * 74)
        print("B. snapshot: project.save (file-level; the inverse is a command)")
        print("=" * 74)
        target = os.path.join(instance.tmp, "revision-transcript.mmp")
        H.ok_result(call(client, 201, "project.save", {"path": target}), 201)
        first_sha = sha256_of(target)
        first_size = size_of(target)
        report("first save", "sha256=%s bytes=%d" % (first_sha, first_size))

        # Change the session, save again: the first revision must be recoverable.
        H.ok_result(call(client, 202, "track.add", {"type": "pattern"}), 202)
        second = H.ok_result(call(client, 203, "project.save", {"path": target}), 203)
        second_sha = sha256_of(target)
        report("second save", "sha256=%s bytes=%d revision_kept=%s"
               % (second_sha, size_of(target), second.get("revision_kept")))
        report("retained revisions", second.get("revisions"))
        problems.require(second_sha != first_sha, "the second save wrote identical bytes; the test proves nothing")

        save_record = last_transaction(client, call, 204, "project.save")
        report("transaction", "class=%s reversible=%s inverse=%s"
               % (save_record.get("class"), save_record.get("reversible"), save_record.get("inverse")))
        undone = H.ok_result(call(client, 205, "control.undo"), 205)
        report("control.undo", "undone=%s restored_by=%s" % (undone.get("undone"), undone.get("restored_by")))
        report("file read back", "sha256=%s bytes=%d (expected the first save's bytes)"
               % (sha256_of(target), size_of(target)))
        problems.require(save_record.get("reversible") is True, "project.save did not record reversible:true")
        problems.require(undone.get("restored_by") == "project.restore_revision",
                         "the save's undo did not dispatch project.restore_revision")
        problems.require(sha256_of(target) == first_sha,
                         "the previous file revision was NOT recovered (sha256 differs)")

        # ------------------------------------------------------------------
        # C. irreversible: script.run refuses, typed, and does not pretend
        # ------------------------------------------------------------------
        print("")
        print("=" * 74)
        print("C. irreversible: script.run (no inverse; typed refusal)")
        print("=" * 74)
        tempo_before = H.ok_result(call(client, 301, "transport.get_state"), 301)["tempo"]
        wanted = tempo_before + 5
        H.ok_result(call(client, 302, "transport.set_tempo", {"bpm": wanted}), 302)
        tempo_set = H.ok_result(call(client, 303, "transport.get_state"), 303)["tempo"]
        report("reversible step underneath", "tempo %d -> %d" % (tempo_before, tempo_set))

        H.ok_result(call(client, 304, "script.run",
                         {"source": "lmms.log():info('a16 transcript')\n"}), 304)
        script_record = last_transaction(client, call, 305, "script.run")
        report("transaction", "class=%s reversible=%s" % (script_record.get("class"), script_record.get("reversible")))

        refused = call(client, 306, "control.undo")
        error = H.typed_error(refused, 306, "irreversible")
        report("control.undo", "ok=%s error.kind=%s" % (refused.get("ok"), error.get("kind")))
        report("message", error.get("message"))
        tempo_after = H.ok_result(call(client, 307, "transport.get_state"), 307)["tempo"]
        report("tempo read back", "%d (expected %d: the reversible step was NOT undone)"
               % (tempo_after, tempo_set))
        problems.require(script_record.get("class") == "irreversible",
                         "script.run is not classed irreversible")
        problems.require("script.run" in (error.get("message") or ""),
                         "the refusal does not name the command")
        problems.require("addCheckPoint" in (error.get("message") or ""),
                         "the refusal does not name the documented fallback")
        problems.require(tempo_after == tempo_set,
                         "control.undo quietly undid an OLDER step after an irreversible command")

        # ------------------------------------------------------------------
        quit_reply = call(client, 400, "control.quit", {"save": False})
        report("control.quit", quit_reply)
        client.close()
        instance.wait_for_exit(30.0)

    print("")
    print("=" * 74)
    print("RAW TRANSCRIPT (every request, every reply)")
    print("=" * 74)
    for line in lines:
        print(line)

    ok = problems.report("A16 reversibility transcript")
    return 0 if ok else 1


def last_transaction(client, call, request_id, command):
    """The last recorded transaction for `command` (SPEC A16's audit record)."""
    report = H.ok_result(call(client, request_id, "control.transactions"), request_id)
    found = None
    for entry in report.get("transactions", []):
        if entry.get("command") == command:
            found = entry
    if found is None:
        raise AssertionError("no transaction recorded for %s" % command)
    return found


if __name__ == "__main__":
    sys.exit(main(sys.argv))
