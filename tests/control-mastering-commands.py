#!/usr/bin/env python3
"""END-TO-END proof that auto-mastering wave 1 is drivable by an agent.

THE CLAIM UNDER TEST: an agent connected to a real `zene` instance over its control
socket can ask what mastering candidates wave 1 generates, master the open session -
ONE project render feeding every candidate - read every candidate's objective
measurements back off the wire, and TAKE THE RUN BACK (control.undo removes the files
it created and restores the revisions it replaced). Feature rows 25 and 72 of
docs/FEATURE-LIST-0.3.0.md; the design of record is docs/AUTO-MASTERING.md.

WHY A REGISTERED CTEST AND NOT A UNIT TEST. tests/src/core/MasteringTest.cpp holds the
ENGINE to account in process (render-once/branch-many, the measurements, the
distinction control, the report document). What it cannot prove is the release
contract's section 3.1: that the feature is reachable THROUGH THE SOCKET, with schema
validation, the wire numbers and the undo behaviour an external client actually gets.
So this starts the REAL binary (the ControlSocketIntegration mould:
`$<TARGET_FILE:zene>`, QT_QPA_PLATFORM=offscreen, the shared control_socket_harness)
with its own HOME/XDG world, and opens the product's OWN fixture
(tools/auto-mastering-demo.py, the generator the doc's reproduction section names):
three transient-heavy sample tracks, so the limiter has real work. The plumbing
(session, recorder, fixture loader, wire readers) is in tests/mastering_probe_lib.py,
which is a separate file for this fork's per-FILE gates, not a second contract.

WHAT IT ASSERTS, in numbers read off the wire:

  * the group is registered: control.commands_list carries mastering.list_candidates,
    mastering.get_state and mastering.run, each with a schema and an A16 class;
  * the candidate set is the engine's own: five candidates, the -23 LUFS-I / +/-0.5 LU
    EBU R 128 target among them, the dynamics stage on exactly one, and NO field that
    could rank one - no score, no order, no preferred flag anywhere in the payload;
  * before any run `mastering.get_state` reports has_run false, last_run null and no
    files (an empty candidate list would be a different fact);
  * every refusal is typed and writes nothing: a missing out_dir, a relative one, and
    junk argument types;
  * the run: candidate_count 5 from render_count 1, six wav files on disk matching the
    reported paths, every candidate's loudness inside its own target's tolerance and
    its measured true peak at or below its ceiling - compared against the TARGET the
    wire reports, never against the command's own summary;
  * the read-back: `mastering.get_state` returns the run's own document and hashes the
    files it wrote;
  * THE INVERSE, twice over: after control.undo the directory the run wrote into holds
    no candidate at all (a first run into an empty directory is taken back by REMOVING
    what it created - a revision that never existed cannot be restored), and when a
    second run REPLACES a first run's files, control.undo restores the replaced
    revision byte for byte, measured by sha256;
  * the bound is enforced, not documented: a directory holding more than the 64 MiB
    capture bound is REFUSED, typed, and nothing is written.

Usage:
    QT_QPA_PLATFORM=offscreen python3 control-mastering-commands.py <zene-binary>

Exit codes: 0 every check held; 1 a check failed (the app log is printed);
2 cannot run (no binary at the path, or no shipped fixture generator).
"""

from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import control_socket_harness as H  # noqa: E402  (path set above)
import mastering_probe_lib as M  # noqa: E402  (path set above)

from mastering_probe_lib import (  # noqa: E402  (path set above)
    CANDIDATES, CAPTURE_LIMIT, CANDIDATE_KEYS, EBU_CEILING, EBU_LUFS, EBU_TOLERANCE,
    EXPECTED_TARGETS, SOURCE_RENDER, Recorder, Session, candidate_key_sets,
    dynamics_names, ranked_keys, sha256_of, standard_less_names, target_tuple,
    wav_names,
)


# ---------------------------------------------------------------------------
# the checks
# ---------------------------------------------------------------------------


