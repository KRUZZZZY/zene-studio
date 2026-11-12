#!/usr/bin/env python3
"""Control-surface transcript for the MIDI bindings that target a SAVED WORKFLOW (Q050-217.2).

Starts the REAL `zene` binary headless (its own throwaway config and working directory) and drives
`session.bind_command`, `session.command_bindings` and the workflow.* group over the AF_UNIX control
socket, printing every request and reply verbatim.

WHAT THIS PROVES, AND WHAT IT DOES NOT.

  * a binding can NAME a saved workflow instead of a command, over the socket an agent uses - the
    target and the trigger are stored, and the reply carries the store's one-undo-step list;
  * it reads back with its target kind, its name, whether it resolves against the library in use and
    which library that is;
  * it SURVIVES A RESTART: the process quits, a second process starts on the same config file, and
    `session.command_bindings` still names the same workflow - which is the fact an in-process QtTest
    cannot show, because it never re-executes the binary;
  * the workflow it names is resolved from the library IN USE, not frozen: with workflows kept with
    the project the shared one stops resolving (and says why), saving that name in the project's
    library makes it resolve again, and the binding itself is never rewritten;
  * an ambiguous or unknown target is a typed refusal and stores nothing, and a bind is one undo step.

WHAT IT DOES NOT PROVE: that a MIDI NOTE or CC actually fires the binding. A running instance cannot
have a MIDI event injected from its control socket - there is no such command, and adding one would be
a new agent-only surface rather than this stage - so the replay half of the acceptance ("replay both,
the selected workflow completes") is proved by the registered Qt tests
`MidiWorkflowBindingsTest` (the synthetic MIDI-thread seam) and `MidiWorkflowBindingsGuiTest` (the
built menu and live events over the real application). Both are run beside this transcript.

Usage:  QT_QPA_PLATFORM=offscreen python3 tests/control-workflow-bindings.py <zene>
Exit 0 only when every check held.
"""

import os
import sys
import time

import control_socket_harness as H
from freeze_bounce_evidence import Recorder, Session, report_results


def kind(reply):
    return (reply.get("error") or {}).get("kind")


def track_count(session):
    return session.result("track.list").get("count") or 0


def step(name, count):
    return [{"command": "track.add", "args": {"type": "instrument", "name": "%s %d" % (name, i + 1)}}
            for i in range(count)]


def targets(session):
    return session.result("session.command_bindings").get("targets") or []


def named(session, name):
    for entry in targets(session):
        if entry.get("name") == name:
            return entry
    return {}


def wait_run(session):
    status = {}
    for _ in range(80):
        status = session.result("workflow.status")
        if status.get("state") == "done":
            break
        time.sleep(0.1)
    return status


def check_binding(session, recorder):
    """A note bound to a saved workflow: stored, read back, and the named workflow runs."""
    saved = session.call("workflow.save", {"name": "socket run", "steps": step("socket", 2)})
    bound = session.result("session.bind_command", {
        "trigger": "note", "key": 60, "channel": 0, "workflow": "socket run"})
    stored = bound.get("bindings") or [{}]
    recorder.check(
        "a note binds to a SAVED WORKFLOW over the socket (not only to a command)",
        saved.get("ok") is True and stored[0].get("workflow") == "socket run" and "command" not in stored[0],
        "save=%r bind=%r" % (saved, bound))
    check_readback(session, recorder)
    check_named_run(session, recorder)
    check_refusals(session, recorder)
    check_one_undo(session, recorder)


def check_readback(session, recorder):
    """The binding reads back with its target kind, its name and whether it resolves."""
    read = named(session, "socket run")
    listed = session.result("session.command_bindings")
    workflows = listed.get("workflows") or []
    recorder.check(
        "it reads back with its target kind, its name and whether it resolves",
        read.get("target") == "workflow" and read.get("resolved") is True
        and listed.get("workflow_scope") == "shared" and "socket run" in workflows,
        "target=%r scope=%r workflows=%r" % (read, listed.get("workflow_scope"), workflows))


def check_named_run(session, recorder):
    """The named workflow runs through workflow.run and completes with its own steps - the path the
    binding calls (the activation half is the Qt tests' - a live instance has no MIDI injection)."""
    before = track_count(session)
    ran = session.result("workflow.run", {"name": "socket run"})
    status = wait_run(session)
    recorder.check(
        "the named workflow runs through workflow.run and completes with its own steps",
        ran.get("queued") == 2 and status.get("state") == "done" and status.get("stopped_at") == -1
        and status.get("name") == "socket run" and track_count(session) == before + 2,
        "run=%r status=%r before=%d after=%d" % (ran, status, before, track_count(session)))


def check_refusals(session, recorder):
    """An unknown workflow, and two targets at once: typed refusals that store nothing."""
    bad = session.call("session.bind_command", {
        "trigger": "note", "key": 61, "channel": 0, "workflow": "no such workflow"})
    both = session.call("session.bind_command", {
        "trigger": "note", "key": 61, "channel": 0,
        "command": "transport.stop", "workflow": "socket run"})
    recorder.check(
        "an unknown workflow and two targets at once are typed refusals that store nothing",
        kind(bad) == "not_found" and kind(both) == "invalid_args"
        and all(entry.get("key") != 61 for entry in targets(session)),
        "unknown=%r both=%r" % (bad, both))


