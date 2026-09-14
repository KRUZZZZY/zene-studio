#!/usr/bin/env python3
"""END-TO-END proof that the crash reporter is OBSERVABLE and OPERABLE by an
agent, and that the verbs it does NOT have are refused rather than missing.

THE CLAIM UNDER TEST (docs/FEATURE-LIST-0.3.0.md row 54, "Crash reporter"). The
audit's row says the reporter is "in the tree with a registered
`CrashReporterTest`; no command group". This is the group's proof:

  * crash.list_reports is the read: whether the reporter is installed, its
    report directory and the file it owns with its size and last-written time,
    whether a report is still pending an offer (the module's own
    hasPendingReport), the `offered` sentinel, whether a session marker says the
    previous run exited uncleanly, the module's two hard bounds, and the upload
    policy;
  * crash.acknowledge_report and crash.discard_report are the two operations
    the module really has (acknowledgePendingReport / discardPendingReport),
    and each is measured on the FILE as well as on the wire;
  * crash.upload_report is registered and REFUSES every call, by name, because
    this build has no upload and no network code of any kind in the reporter
    (include/CrashReporter.h states it) - so a client that asks to send a report
    is told why not and where the file is.

WHY IT IS A REGISTERED CTEST AND NOT A UNIT TEST. tests/src/core/
CrashReporterTest.cpp proves the module in process: the report is bounded to
kMaxReportBytes, the handler is re-entrancy-guarded, a clean run leaves no crash
state, the offered sentinel suppresses the second offer. What it cannot prove is
the release contract's section 3.1 - that the capability is reachable THROUGH THE
SOCKET by an agent. So this runs the REAL binary ($<TARGET_FILE:zene>,
QT_QPA_PLATFORM=offscreen, the shared control_socket_harness) and drives the
state machine over the wire.

HOW THE FIXTURE IS MADE, stated because it is not a crash. A real crash cannot be
induced in-process - the reporter writes the report from a signal handler and
the process then dies - so this transcript PLANTS a report at exactly the path
the reporter names (the format is the module's own: a `Zene Studio crash report
v1` header), which is the file the module's predicates are about: `pending` is
"a report file exists and the offered sentinel does not" (CrashReporter.cpp,
hasPendingReport). Everything downstream of that file - the read, the
acknowledge, the discard, the typed refusals - is the real implementation.

WHAT IT ASSERTS, in numbers:
  * on a fresh run: the reporter is installed, its report directory is
    <working dir>/crash-reports, its file is zene-crash-report.txt there with the
    offered sentinel beside it, reports is empty, pending/offered are false, the
    bounds are the module's own (kMaxReportBytes == 4096,
    kMaxProjectPathBytes == 512), upload.supported is false, and the RUNNING
    instance's own session marker is present (so previous_run_exited_cleanly is
    false while this process lives - the marker is what the clean exit removes);
  * after the fixture report is planted: one report with its byte count, pending
    true, offered false;
  * acknowledge: pending false and offered true on the wire, the sentinel EXISTS
    on disk, and the report file is still there (acknowledge keeps it);
  * discard: both files gone from disk, removed_count 2, pending false;
  * both writers are `irreversible`: control.undo after each FAILS, typed, and
    the message names the fallback; control.transactions carries the class and a
    non-empty mechanism for both;
  * an acknowledge or a discard with nothing to act on is a typed not_found, and
    an argument to a command that takes none is a typed invalid_args - so a
    mistyped call cannot look like a successful one.

THE BOUND THIS TEST STATES RATHER THAN HIDES: nothing can UN-acknowledge a report
or restore a discarded one - the module has no such function and nothing writes a
report from a caller's bytes - so both writers are irreversible by design and the
test asserts the typed refusal and the named fallback rather than a fake inverse.
There is also no crash.enable / crash.disable: main() installs the reporter
before this socket exists, and the module has no uninstall.

Usage:
    QT_QPA_PLATFORM=offscreen python3 control-crash-reporter.py <zene-binary>

Exit codes: 0 every check held; 1 a check failed (the app log is printed);
2 cannot run (no binary at the path).
"""

from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import control_socket_harness as H  # noqa: E402  (path set above)

#: The names include/CrashReporter.h declares, relative to the report directory.
REPORT_DIR_NAME = "crash-reports"
REPORT_FILE_NAME = "zene-crash-report.txt"
OFFERED_MARKER_NAME = "zene-crash-report.offered"
SESSION_MARKER_NAME = "zene-session-open.marker"
#: The module's own hard caps (kMaxReportBytes / kMaxProjectPathBytes).
MAX_REPORT_BYTES = 4096
MAX_PROJECT_PATH_BYTES = 512
#: A plausible report body, in the module's own format (writeReportIfIdle).
REPORT_BODY = ("Zene Studio crash report v1\nsignal=SIGSEGV(11)\nfault_addr=0x0\npc=0x0\n"
               "thread=1\npid=0\ntime_unix=0\nversion=0.3.0-alpha\n"
               "platform=test machine\ncompiler=test\nproject=(none)\n")