def check_registration(session, recorder):
    """The three ids exist, carry schemas, and the writer declares itself."""
    listed = {entry.get("id")
              for entry in session.result("control.commands_list").get("commands") or []}
    wanted = {"mastering.list_candidates", "mastering.get_state", "mastering.run"}
    recorder.check("control.commands_list carries the three mastering.* ids",
                   wanted <= listed, "missing=%s" % sorted(wanted - listed))

    entry = session.command_entry("mastering.run")
    schema = entry.get("args_schema") or {}
    recorder.check("mastering.run requires out_dir and declares it writes",
                   entry.get("mutating") is True
                   and "out_dir" in (schema.get("required") or []),
                   "mutating=%r required=%r" % (entry.get("mutating"), schema.get("required")))
    reads = [session.command_entry(read_id)
             for read_id in ("mastering.list_candidates", "mastering.get_state")]
    recorder.check("both reads declare a result schema and write nothing",
                   all(entry.get("mutating") is False and entry.get("result_schema")
                       for entry in reads),
                   "reads=%r" % [(entry.get("id"), entry.get("mutating")) for entry in reads])


def check_candidate_targets(recorder, rows):
    """Every candidate's target numbers, and the document each one comes from."""
    measured = {row.get("name"): target_tuple(row) for row in rows}
    recorder.check("every candidate carries its own target's numbers",
                   measured == EXPECTED_TARGETS, "targets=%r" % measured)
    recorder.check("every target names the document its numbers come from",
                   not standard_less_names(rows), "missing=%r" % standard_less_names(rows))


def check_candidate_set(session, recorder):
    """The set is the engine's own five, with the cited targets and no ranking."""
    result = session.result("mastering.list_candidates")
    rows = result.get("candidates") or []
    recorder.check("mastering.list_candidates returns %d candidates" % CANDIDATES,
                   len(rows) == CANDIDATES and result.get("count") == CANDIDATES,
                   "count=%r len=%d" % (result.get("count"), len(rows)))
    check_candidate_targets(recorder, rows)
    recorder.check("the dynamics stage is on exactly one candidate",
                   dynamics_names(rows) == ["streaming-16-dynamics"],
                   "dynamics=%r" % dynamics_names(rows))

    # No field that could rank: a candidate's payload is settings only.
    recorder.check("a candidate carries settings only - no score, no order, no best",
                   candidate_key_sets(rows) == {CANDIDATE_KEYS},
                   "keys=%r" % sorted(candidate_key_sets(rows)))
    recorder.check("the payload names no preferred candidate and holds no order",
                   "preferred" in (result.get("note") or "") and not ranked_keys(result),
                   "keys=%r" % sorted(result.keys()))


def check_state_before_any_run(session, recorder):
    """Nothing has been mastered yet, and the instance says so rather than inventing."""
    state = session.result("mastering.get_state")
    recorder.check("before any run: has_run false, last_run null, no files",
                   state.get("has_run") is False and state.get("last_run") is None
                   and state.get("files") == [] and state.get("files_present") == 0,
                   "state=%r" % {key: state.get(key) for key in
                                 ("has_run", "last_run", "files", "files_present")})


def check_refusals(session, recorder, outdir):
    """Every refusal is typed, and no refusal writes anything."""
    before = wav_names(outdir)
    junk = [("mastering.run", {}),
            ("mastering.run", {"out_dir": "candidates"}),
            ("mastering.run", {"out_dir": "", "extra": 0}),
            ("mastering.run", {"out_dir": 0}),
            ("mastering.run", {"out_dir": [], "x": {}})]
    failures = []
    for command, args in junk:
        error = session.typed_error(command, args)
        if error.get("kind") != "invalid_args" or not error.get("message"):
            failures.append("%s %r -> %r" % (command, args, error))
    recorder.check("every junk request is a typed invalid_args with a message",
                   not failures, "; ".join(failures))
    recorder.check("no refusal wrote a file",
                   wav_names(outdir) == before, "after=%r" % (wav_names(outdir),))


