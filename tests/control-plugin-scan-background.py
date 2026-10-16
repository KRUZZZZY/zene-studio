#!/usr/bin/env python3
"""END-TO-END proof of the background plug-in rescan (Q050-090): the scan runs
without blocking the window, with progress, cancellation and the cached
catalogue still served - and plugin.list carries each device's vendor.

THE CLAIM UNDER TEST (plans/QUEUE-050.md Q050-090, "Plug-in manager vendor and
background scan"). Two halves, both driven over --control-socket against the
REAL binary (QT_QPA_PLATFORM=offscreen, the shared control_socket_harness):

  * VENDOR: every plugin.list entry carries `vendor` - who made it, as its own
    format reports it ("Zene Studio" for a built-in, the LADSPA maker, the LV2
    author, the VST3/CLAP factory's vendor; empty when the format reports
    none). This is the same string the device picker shows, minus its
    "Not reported" words.
  * BACKGROUND SCAN: plugin.scan_start begins a run (one file per event-loop
    turn), plugin.scan_status reports active/progress/per-file outcomes while
    plugin.list keeps serving the PREVIOUS catalogue, plugin.scan_cancel stops
    the run and keeps that catalogue, and the run appears in task.list with
    its progress. A paced run (pace_ms) makes progress observable and a
    cancel deterministic however many plugin files this build has - even zero.

WHAT IT ASSERTS, in numbers:
  * before any run, scan_status reports active=false, outcome "never",
    progress 0 and task_id -1;
  * plugin.list entries all carry a string `vendor`; the built-in amplifier's
    vendor is "Zene Studio";
  * pace_ms outside 0..1000 (and a non-number) is typed invalid_args;
  * scan_start answers task_id, total_files and the catalogue_count it keeps
    serving; a second start while running is typed busy; plugin.rescan while
    running is typed busy and scans nothing;
  * DURING the run, plugin.list still answers the exact pre-run catalogue
    (names compared), and scan_status shows active with 0 <= progress <= 1;
  * scan_cancel answers cancelling=true; polled scan_status reaches outcome
    "cancelled" (bounded wait), with done_files <= total_files;
  * after the cancel, plugin.list is STILL the pre-run catalogue (the proof
    the window kept working on cached results), and scan_cache_list answers
    with sorted entries (cache read-back);
  * a follow-up unpaced run reaches outcome "done" with progress 1.0,
    len(files) == total_files and every file status in the run vocabulary;
  * task.list holds plugin_scan rows (the cancelled and the done run);
  * control.transactions records plugin.scan_start as irreversible with a
    non-empty mechanism;
  * after a finished run, scan_cancel is typed refused ("already ended");
  * control.quit stops the instance cleanly.

Usage:
    QT_QPA_PLATFORM=offscreen python3 control-plugin-scan-background.py <zene-binary>

Exit codes: 0 every check held; 1 a check failed (the app log is printed);
2 cannot run (no binary at the path).
"""

from __future__ import annotations

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import control_socket_harness as H  # noqa: E402  (path set above)

#: pace_ms bounds the engine documents (ControlCommandsPluginScanBackground.cpp).
MAX_PACE_MS = 1000
#: The per-file statuses a run reports (scanFileJson's vocabulary).
RUN_STATUSES = ("served-from-cache", "scanned", "not-a-plugin", "load-failed")
#: How long a poll loop waits for the run to move (seconds).
POLL_BOUND = 30.0


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


def plugin_directory(binary):
    """The build's own plugin modules, when they sit beside the binary."""
    candidate = os.path.join(os.path.dirname(os.path.abspath(binary)), "plugins")
    return candidate if os.path.isdir(candidate) else None


def device_names(catalogue):
    """Every device name plugin.list offers."""
    return sorted(entry.get("name") for entry in catalogue.get("devices") or [])


def wait_until(session, wanted, problems, what):
    """Poll scan_status until outcome is in `wanted`, or record a failure."""
    deadline = time.time() + POLL_BOUND
    seen = None
    while time.time() < deadline:
        seen = session.result("plugin.scan_status")
        if seen.get("outcome") in wanted:
            return seen
        time.sleep(0.05)
    problems.add("timed out waiting for the scan to %s (wanted %r, last saw %r)"
                 % (what, wanted, seen))
    return seen or {}


