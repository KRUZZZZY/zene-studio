#!/usr/bin/env python3
"""Socket proof that mastering.run ACCEPTS and RETAINS its run choices (Q050-188.1).

THE CLAIM UNDER TEST: an agent that passes `genre` and `delivery_target` to
mastering.run over the control socket gets them back, unchanged, in the run's own
document (last_run) and on every candidate row. An out_dir-only call still completes
and reports both choices as JSON null. A malformed choice is refused, typed as
invalid_args, with no child started and not one byte of out_dir changed.

THE ONE HOME OF THE VALUE LISTS. The accepted sets are held here, as data. The product
publishes the same lists through mastering.list_candidates (keys `genres`,
`delivery_targets`, `genre_max_length`); check_published_lists() cross-checks that
every value this proof uses is in the product's own list, so the two cannot drift.

  GENRES            a closed, lower-case set. A case variant ('Electronic') is REFUSED,
                    never silently lower-cased.
  GENRE_MAX_LENGTH  the documented maximum genre length, in characters. One character
                    over it is refused as invalid_args.
  DELIVERY_TARGETS  a closed, lower-case set.

The fixture is the shipped one (tools/auto-mastering-demo.py), loaded through
tests/mastering_probe_lib.py, the same plumbing as control-mastering-commands.py.

Usage:
    QT_QPA_PLATFORM=offscreen python3 control-mastering-choices.py <zene-binary>

Exit codes: 0 every check held; 1 a check failed (the transcript is printed);
2 cannot run (no binary at the path); 77 cannot run here (no shipped fixture generator).
"""

from __future__ import annotations

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import control_socket_harness as H  # noqa: E402  (path set above)
import mastering_probe_lib as M  # noqa: E402  (path set above)

from mastering_probe_lib import Recorder, Session, sha256_of  # noqa: E402  (path set above)


GENRES = ("ambient", "classical", "contemporary-alt-r-and-b", "electronic", "folk",
          "hip-hop", "jazz", "pop", "rock")
GENRE_MAX_LENGTH = 24
DELIVERY_TARGETS = ("ebu-r128", "streaming-14", "streaming-16")

#: A genre of exactly GENRE_MAX_LENGTH characters that IS in GENRES, and one over it.
GENRE_AT_MAX = "contemporary-alt-r-and-b"
GENRE_OVER_MAX = GENRE_AT_MAX + "x"

#: The choices the accepted run carries.
CHOICE = {"genre": "electronic", "delivery_target": "streaming-14"}

#: Step 4: malformed choices, each refused typed as invalid_args.
REFUSED_CHOICES = [
    {"genre": "polka-deluxe"},
    {"genre": 5},
    {"genre": ""},
    {"delivery_target": "streaming-99"},
    {"delivery_target": ["streaming-14"]},
]

#: Step 5: the boundary and the closed-set case rule, each refused typed as invalid_args.
REFUSED_BOUNDARY = [
    {"genre": GENRE_OVER_MAX},
    {"genre": "Electronic"},
]

#: The ACK must come back inside this many seconds (the request, not the render).
ACK_BOUND_SECONDS = 5.0

#: How long a run gets to finish, polled through mastering.get_state.
RUN_COMPLETION_BOUND = 120.0
POLL_SECONDS = 0.5


# ---------------------------------------------------------------------------
# helpers
# ---------------------------------------------------------------------------


def settle(session):
    """Polls mastering.get_state until its state leaves "running", inside the bound.

    Returns the last state read; on a timeout that is still "running", which the
    caller's checks then report.
    """
    deadline = time.monotonic() + RUN_COMPLETION_BOUND
    state = session.result("mastering.get_state")
    while state.get("state") == "running" and time.monotonic() < deadline:
        time.sleep(POLL_SECONDS)
        state = session.result("mastering.get_state")
    return state


def dir_hashes(directory):
    """{name: sha256} of every regular file in a directory, or {} if there is none."""
    if not os.path.isdir(directory):
        return {}
    return {name: sha256_of(os.path.join(directory, name))
            for name in sorted(os.listdir(directory))
            if os.path.isfile(os.path.join(directory, name))}