def reading_problems(rows):
    """Every candidate that is outside its own target's tolerance or ceiling."""
    problems = []
    for row in rows:
        target, metrics = row.get("target") or {}, row.get("metrics") or {}
        lufs, dbtp = metrics.get("lufs_i"), metrics.get("true_peak_dbtp")
        if lufs is None or dbtp is None:
            problems.append("%s has no reading" % row.get("name"))
            continue
        if abs(lufs - target.get("integrated_lufs", 0.0)) > target.get("tolerance_lu", 0.0):
            problems.append("%s: %.2f outside %.2f +/-%.2f"
                            % (row.get("name"), lufs, target.get("integrated_lufs"),
                               target.get("tolerance_lu")))
        if dbtp > target.get("ceiling_dbtp", 0.0) + 0.05:
            problems.append("%s: %.2f dBTP above its ceiling" % (row.get("name"), dbtp))
    return problems


def check_the_run_counts(recorder, result, outdir, rows):
    """The counted facts: one render, six files, and the report matching them."""
    recorder.check("the run counted ONE project render for %d candidates" % CANDIDATES,
                   result.get("render_count") == 1
                   and result.get("candidate_count") == CANDIDATES,
                   "render_count=%r candidate_count=%r"
                   % (result.get("render_count"), result.get("candidate_count")))
    on_disk = wav_names(outdir)
    recorder.check("the source render and every candidate are on disk",
                   len(on_disk) == CANDIDATES + 1 and SOURCE_RENDER in on_disk,
                   "files=%r" % (on_disk,))
    reported = {os.path.basename(row.get("file") or "") for row in rows}
    recorder.check("every reported file is the file that exists",
                   reported <= set(on_disk) and len(reported) == CANDIDATES,
                   "reported=%r on_disk=%r" % (sorted(reported), on_disk))
    facts = result.get("files") or []
    recorder.check("the run reports every written file with a hash and a size",
                   len(facts) == CANDIDATES + 1
                   and all(entry.get("exists") and entry.get("sha256") for entry in facts),
                   "facts=%d" % len(facts))


def check_the_session_is_not_modified(session, recorder, before, after):
    """The render ran in a child process, so THIS instance's project did not move."""
    differing = sorted(key for key in set(before) | set(after)
                       if before.get(key) != after.get(key))
    recorder.check("the session is unchanged by a mastering run (it renders a copy)",
                   not differing and bool(before),
                   "differing=%r before=%r after=%r"
                   % (differing, {k: before.get(k) for k in differing},
                      {k: after.get(k) for k in differing}))


def check_the_run(session, recorder, outdir):
    """The run: one render, five measured candidates, files that match the report."""
    before_state = session.result("project.get_state")
    result = session.result("mastering.run", {"out_dir": outdir}, timeout=H.READY_TIMEOUT)
    if result.get("error"):
        recorder.check("mastering.run completed", False, "error=%r" % result.get("error"))
        return result

    rows = result.get("candidates") or []
    check_the_run_counts(recorder, result, outdir, rows)
    check_the_session_is_not_modified(session, recorder, before_state,
                                      session.result("project.get_state"))
    recorder.check("every candidate is inside its own target's tolerance and ceiling",
                   not reading_problems(rows), "; ".join(reading_problems(rows)))
    graded = {row.get("name"): (target_tuple(row), row.get("loudness_pass")) for row in rows}
    recorder.check("the EBU R 128 candidate is graded against the published numbers",
                   graded.get("ebu-r128") == ((EBU_LUFS, EBU_TOLERANCE, EBU_CEILING), True),
                   "ebu=%r" % (graded.get("ebu-r128"),))
    return result