def check_one_undo(session, recorder):
    """One edit, one undo: a CC binding to another workflow, and one undo puts the list back."""
    session.result("workflow.save", {"name": "second run", "steps": step("second", 1)})
    session.result("session.bind_command", {
        "trigger": "cc", "controller": 74, "channel": 1, "workflow": "second run"})
    two = len(targets(session))
    undone = session.call("control.undo")
    recorder.check(
        "a second bind is ONE undo step back to the first list",
        two == 2 and undone.get("ok") and len(targets(session)) == 1
        and targets(session)[0].get("name") == "socket run",
        "two=%d undo=%r after=%r" % (two, undone, targets(session)))


def check_library_resolution(session, recorder):
    switched = session.result("workflow.set_scope", {"scope": "project"})
    shared_gone = named(session, "socket run")
    recorder.check(
        "with workflows kept with the project the shared workflow stops resolving, and says why",
        switched.get("scope") == "project" and shared_gone.get("resolved") is False
        and "socket run" in (shared_gone.get("reason") or ""),
        "scope=%r target=%r" % (switched, shared_gone))

    session.result("workflow.save", {"name": "socket run", "steps": step("project", 3)})
    project_now = named(session, "socket run")
    listed = session.result("workflow.list")
    recorder.check(
        "saving that name in the PROJECT's library makes the same binding resolve it",
        project_now.get("resolved") is True and listed.get("scope") == "project"
        and any(entry.get("name") == "socket run" for entry in listed.get("workflows") or []),
        "target=%r list=%r" % (project_now, listed))

    # The binding was never rewritten while the library moved underneath it.
    stored = session.result("session.command_bindings").get("bindings") or [{}]
    recorder.check(
        "the stored binding still names the workflow, with no copied steps",
        stored[0].get("workflow") == "socket run" and "steps" not in stored[0],
        "stored=%r" % stored)


def check_restart(instance, recorder, transcript, binary):
    """Quit cleanly, then start a FRESH process that reads the config the first one wrote."""
    client = H.connect(instance)
    H.wait_ready(instance, client, transcript)
    session = Session(client, transcript)
    project = os.path.join(instance.workspace, "kept.mmp")
    session.result("project.save", {"path": project})
    quit_reply = session.call("control.quit")
    session.client.close()
    exited, code, _ = instance.wait_for_exit(H.QUIT_TIMEOUT)
    recorder.check("the instance quits cleanly so its config is on disk", exited and code == 0,
                   "quit=%r code=%r" % (quit_reply, code))
    config_text = open(instance.config_path, errors="replace").read() if os.path.exists(instance.config_path) else ""
    recorder.check("the config file holds the workflow binding it wrote",
                   "socket run" in config_text, "config=%d chars" % len(config_text))

    # A NEW instance in its own temp world, handed that SAME config file and working directory: this
    # is a restart as far as the application can tell, and the binary really executes again.
    second = H.Instance(binary, workingdir=instance.workspace)
    try:
        with open(instance.config_path, "rb") as source, open(second.config_path, "wb") as target:
            target.write(source.read())
        second.spawn()
        H.wait_for_socket(second)
        client2 = H.connect(second)
        H.wait_ready(second, client2, transcript)
        session2 = Session(client2, transcript)
        read = named(session2, "socket run")
        scope = session2.result("workflow.list").get("scope")
        recorder.check(
            "after a restart the binding is still there and still names the same workflow",
            read.get("target") == "workflow" and read.get("name") == "socket run" and scope == "project",
            "target=%r scope=%r" % (read, scope))
        # The workflow it names is the PROJECT's, so it lives in the project: before the project is
        # open the binding honestly says it does not resolve (and why), and opening the project - which
        # carries the library - makes it resolve. That is the "project workflow library" half.
        recorder.check(
            "before its project is open the project-scoped workflow honestly does not resolve",
            read.get("resolved") is False and "socket run" in (read.get("reason") or ""),
            "target=%r" % (read,))
        reopened = session2.call("project.open", {"path": project})
        read_again = named(session2, "socket run")
        recorder.check(
            "reopening the project makes the workflow the binding resolves come back",
            reopened.get("ok") is True and read_again.get("resolved") is True,
            "open=%r target=%r" % (reopened, read_again))
        session2.call("control.quit")
        second.wait_for_exit(H.QUIT_TIMEOUT)
    finally:
        second.close()


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    recorder = Recorder()
    transcript = H.Transcript()
    instance = H.start_instance(argv[1])
    try:
        H.wait_for_socket(instance)
        client = H.connect(instance)
        H.wait_ready(instance, client, transcript)
        session = Session(client, transcript)
        print("instance: %s" % argv[1])
        print("socket:   %s" % instance.socket_path)
        session.result("control.version")
        check_binding(session, recorder)
        check_library_resolution(session, recorder)
        check_restart(instance, recorder, transcript, argv[1])
    finally:
        if instance.alive():
            instance.close()

    report_results(recorder)
    transcript.dump()
    if recorder.problems:
        print("")
        recorder.problems.report("workflow MIDI bindings control-surface transcript")
        return 1
    H.ok("workflow MIDI bindings control-surface transcript (every check held)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