def transaction_count(session):
    return len(session.result("control.transactions").get("transactions") or [])


def is_null(row, key):
    """True when the key is PRESENT and its value is JSON null, not merely absent."""
    return key in row and row[key] is None


# ---------------------------------------------------------------------------
# the checks
# ---------------------------------------------------------------------------


def check_published_lists(session, recorder):
    """The product publishes the lists this proof uses, and the same genre maximum."""
    result = session.result("mastering.list_candidates")
    genres = result.get("genres") or []
    targets = result.get("delivery_targets") or []
    recorder.check("mastering.list_candidates publishes every genre and delivery target this proof uses",
                   set(GENRES) <= set(genres) and set(DELIVERY_TARGETS) <= set(targets),
                   "genres=%r delivery_targets=%r" % (genres, targets))
    recorder.check("mastering.list_candidates publishes the genre maximum this proof tests",
                   result.get("genre_max_length") == GENRE_MAX_LENGTH,
                   "genre_max_length=%r" % result.get("genre_max_length"))


def check_accepted_ack(session, recorder, args):
    """The ACK shape today's mastering.run returns, now for a request with choices."""
    started = time.monotonic()
    reply = session.call("mastering.run", args, timeout=H.SOCKET_TIMEOUT)
    elapsed = time.monotonic() - started
    ack = (reply.get("result") or {}) if reply.get("ok") is True else {}
    since = ack.get("since_submit_seconds")
    recorder.check("mastering.run accepts the choices: ok ACK, state running, poll mastering.get_state, out_dir echoed",
                   reply.get("ok") is True and ack.get("state") == "running"
                   and ack.get("poll") == "mastering.get_state" and ack.get("out_dir") == args["out_dir"],
                   "reply=%r" % reply)
    recorder.check("the ACK returns inside %.0f ms" % (ACK_BOUND_SECONDS * 1000),
                   elapsed < ACK_BOUND_SECONDS, "elapsed=%.3fs" % elapsed)
    recorder.check("the ACK carries since_submit_seconds as a number",
                   isinstance(since, (int, float)) and not isinstance(since, bool),
                   "since_submit_seconds=%r" % (since,))


def check_run_document(recorder, state, genre, target):
    """The run's own document carries the choices, on the run and on every row."""
    last = state.get("last_run") or {}
    recorder.check("the run reaches completed inside the bound",
                   state.get("state") == "completed",
                   "state=%r error=%r" % (state.get("state"), state.get("error")))
    recorder.check("last_run.genre is %r" % genre, last.get("genre") == genre,
                   "genre=%r" % last.get("genre"))
    recorder.check("last_run.delivery_target is %r" % target,
                   last.get("delivery_target") == target,
                   "delivery_target=%r" % last.get("delivery_target"))
    rows = last.get("candidates") or []
    echoed = [row.get("name") for row in rows
              if row.get("genre") != genre or row.get("delivery_target") != target]
    recorder.check("every candidate row carries the run's genre and delivery_target (%d rows)" % len(rows),
                   bool(rows) and not echoed, "rows not carrying them=%r" % echoed)


def check_absent_choices(session, recorder, directory):
    """No choice given: the run completes and every choice field is JSON null."""
    reply = session.call("mastering.run", {"out_dir": directory}, timeout=H.SOCKET_TIMEOUT)
    recorder.check("an out_dir-only mastering.run is accepted", reply.get("ok") is True,
                   "reply=%r" % reply)
    state = settle(session)
    last = state.get("last_run") or {}
    rows = last.get("candidates") or []
    recorder.check("an out_dir-only run completes", state.get("state") == "completed",
                   "state=%r error=%r" % (state.get("state"), state.get("error")))
    recorder.check("an out_dir-only run reports genre and delivery_target as JSON null",
                   is_null(last, "genre") and is_null(last, "delivery_target"),
                   "genre=%r delivery_target=%r" % (last.get("genre"), last.get("delivery_target")))
    not_null = [row.get("name") for row in rows
                if not (is_null(row, "genre") and is_null(row, "delivery_target"))]
    recorder.check("every candidate row reports both choices as JSON null (%d rows)" % len(rows),
                   bool(rows) and not not_null, "rows not null=%r" % not_null)