class Session:
    """The socket client, a running request id and the raw transcript."""

    def __init__(self, client, transcript):
        self.client = client
        self.transcript = transcript
        self.last_id = 0

    def call(self, command, args=None):
        self.last_id += 1
        return self.client.call(self.last_id, command, args, transcript=self.transcript)

    def result(self, command, args=None):
        reply = self.call(command, args)
        if reply.get("ok") is True:
            return reply.get("result") or {}
        return {"error": reply.get("error") or reply}

    def typed_error(self, command, args=None):
        reply = self.call(command, args)
        if reply.get("ok") is False:
            return reply.get("error") or {}
        return {}


def plant_report(instance, problems):
    """Create the report the reporter's predicates are about, where it looks."""
    directory = os.path.join(instance.workspace, REPORT_DIR_NAME)
    path = os.path.join(directory, REPORT_FILE_NAME)
    try:
        os.makedirs(directory, exist_ok=True)
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(REPORT_BODY)
    except OSError as error:
        problems.add("could not plant the report fixture at %s: %s" % (path, error))
        return None
    return path


# ---------------------------------------------------------------------------
# the checks
# ---------------------------------------------------------------------------


def check_fresh_state(session, instance, problems):
    """The read, on an instance that has crashed nothing."""
    state = session.result("crash.list_reports")
    directory = os.path.join(instance.workspace, REPORT_DIR_NAME)
    problems.require(state.get("installed") is True,
                     "the reporter must be installed on this platform: %r" % (state.get("installed"),))
    problems.require(state.get("report_directory") == directory,
                     "the report directory must be <working dir>/%s: %r"
                     % (REPORT_DIR_NAME, state.get("report_directory")))
    problems.require(state.get("report_path") == os.path.join(directory, REPORT_FILE_NAME)
                     and state.get("offered_marker_path")
                     == os.path.join(directory, OFFERED_MARKER_NAME)
                     and state.get("session_marker_path")
                     == os.path.join(instance.workspace, SESSION_MARKER_NAME),
                     "the three file paths must be the module's own names: %r"
                     % ({k: state.get(k) for k in ("report_path", "offered_marker_path",
                                                   "session_marker_path")},))
    problems.require(state.get("reports") == [] and state.get("report_count") == 0
                     and state.get("pending") is False and state.get("offered") is False,
                     "a run that has not crashed has no report: %r" % (state.get("reports"),))
    bounds = state.get("bounds") or {}
    problems.require(bounds.get("max_report_bytes") == MAX_REPORT_BYTES
                     and bounds.get("max_project_path_bytes") == MAX_PROJECT_PATH_BYTES,
                     "the bounds are the module's own constants: %r" % (bounds,))
    problems.require((state.get("upload") or {}).get("supported") is False,
                     "this build has no upload: %r" % (state.get("upload"),))
    # THIS process is running, so its own session marker is on disk: the marker
    # is what the clean exit path removes (endSession()).
    problems.require(state.get("session_marker_present") is True
                     and state.get("previous_run_exited_cleanly") is False,
                     "the running instance's own marker must be reported: %r/%r"
                     % (state.get("session_marker_present"),
                        state.get("previous_run_exited_cleanly")))
    return state


def check_nothing_to_do(session, problems):
    """The writers refuse when there is nothing to act on, and write nothing."""
    ack = session.typed_error("crash.acknowledge_report")
    problems.require(ack.get("kind") == "not_found",
                     "acknowledging with no report is not_found: %r" % (ack,))
    discard = session.typed_error("crash.discard_report")
    problems.require(discard.get("kind") == "not_found",
                     "discarding with no report is not_found: %r" % (discard,))
    extra = session.typed_error("crash.list_reports", {"bogus": 1})
    problems.require(extra.get("kind") == "invalid_args",
                     "the read takes no arguments, so one is invalid_args: %r" % (extra,))


def check_upload_refusal(session, problems):
    """The verb that does not exist is REFUSED, by name, with the file named."""
    refused = session.typed_error("crash.upload_report")
    message = refused.get("message") or ""
    problems.require(refused.get("kind") == "refused",
                     "upload must be a typed refusal: %r" % (refused,))
    problems.require("no upload" in message and "network" in message,
                     "the refusal must name the module's own stated absence: %r" % (message,))
    problems.require(REPORT_FILE_NAME in message,
                     "the refusal must name the file to attach by hand: %r" % (message,))


def check_planted_report(session, instance, problems):
    """The read on a run that HAS a report, and the metadata it carries."""
    path = plant_report(instance, problems)
    if path is None:
        return None
    state = session.result("crash.list_reports")
    reports = state.get("reports") or []
    problems.require(state.get("report_count") == 1 and len(reports) == 1,
                     "the planted report must be listed: %r" % (reports,))
    entry = reports[0] if reports else {}
    problems.require(entry.get("path") == path and entry.get("exists") is True
                     and entry.get("bytes") == len(REPORT_BODY)
                     and (entry.get("modified_unix") or 0) > 0,
                     "the report's path, size and last-written time must be reported: %r" % (entry,))
    problems.require(state.get("pending") is True and state.get("offered") is False,
                     "an offered marker that is not there means pending: %r/%r"
                     % (state.get("pending"), state.get("offered")))
    return path


