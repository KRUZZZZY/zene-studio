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

and D, the TWO windows of the coalescing rule (BUG-CTL-4): a `plugin.param_set`
run inside the window is ONE undo step, and `control.undo` has to SAY how many
commands that one step covered - a "one command" reply over a merged span is
the A16 honesty failure this section exists to prevent.

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
        # D. one coalesced span: control.undo says how many commands it covered
        # ------------------------------------------------------------------
        print("")
        print("=" * 74)
        print("D. a coalesced span: control.undo reports undone_commands (BUG-CTL-4)")
        print("=" * 74)
        the_coalescing_rule_reports_what_it_covered(client, call, problems)

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


def parameter_of(reply):
    """The `parameter` object of a plugin.param_get reply, or {} when refused."""
    if reply.get("ok") is not True:
        return {}
    return (reply.get("result") or {}).get("parameter") or {}


def is_bipolar_number(spec):
    """A bipolar NUMBER parameter: the range it reports is the range the setter
    takes. A boolean flag's is not - an effect's own 'Effect enabled' reports the
    schema's -1..1 while plugin.param_set enforces 0..1 (measured, BUG-CTL-4) - so
    the values this section derives have to come from a parameter that passes."""
    if spec.get("type") != "number":
        return False
    return float(spec.get("min", 0)) < 0 < float(spec.get("max", 0))


def first_bipolar_parameter(client, call, host, effect):
    """The device's first bipolar numeric parameter, by index, or {}."""
    for index in range(0, 8):
        reply = call(client, 506 + index, "plugin.param_get",
                     {"target": host, "plugin": effect, "index": index})
        spec = parameter_of(reply)
        if is_bipolar_number(spec):
            return spec
    return {}


def candidate_values(spec):
    """Values inside the engine's own range that are clear of the default, so "it
    came back" cannot be confused with "it never moved"."""
    low, high = float(spec.get("min") or -1.0), float(spec.get("max") or 1.0)
    baseline = float(spec.get("value") or 0.0)
    picks = [low + (high - low) * fraction for fraction in (0.2, 0.45, 0.75, 0.35, 0.6, 0.9)]
    return [value for value in picks if abs(value - baseline) > 1e-3]


def coalescing_fixture(client, call, problems):
    """The device, parameter and values section D needs, or None (reported, never
    a silent pass)."""
    host = H.ok_result(call(client, 503, "track.add", {"type": "instrument"}), 503)["track"]
    offered = H.ok_result(call(client, 504, "plugin.list",
                               {"kind": "effect", "loadable_only": True}), 504)
    devices = offered.get("devices") or []
    if not devices:
        problems.add("section D could not run: plugin.list offered no loadable effect")
        return None
    effect = H.ok_result(call(client, 505, "plugin.load",
                              {"target": host, "device": devices[0]["id"]}), 505)["id"]
    spec = first_bipolar_parameter(client, call, host, effect)
    if not spec.get("name"):
        problems.add("section D could not run: %s offers no bipolar numeric parameter to drive "
                     "the coalescing rule with" % effect)
        return None
    fixture = {"host": host, "effect": effect, "name": spec["name"],
               "baseline": float(spec.get("value") or 0.0),
               "values": candidate_values(spec)}
    report("device / parameter", "%s '%s': min=%g max=%g default=%g, %d candidate values"
           % (fixture["effect"], fixture["name"], spec.get("min"), spec.get("max"),
              fixture["baseline"], len(fixture["values"])))
    if len(fixture["values"]) < 3:
        problems.add("the parameter's range gives fewer than three values away from its default")
        return None
    return fixture


def write_param(client, call, fixture, request_id, value):
    H.ok_result(call(client, request_id, "plugin.param_set",
                     {"target": fixture["host"], "plugin": fixture["effect"],
                      "name": fixture["name"], "value": value}), request_id)


def read_param(client, call, fixture, request_id):
    """The parameter's value as the engine reports it, read over the wire."""
    reply = H.ok_result(call(client, request_id, "plugin.param_get",
                             {"target": fixture["host"], "plugin": fixture["effect"],
                              "name": fixture["name"]}), request_id)
    return (reply.get("parameter") or {}).get("value")