def check_the_readback(session, recorder, run_result, outdir):
    """get_state returns the run's own document and hashes the files it wrote."""
    state = session.result("mastering.get_state")
    last = state.get("last_run") or {}
    differing = [(key, run_result.get(key), last.get(key))
                 for key in ("candidate_count", "render_count", "source_render_file")
                 if run_result.get(key) != last.get(key)]
    recorder.check("mastering.get_state returns the run's own report",
                   state.get("has_run") is True and not differing,
                   "differences=%r" % (differing,))
    files = state.get("files") or []
    recorder.check("the read-back hashes every file the run wrote",
                   state.get("files_present") == CANDIDATES + 1 and len(files) == CANDIDATES + 1,
                   "files_present=%r files=%d" % (state.get("files_present"), len(files)))
    hashed = {os.path.basename(entry.get("path") or "") for entry in files}
    recorder.check("the files the read-back hashes are the files on disk",
                   hashed == set(wav_names(outdir)),
                   "readback=%r disk=%r" % (sorted(hashed), wav_names(outdir)))


def check_transaction(session, recorder):
    """The run's A16 record: a true_inverse whose before-state is the directory."""
    records = session.result("control.transactions").get("transactions") or []
    mine = [record for record in records if record.get("command") == "mastering.run"]
    if not mine:
        recorder.check("control.transactions carries the mastering.run record", False,
                       "commands=%r" % [record.get("command") for record in records])
        return
    top = mine[-1]
    recorder.check("mastering.run records true_inverse and is reversible",
                   top.get("class") == "true_inverse" and top.get("reversible") is True,
                   "class=%r reversible=%r" % (top.get("class"), top.get("reversible")))

    before = top.get("before") or {}
    measured = {"directory_is_set": bool(before.get("directory")),
                "capture_limit_bytes": before.get("capture_limit_bytes"),
                "created_count": before.get("created_count")}
    recorder.check("the record's before-state is the output directory, bounded",
                   measured == {"directory_is_set": True, "capture_limit_bytes": CAPTURE_LIMIT,
                                "created_count": CANDIDATES + 1},
                   "before=%r" % measured)
    mechanism = top.get("mechanism") or ""
    recorder.check("the mechanism names the recorded action and is honest about redo",
                   "action checkpoint" in mechanism and "ONE-WAY" in mechanism,
                   "mechanism=%r" % mechanism[:160])


def check_undo_removes_the_run(session, recorder, outdir):
    """The inverse, first half: what the run CREATED is removed, and redo cannot fake it."""
    reply = session.call("control.undo", timeout=H.READY_TIMEOUT)
    recorder.check("control.undo answered for the mastering run",
                   reply.get("ok") is True, "%r" % reply)
    recorder.check("the directory the run wrote into holds no candidate afterwards",
                   wav_names(outdir) == (), "files=%r" % (wav_names(outdir),))
    # The row says the recorded step has NO redo half. Measured rather than trusted:
    # a redo that silently replayed an older step would bring files back here.
    session.call("control.redo")
    recorder.check("control.redo does not resurrect a candidate set there is no redo half for",
                   wav_names(outdir) == (), "files=%r" % (wav_names(outdir),))


def file_hashes(directory, names):
    """{name: sha256} for files read from disk, so a restore is compared by bytes."""
    return {name: sha256_of(os.path.join(directory, name)) for name in names}


def reported_hashes(facts):
    """{file name: the hash the run reported} for one run's file facts."""
    return {os.path.basename(entry.get("path") or ""): entry.get("sha256") for entry in facts}


def differing_names(left, right):
    """The names whose two hashes disagree."""
    return [name for name in left if left[name] != right.get(name)]


def check_a_replaced_revision_comes_back(session, recorder, outdir):
    """The inverse, second half: a RE-RUN's replaced revision is restored byte for byte."""
    first = session.result("mastering.run", {"out_dir": outdir}, timeout=H.READY_TIMEOUT)
    if first.get("error"):
        recorder.check("the first run of the pair completed", False,
                       "error=%r" % first.get("error"))
        return

    names = reported_hashes(first.get("files") or [])
    planted = file_hashes(outdir, list(names.keys()))
    recorder.check("the run's own hash matches the file it wrote", names == planted,
                   "differing=%r" % differing_names(planted, names))
    if names != planted:
        return

    session.result("mastering.run", {"out_dir": outdir}, timeout=H.READY_TIMEOUT)
    session.call("control.undo", timeout=H.READY_TIMEOUT)
    after = file_hashes(outdir, list(names.keys()))
    recorder.check("control.undo restores a REPLACED revision byte for byte",
                   after == planted, "differing=%r" % differing_names(planted, after))
    recorder.check("and the restored directory holds exactly the first run's files",
                   set(wav_names(outdir)) == set(names), "files=%r" % (wav_names(outdir),))