def check_acknowledge(session, problems, report_path):
    """acknowledge: writes the sentinel, KEEPS the report, and records no inverse."""
    directory = os.path.dirname(report_path)
    sentinel = os.path.join(directory, OFFERED_MARKER_NAME)
    acked = session.result("crash.acknowledge_report")
    problems.require(acked.get("acknowledged") is True and acked.get("pending") is False
                     and acked.get("offered") is True,
                     "acknowledge must stop the report being pending: %r" % (acked,))
    problems.require(os.path.exists(sentinel),
                     "the `offered` sentinel must be on disk after an acknowledge: %s" % sentinel)
    problems.require(os.path.exists(report_path),
                     "acknowledge must KEEP the report file so it can be attached: %s" % report_path)
    state = session.result("crash.list_reports")
    problems.require(state.get("pending") is False and state.get("offered") is True,
                     "the state must stay acknowledged across reads: %r" % (state.get("pending"),))

    refused = session.typed_error("control.undo")
    problems.require(refused.get("kind") == "irreversible"
                     and OFFERED_MARKER_NAME in (refused.get("message") or ""),
                     "control.undo must fail typed and name the sentinel to delete: %r" % (refused,))


def check_discard(session, problems, report_path):
    """discard: clears both files, and says in as many words that it cannot come back."""
    directory = os.path.dirname(report_path)
    sentinel = os.path.join(directory, OFFERED_MARKER_NAME)
    discarded = session.result("crash.discard_report")
    problems.require(discarded.get("discarded") is True and discarded.get("removed_count") == 2,
                     "discard must report both files it removed: %r" % (discarded,))
    problems.require(not os.path.exists(report_path) and not os.path.exists(sentinel),
                     "both files must be gone from disk: %r" % (discarded.get("removed"),))
    state = session.result("crash.list_reports")
    problems.require(state.get("report_count") == 0 and state.get("pending") is False,
                     "the state must report no report after the discard: %r" % (state.get("reports"),))

    refused = session.typed_error("control.undo")
    problems.require(refused.get("kind") == "irreversible"
                     and "recoverable" in (refused.get("message") or ""),
                     "control.undo must fail typed and name the fallback: %r" % (refused,))
    again = session.typed_error("crash.discard_report")
    problems.require(again.get("kind") == "not_found",
                     "a second discard with nothing to discard is not_found: %r" % (again,))


def check_transactions(session, problems):
    """The A16 records: both writers are irreversible with a stated mechanism."""
    records = [record for record in session.result("control.transactions").get("transactions") or []
               if record.get("command", "").startswith("crash.")]
    classes = {(record.get("command"), record.get("class"), record.get("reversible"),
                bool(record.get("mechanism"))) for record in records}
    problems.require(("crash.acknowledge_report", "irreversible", False, True) in classes,
                     "acknowledge must record an irreversible transaction: %r" % (classes,))
    problems.require(("crash.discard_report", "irreversible", False, True) in classes,
                     "discard must record an irreversible transaction: %r" % (classes,))


def check_quit(session, instance, problems):
    """The instance stops cleanly, so the socket contract holds end to end."""
    reply = session.call("control.quit")
    if reply.get("ok") is not True:
        problems.add("control.quit answered: %r" % (reply,))
        return
    session.client.close()
    exited, code, waited = instance.wait_for_exit(H.QUIT_TIMEOUT)
    problems.require(exited and code == 0,
                     "control.quit must stop the instance: exited=%r code=%r after %.1fs"
                     % (exited, code, waited))


def report(problems):
    if problems.report("the crash-reporter socket proof has %d failed check(s)" % len(problems.items)):
        return 0
    return 1


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    if not os.path.exists(argv[1]):
        print("cannot run: no binary at %s" % argv[1])
        return 2
    problems = H.Problems()
    transcript = H.Transcript()
    print("instance: %s" % argv[1])
    with H.start_instance(argv[1]) as instance:
        H.wait_for_socket(instance)
        client = H.connect(instance)
        H.wait_ready(instance, client, transcript)
        session = Session(client, transcript)
        try:
            print("report dir: %s" % os.path.join(instance.workspace, REPORT_DIR_NAME))
            check_fresh_state(session, instance, problems)
            check_nothing_to_do(session, problems)
            check_upload_refusal(session, problems)
            report_path = check_planted_report(session, instance, problems)
            if report_path is None:
                problems.add("the report fixture could not be planted, so the two writers were "
                             "not measured at all")
            else:
                check_acknowledge(session, problems, report_path)
                check_discard(session, problems, report_path)
            check_transactions(session, problems)
            check_quit(session, instance, problems)
        finally:
            transcript.dump()
    code = report(problems)
    if code == 0:
        H.ok("crash-reporter socket proof (every check held)")
    return code


if __name__ == "__main__":
    sys.exit(main(sys.argv))