# ---------------------------------------------------------------------------
# the checks
# ---------------------------------------------------------------------------


def check_never_run(session, problems):
    """Before any run, the status is rest, not a half-answered question."""
    status = session.result("plugin.scan_status")
    problems.require(status.get("active") is False,
                     "no run yet: active must be false: %r" % (status,))
    problems.require(status.get("outcome") == "never",
                     "no run yet: outcome must be 'never': %r" % (status,))
    problems.require(status.get("progress") == 0,
                     "no run yet: progress must be 0: %r" % (status,))
    problems.require(status.get("task_id") == -1,
                     "no run yet: task_id must be -1: %r" % (status,))


def check_vendor(session, problems):
    """Every listed device names its vendor; a built-in names Zene Studio."""
    catalogue = session.result("plugin.list")
    devices = catalogue.get("devices") or []
    problems.require(len(devices) > 0, "plugin.list must offer devices: %r" % (catalogue,))
    missing = [entry.get("name") for entry in devices if not isinstance(entry.get("vendor"), str)]
    problems.require(not missing,
                     "every device must carry a string vendor: missing on %r" % (missing[:5],))
    amplifier = [entry for entry in devices if entry.get("name") == "amplifier"]
    problems.require(len(amplifier) == 1 and amplifier[0].get("vendor") == "Zene Studio",
                     "the built-in amplifier's vendor must be 'Zene Studio': %r" % (amplifier,))
    return catalogue


def check_pace_bounds(session, problems):
    """A pace outside 0..1000 (or not a number) is invalid_args, not a stall."""
    for bad in (-1, MAX_PACE_MS + 1, "fast"):
        refused = session.typed_error("plugin.scan_start", {"pace_ms": bad})
        problems.require(refused.get("kind") == "invalid_args",
                         "pace_ms=%r must be invalid_args: %r" % (bad, refused))


def check_start_and_busy(session, problems, catalogue_count):
    """The run starts, and neither a second run nor a sync rescan may disturb it."""
    started = session.result("plugin.scan_start", {"pace_ms": 150})
    problems.require(isinstance(started.get("task_id"), int) and started.get("task_id") >= 0,
                     "scan_start must answer a task id: %r" % (started,))
    problems.require(started.get("total_files", -1) >= 0,
                     "scan_start must answer total_files: %r" % (started,))
    problems.require(started.get("catalogue_count") == catalogue_count,
                     "the run must keep serving the pre-run catalogue (%r), not %r"
                     % (catalogue_count, started.get("catalogue_count")))
    second = session.typed_error("plugin.scan_start")
    problems.require(second.get("kind") == "busy",
                     "a second scan_start while running is busy: %r" % (second,))
    rescan = session.typed_error("plugin.rescan")
    problems.require(rescan.get("kind") == "busy",
                     "plugin.rescan while a background run is active is busy: %r" % (rescan,))
    return started


def check_cached_catalogue_during_run(session, problems, names):
    """While the run is on, plugin.list still serves the cached catalogue."""
    status = session.result("plugin.scan_status")
    problems.require(status.get("active") is True,
                     "the run must be active right after scan_start: %r" % (status,))
    progress = status.get("progress", -1)
    problems.require(isinstance(progress, (int, float)) and 0 <= progress <= 1,
                     "progress must be 0..1 while running: %r" % (status,))
    problems.require(device_names(session.result("plugin.list")) == names,
                     "plugin.list must keep serving the pre-run catalogue during the run")
    problems.require(status.get("catalogue_count") == len(names),
                     "scan_status catalogue_count must be the served catalogue: %r" % (status,))