def check_the_capture_bound(session, recorder, outdir):
    """The bound is enforced: an oversized directory is refused and nothing is written."""
    held = os.path.join(outdir, "held.wav")
    os.makedirs(outdir, exist_ok=True)
    with open(held, "wb") as handle:
        handle.truncate(CAPTURE_LIMIT + 4096)
    error = session.typed_error("mastering.run", {"out_dir": outdir})
    recorder.check("a directory holding more than the capture bound is REFUSED, typed",
                   error.get("kind") == "refused" and "64" in (error.get("message") or ""),
                   "%r" % error)
    recorder.check("the refusal wrote nothing",
                   wav_names(outdir) == ("held.wav",), "files=%r" % (wav_names(outdir),))
    os.remove(held)


def check_quit(session, instance, recorder):
    reply = session.call("control.quit")
    if reply.get("ok") is not True:
        recorder.check("control.quit answered", False, "%r" % reply)
        return
    session.client.close()
    exited, code, waited = instance.wait_for_exit(H.QUIT_TIMEOUT)
    recorder.check("control.quit stops the instance", exited and code == 0,
                   "exited=%r code=%r after %.1fs" % (exited, code, waited))


def report(recorder):
    print("")
    print("==== checks ====")
    for name, passed, evidence in recorder.results:
        print("  %-62s %s" % (name, "ok" if passed else "FAILED"))
        if not passed:
            print("      %s" % evidence)
    problems = recorder.problems()
    if problems:
        print("")
        print("FAIL: the mastering.* socket proof has %d failed check(s)" % len(problems))
        return 1
    print("")
    print("PASS: %d checks, every one a measured number" % len(recorder.results))
    return 0


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    if not os.path.exists(argv[1]):
        print("cannot run: no binary at %s" % argv[1])
        return 2

    recorder = Recorder()
    transcript = H.Transcript()
    with H.start_instance(argv[1]) as instance:
        project = M.shipped_fixture(os.path.join(instance.tmp, "mastering-fixture"))
        if project is None:
            # Never Passed: a tree with no fixture generator cannot make this
            # measurement, and ctest reports 77 as Skipped.
            print("cannot run: no shipped fixture generator at %s/tools/auto-mastering-demo.py"
                  % M.REPO_ROOT)
            return 77
        outdir = os.path.join(instance.tmp, "candidates")

        H.wait_for_socket(instance)
        client = H.connect(instance)
        H.wait_ready(instance, client, transcript)
        session = Session(client, transcript)
        print("instance: %s" % argv[1])
        print("socket:   %s" % instance.socket_path)
        print("fixture:  %s" % project)

        session.result("control.version")
        check_registration(session, recorder)
        check_candidate_set(session, recorder)
        check_state_before_any_run(session, recorder)
        check_refusals(session, recorder, outdir)

        session.result("project.open", {"path": project})
        state = session.result("mastering.get_state")
        recorder.check("the fixture session is not empty", state.get("session_empty") is False,
                       "session_empty=%r" % state.get("session_empty"))

        run_result = check_the_run(session, recorder, outdir)
        if run_result.get("error"):
            H.fail("mastering.run is the point of this proof and it failed (%r)"
                   % run_result.get("error"), instance, transcript)
        check_the_readback(session, recorder, run_result, outdir)
        check_transaction(session, recorder)
        check_undo_removes_the_run(session, recorder, outdir)
        check_a_replaced_revision_comes_back(session, recorder, outdir)
        check_the_capture_bound(session, recorder, outdir)
        check_quit(session, instance, recorder)

    transcript.dump()
    code = report(recorder)
    if code == 0:
        H.ok("mastering.* socket proof (every check held)")
    return code


if __name__ == "__main__":
    sys.exit(main(sys.argv))