def check_coalesced_span(client, call, problems, fixture):
    """(1) set -> a NON-journaled read -> set: the run survives the read, so the two
    writes are ONE step covering TWO commands - and the reply has to say so."""
    first, second = fixture["values"][0], fixture["values"][1]
    write_param(client, call, fixture, 510, first)
    landed = read_param(client, call, fixture, 511)
    H.ok_result(call(client, 512, "dsp.get_state", {"target": fixture["host"]}), 512)
    write_param(client, call, fixture, 513, second)
    merged = H.ok_result(call(client, 514, "control.undo"), 514)
    after = read_param(client, call, fixture, 515)
    report("coalesced: set, dsp.get_state, set, undo",
           "undone_command=%s undone_commands=%s | %g -> %g -> undo -> %g (pre-gesture %g)"
           % (merged.get("undone_command"), merged.get("undone_commands"),
              first, second, after, fixture["baseline"]))
    problems.require(abs(float(landed) - first) < 1e-6,
                     "the first plugin.param_set did not land, so the merge proves nothing")
    problems.require(merged.get("undone_command") == "plugin.param_set",
                     "control.undo did not report the parameter write it unwound")
    problems.require(merged.get("undone_commands") == 2,
                     "the ONE step control.undo unwound covered TWO commands and the reply did "
                     "not say so (BUG-CTL-4): it answered undone_commands=%r, which an agent "
                     "reads as a single-command inversion" % merged.get("undone_commands"))
    problems.require(abs(float(after) - fixture["baseline"]) < 1e-6,
                     "the merged step did not return the parameter to its pre-gesture value "
                     "(the earliest capture has to survive the merge: docs/UNDO-BOUNDS.md)")
    problems.require(abs(float(after) - first) > 1e-6,
                     "the two writes did NOT merge, so this leg is not the coalescing case")


def check_intervening_step(client, call, problems, fixture):
    """(2) set -> a JOURNALED step -> set: the run is broken, so each write is its
    own step and the FIRST write stays individually reversible."""
    first, third = fixture["values"][0], fixture["values"][2]
    tempo = H.ok_result(call(client, 519, "transport.get_state"), 519)["tempo"]
    write_param(client, call, fixture, 520, first)
    before = read_param(client, call, fixture, 521)
    H.ok_result(call(client, 522, "transport.set_tempo", {"bpm": tempo + 1}), 522)
    write_param(client, call, fixture, 523, third)
    single = H.ok_result(call(client, 524, "control.undo"), 524)
    after = read_param(client, call, fixture, 525)
    report("intervening journaled step: set, set_tempo, set, undo",
           "undone_command=%s undone_commands=%s | %g -> %g -> undo -> %g (pre-gesture %g)"
           % (single.get("undone_command"), single.get("undone_commands"),
              first, third, after, fixture["baseline"]))
    problems.require(abs(float(before) - first) < 1e-6,
                     "the intervening leg's first plugin.param_set did not land")
    problems.require(single.get("undone_commands") == 1,
                     "an intervening journaled step did not leave each write its own undo step "
                     "(BUG-CTL-4): control.undo reported undone_commands=%r"
                     % single.get("undone_commands"))
    problems.require(abs(float(after) - first) < 1e-6,
                     "the undo of the second write did not restore the FIRST write's value: one "
                     "command's edit was not individually reversible")
    problems.require(abs(float(after) - fixture["baseline"]) > 1e-6,
                     "the undo reverted past the first write, which is the defect itself")


def the_coalescing_rule_reports_what_it_covered(client, call, problems):
    """Section D - BUG-CTL-4, and BOTH windows of the coalescing rule.

    One `control.undo` unwinds ONE undo step; a step is one command EXCEPT where
    the coalescing rule (docs/UNDO-BOUNDS.md Decision 2) merged a run of the same
    command on the same target. The difference between the two windows IS the
    rule, so both are asserted: a NON-journaled read in between leaves the run
    alive (one step covering TWO commands, and the reply has to say so), while an
    intervening JOURNALED step ends it (each write its own step, and the first
    write stays individually reversible). One helper per report-area, the shape
    this file's sibling tests use: the complexity gate counts every boolean
    operator, so the branches stay in the helpers rather than in the driver.
    """
    # A generous window makes "these two calls are one gesture" a property of the
    # rule rather than of how fast two requests happened to arrive; setting it is
    # the documented way to state the granularity a leg asserts.
    window_before = H.ok_result(call(client, 501, "control.undo_depth"), 501) \
        .get("coalescing", {}).get("window_ms", 0)
    H.ok_result(call(client, 502, "control.set_undo_coalescing", {"window_ms": 60000}), 502)
    fixture = coalescing_fixture(client, call, problems)
    if fixture is None:
        return
    check_coalesced_span(client, call, problems, fixture)
    check_intervening_step(client, call, problems, fixture)
    H.ok_result(call(client, 530, "control.set_undo_coalescing", {"window_ms": window_before}), 530)


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