def check_cancel(session, problems, names):
    """Cancelling stops the run and keeps the cached catalogue and cache."""
    cancelled = session.result("plugin.scan_cancel")
    problems.require(cancelled.get("cancelling") is True,
                     "scan_cancel must answer cancelling=true: %r" % (cancelled,))
    status = wait_until(session, ("cancelled",), problems, "cancel")
    problems.require(status.get("active") is False,
                     "after the cancel the run must be inactive: %r" % (status,))
    problems.require(status.get("done_files", -1) <= status.get("total_files", -2),
                     "done_files cannot pass total_files: %r" % (status,))
    problems.require(device_names(session.result("plugin.list")) == names,
                     "after the cancel plugin.list must STILL serve the pre-run catalogue")
    listing = session.result("plugin.scan_cache_list")
    entries = listing.get("entries") or []
    problems.require(listing.get("count") == len(entries),
                     "the cache must still answer read-back after the cancel: %r" % (listing,))
    paths = [entry.get("path") for entry in entries]
    problems.require(paths == sorted(paths),
                     "the cache entries must stay in path order: %r" % (paths[:5],))
    return status


def check_done_run(session, problems):
    """An unpaced run finishes, reports every file, and lands in the task list."""
    session.result("plugin.scan_start")
    status = wait_until(session, ("done",), problems, "finish")
    problems.require(status.get("progress") == 1.0,
                     "a finished run reports progress 1.0: %r" % (status,))
    files = status.get("files") or []
    problems.require(len(files) == status.get("total_files") == status.get("done_files"),
                     "a finished run reports every file: %r" % (status,))
    bad = [entry for entry in files if entry.get("status") not in RUN_STATUSES]
    problems.require(not bad,
                     "every file status is in the run vocabulary: %r" % (bad[:3],))
    problems.require(isinstance(status.get("report"), str) and status.get("report"),
                     "a finished run reports its one-line summary: %r" % (status,))
    tasks = session.result("task.list").get("tasks") or []
    scans = [task for task in tasks if task.get("kind") == "plugin_scan"]
    states = sorted(task.get("state") for task in scans)
    problems.require("cancelled" in states and "done" in states,
                     "task.list must hold the cancelled and the done run: %r" % (states,))
    ended = session.typed_error("plugin.scan_cancel")
    problems.require(ended.get("kind") == "refused",
                     "scan_cancel after the run ended is refused: %r" % (ended,))
    # The guard is down again: a synchronous rescan runs after the background one.
    rescan = session.result("plugin.rescan")
    problems.require("plugin-scan:" in (rescan.get("report") or ""),
                     "plugin.rescan must run again after the background run: %r" % (rescan,))


def check_transactions(session, problems):
    """The A16 record: scan_start is irreversible with a mechanism."""
    records = [record for record in session.result("control.transactions").get("transactions") or []
               if record.get("command", "").startswith("plugin.scan_")]
    classes = {(record.get("command"), record.get("class"), bool(record.get("mechanism")))
               for record in records}
    problems.require(("plugin.scan_start", "irreversible", True) in classes,
                     "scan_start must record an irreversible transaction: %r" % (classes,))


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
    if problems.report("the background scan socket proof has %d failed check(s)" % len(problems.items)):
        return 0
    return 1


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    if not os.path.exists(argv[1]):
        print("cannot run: no binary at %s" % argv[1])
        return 2
    directory = plugin_directory(argv[1])
    extra_env = {"LMMS_PLUGIN_DIR": directory} if directory else {}
    problems = H.Problems()
    transcript = H.Transcript()
    print("instance:  %s" % argv[1])
    print("plugin dir: %s" % (directory or "(none: paced start/cancel need no plugin files)"))
    with H.start_instance(argv[1], extra_env=extra_env) as instance:
        H.wait_for_socket(instance)
        client = H.connect(instance)
        H.wait_ready(instance, client, transcript)
        session = Session(client, transcript)
        try:
            check_never_run(session, problems)
            catalogue = check_vendor(session, problems)
            names = device_names(catalogue)
            check_pace_bounds(session, problems)
            check_start_and_busy(session, problems, catalogue.get("total", len(names)))
            check_cached_catalogue_during_run(session, problems, names)
            check_cancel(session, problems, names)
            check_done_run(session, problems)
            check_transactions(session, problems)
            check_quit(session, instance, problems)
        finally:
            transcript.dump()
    code = report(problems)
    if code == 0:
        H.ok("background plugin-scan socket proof (every check held)")
    return code


if __name__ == "__main__":
    sys.exit(main(sys.argv))