def refused_unchanged(session, recorder, label, choices, directory):
    """Every request is refused typed; no refusal changes out_dir or starts a child."""
    before, tx_before = dir_hashes(directory), transaction_count(session)
    failures = []
    for choice in choices:
        reply = session.call("mastering.run", dict(choice, out_dir=directory))
        error = reply.get("error") or {}
        if reply.get("ok") is True:
            settle(session)
        if reply.get("ok") is not False or error.get("kind") != "invalid_args" or not error.get("message"):
            failures.append("%r -> %r" % (choice, reply))
    after, tx_after = dir_hashes(directory), transaction_count(session)
    running = session.result("mastering.get_state").get("state") == "running"
    differing = sorted(name for name in set(before) | set(after)
                       if before.get(name) != after.get(name))
    recorder.check("%s: every refusal is typed invalid_args with a message" % label,
                   not failures, "; ".join(failures))
    recorder.check("%s: out_dir is byte-identical before and after (every file, sha256)" % label,
                   before == after, "differing=%r" % differing)
    recorder.check("%s: no child started (no transaction recorded, no run in flight)" % label,
                   tx_after == tx_before and not running,
                   "transactions %d -> %d running=%r" % (tx_before, tx_after, running))


def check_boundary_genre_accepted(session, recorder, directory):
    """A genre of exactly the documented maximum is accepted and retained."""
    reply = session.call("mastering.run", {"out_dir": directory, "genre": GENRE_AT_MAX},
                         timeout=H.SOCKET_TIMEOUT)
    recorder.check("a genre of exactly %d characters is accepted" % GENRE_MAX_LENGTH,
                   reply.get("ok") is True, "reply=%r" % reply)
    check_run_document(recorder, settle(session), GENRE_AT_MAX, None)


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
        print("FAIL: the mastering.run choices socket proof has %d failed check(s)" % len(problems))
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
            # Skipped, never Passed: a tree with no fixture generator cannot make
            # this measurement, and ctest reports 77 as Skipped.
            print("cannot run: no shipped fixture generator at %s/tools/auto-mastering-demo.py"
                  % M.REPO_ROOT)
            return 77
        choices_dir = os.path.join(instance.tmp, "choices")
        plain_dir = os.path.join(instance.tmp, "plain")

        H.wait_for_socket(instance)
        client = H.connect(instance)
        H.wait_ready(instance, client, transcript)
        session = Session(client, transcript)
        print("instance: %s" % argv[1])
        print("socket:   %s" % instance.socket_path)
        print("fixture:  %s" % project)

        session.result("project.open", {"path": project})
        check_published_lists(session, recorder)
        state = session.result("mastering.get_state")
        recorder.check("the fixture session is not empty", state.get("session_empty") is False,
                       "session_empty=%r" % state.get("session_empty"))

        # (1) + (2): accepted with choices, then retained on the run and on every row.
        check_accepted_ack(session, recorder, dict(CHOICE, out_dir=choices_dir))
        check_run_document(recorder, settle(session), CHOICE["genre"], CHOICE["delivery_target"])
        # (3): the behaviour-preserving case, no choices, JSON null everywhere.
        check_absent_choices(session, recorder, plain_dir)
        # (4): malformed choices are refused and change nothing.
        refused_unchanged(session, recorder, "malformed choices", REFUSED_CHOICES, plain_dir)
        # (5): the boundary, and the closed-set case rule.
        check_boundary_genre_accepted(session, recorder, plain_dir)
        refused_unchanged(session, recorder, "boundary and case", REFUSED_BOUNDARY, plain_dir)
        check_quit(session, instance, recorder)

    transcript.dump()
    code = report(recorder)
    if code == 0:
        H.ok("mastering.run choices socket proof (every check held)")
    return code


if __name__ == "__main__":
    sys.exit(main(sys.argv))
