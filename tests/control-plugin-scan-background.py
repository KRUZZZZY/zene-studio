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
  * BACKGROUND SCAN: plugin.scan_start marks a run active, shows it in the
    task area and returns BEFORE touching a directory or a library - the first
    turn plans the files, each following turn pre-loads one library or scans
    one file. plugin.scan_status reports active/progress/per-file outcomes
    while plugin.list keeps serving the PREVIOUS catalogue, plugin.scan_cancel
    stops the run and keeps that catalogue, and the run appears in task.list
    with its progress. A paced run (pace_ms) makes progress observable and a
    cancel deterministic however many plugin files this build has - even zero.

WHAT IT ASSERTS, in numbers:
  * before any run, scan_status reports active=false, outcome "never",
    progress 0 and task_id -1;
  * plugin.list entries all carry a string `vendor`; the built-in amplifier's
    vendor is "Zene Studio";
  * pace_ms outside 0..1000 (and a non-number) is typed invalid_args;
  * scan_start answers task_id and the catalogue_count it keeps serving, with
    total_files 0 (the count is planned on the run's first turn, read through
    scan_status); a second start while running is typed busy; plugin.rescan
    while running is typed busy and scans nothing;
  * right after scan_start the unplanned run (total_files 0) reports scan_status progress 0 and a
    task.list row with progress -1 ("cannot say yet"), never 1.0;
  * the test adds a candidate file (CANDIDATE_NAME) AFTER startup, so it is absent from the
    pre-run cache, plus a trailer (TRAILER_NAME) in the same directory that the scanner's
    path order puts after it; the run completes the candidate while still running, and after
    the cancel the candidate's path is in scan_cache_list - a newly scanned record, not one
    cached before the run;
  * every path comparison goes through path_key() (os.path.normcase(os.path.realpath(...))
    on both sides), and path_key itself is checked first against Windows-style and POSIX
    inputs (drive-letter case, separators, '.'/'..', trailing separators, symlinks);
  * the first turn plans the files (polled scan_status shows total_files > 0);
    DURING the run, plugin.list still answers the exact pre-run catalogue
    (names compared), and scan_status shows active with 0 <= progress <= 1;
  * once the candidate has completed, progress is NONZERO while the run is still
    active (the proof the window watched real work, not a 0 file count);
  * scan_cancel then answers cancelling=true (cancel AFTER the candidate
    completed) and polled scan_status reaches outcome "cancelled"
    (bounded wait), with 1 <= done_files < total_files;
  * after the cancel, plugin.list is STILL the pre-run catalogue (the proof
    the window kept working on cached results), scan_cache_list answers with
    sorted entries (cache read-back), and EVERY completed file's path is in
    the cache (the run's newly scanned records remain cached);
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

import ntpath
import os
import posixpath
import shutil
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import control_socket_harness as H  # noqa: E402  (path set above)

#: pace_ms bounds the engine documents (ControlCommandsPluginScanBackground.cpp).
MAX_PACE_MS = 1000
#: The per-file statuses a run reports (scanFileJson's vocabulary).
RUN_STATUSES = ("served-from-cache", "scanned", "not-a-plugin", "load-failed")
#: The candidate the test adds AFTER startup: absent from the pre-run cache.
#: Not a library, so the scan stores a "load-failed" or "not-a-plugin" record
#: for it - a record that exists only if THIS run took it. The name must match
#: what the scanner enumerates on the platform (PluginFactory.cpp: "*.dll" on
#: Windows, "lib*.so" everywhere else), or the run never plans it.
CANDIDATE_NAME = ("0-q050-090-fresh-candidate.dll" if sys.platform == "win32"
                  else "lib0-q050-090-fresh-candidate.so")
#: A second non-library in the SAME directory, named so it comes after the
#: candidate in the scanner's order. The scanner sorts by
#: QFileInfo::absoluteFilePath() (PluginFactoryBackgroundScan.cpp), and two
#: files of one directory share that path up to their names, so "0-..." before
#: "1-..." holds in every path spelling on every platform. It guarantees the run
#: still has a file to do after the candidate completes, so the cancel always
#: lands on a RUNNING run wherever the platform's paths put the directory.
TRAILER_NAME = ("1-q050-090-trailer.dll" if sys.platform == "win32"
                else "lib1-q050-090-trailer.so")
#: Paced start used for the whole cancelled run (the engine's maximum): the first
#: turn - the one that plans the files - comes this long after scan_start, so the
#: reads right after it see the unplanned run.
START_PACE_MS = 1000
#: How long a poll loop waits for the run to move (seconds).
POLL_BOUND = 30.0


def path_key(path, flavour=os.path):
    """The one spelling two paths are compared in, whoever reported them.

    The scanner reports QFileInfo::absoluteFilePath() (on Windows: "/"
    separators, an upper-case drive letter, no symlink or 8.3-name resolution);
    Python builds its paths with the native separator and whatever case and
    links the binary path came with. Both sides go through this same function:
    realpath resolves "."/"..", trailing separators and links (and 8.3 short
    names on Windows), normcase folds case and separators where the file system
    does (Windows) and is the identity where it does not (POSIX). `flavour` is
    os.path in the test; check_path_key_contract passes ntpath/posixpath to pin
    the behaviour on both platforms' spellings from any host.
    """
    if not isinstance(path, str) or not path:
        return ""
    return flavour.normcase(flavour.realpath(path))


def path_keys(paths):
    """path_key of every non-empty path in `paths`, as a set."""
    return set(path_key(path) for path in paths if path)


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


def wait_while(session, predicate, problems, what, bound=POLL_BOUND):
    """Poll scan_status until `predicate(status)` holds, or record a failure."""
    deadline = time.time() + bound
    seen = None
    while time.time() < deadline:
        seen = session.result("plugin.scan_status")
        if predicate(seen):
            return seen
        time.sleep(0.05)
    problems.add("timed out waiting for the scan status to %s (last saw %r)"
                 % (what, seen))
    return seen or {}


# ---------------------------------------------------------------------------
# the checks
# ---------------------------------------------------------------------------


#: Pairs one path_key must make EQUAL, per path flavour: (Qt's spelling, Python's).
SAME_PATH = {
    ntpath: (
        # Qt's "/" and upper-case drive against Python's native "\\" and any case.
        ("C:/Users/runner/build/.zene-scan-plugins-ab12/0-q050-090-fresh-candidate.dll",
         "c:\\Users\\runner\\build\\.zene-scan-plugins-ab12\\0-q050-090-fresh-candidate.dll"),
        # os.path.join on Windows mixes the two separators.
        ("C:/Users/runner/build/.zene-scan-plugins-ab12/0-q050-090-fresh-candidate.dll",
         "C:/Users/runner/build\\.zene-scan-plugins-ab12\\0-q050-090-fresh-candidate.dll"),
        # NTFS compares names without case.
        ("C:/Users/Runner/Build/plugins/AMPLIFIER.dll", "c:\\users\\runner\\build\\plugins\\amplifier.dll"),
        # "." and ".." segments and a trailing separator.
        ("C:/Users/runner/build/plugins/", "C:\\Users\\runner\\build\\.\\x\\..\\plugins"),
    ),
    posixpath: (
        ("/srv/build/plugins/", "/srv/build/./x/../plugins"),
        ("/srv/build/.zene-scan-plugins-ab12/lib0-q050-090-fresh-candidate.so",
         "/srv/build//.zene-scan-plugins-ab12/lib0-q050-090-fresh-candidate.so"),
    ),
}
#: Pairs one path_key must keep DISTINCT, per path flavour.
DIFFERENT_PATH = {
    ntpath: (
        ("C:/Users/runner/build/plugins/a.dll", "C:/Users/runner/build/plugins/b.dll"),
        ("C:/Users/runner/build/plugins/a.dll", "D:/Users/runner/build/plugins/a.dll"),
    ),
    posixpath: (
        # POSIX file systems here are case-sensitive: two names, two files.
        ("/srv/build/plugins/libA.so", "/srv/build/plugins/liba.so"),
        ("/srv/build/plugins/liba.so", "/srv/build/plugin/liba.so"),
    ),
}


def check_path_key_contract(problems):
    """path_key gives one spelling for one file and keeps two files apart, for
    Windows-style and POSIX inputs alike - checked on whatever host runs the
    test, so the Windows normalisation is exercised on Linux too."""
    for flavour, pairs in SAME_PATH.items():
        for left, right in pairs:
            problems.require(path_key(left, flavour) == path_key(right, flavour),
                             "path_key (%s) must equate %r and %r: %r != %r"
                             % (flavour.__name__, left, right, path_key(left, flavour),
                                path_key(right, flavour)))
    for flavour, pairs in DIFFERENT_PATH.items():
        for left, right in pairs:
            problems.require(path_key(left, flavour) != path_key(right, flavour),
                             "path_key (%s) must keep %r and %r apart"
                             % (flavour.__name__, left, right))
    # A link to the directory names the same file. Windows needs a privilege to
    # make one, so this half runs where links are unprivileged; the Windows
    # resolution itself is os.path.realpath's (8.3 names, junctions).
    if sys.platform != "win32":
        root = tempfile.mkdtemp(prefix="zene-path-key-")
        try:
            real = os.path.join(root, "real")
            os.mkdir(real)
            os.symlink(real, os.path.join(root, "link"))
            problems.require(path_key(os.path.join(root, "link", "x.so"))
                             == path_key(os.path.join(real, "x.so")),
                             "path_key must resolve a symlinked directory")
        finally:
            shutil.rmtree(root, ignore_errors=True)


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
    """The run starts active before any file work, and neither a second run
    nor a sync rescan may disturb it."""
    started = session.result("plugin.scan_start", {"pace_ms": START_PACE_MS})
    problems.require(isinstance(started.get("task_id"), int) and started.get("task_id") >= 0,
                     "scan_start must answer a task id: %r" % (started,))
    # The count is planned on the run's first turn (scan_start returns before
    # any directory or library work), so the reply carries 0 and the status
    # carries the planned count.
    problems.require(started.get("total_files") == 0,
                     "scan_start must answer total_files 0 before the first turn plans: %r"
                     % (started,))
    problems.require(started.get("catalogue_count") == catalogue_count,
                     "the run must keep serving the pre-run catalogue (%r), not %r"
                     % (catalogue_count, started.get("catalogue_count")))
    # The unplanned run (total_files 0) must not look finished: the task row
    # says -1 ("cannot say yet"), never 1.0, and the status says progress 0.
    # task.list is read FIRST, so a status that still shows total_files 0
    # afterwards proves the row was read before the first turn planned.
    rows = [task for task in session.result("task.list").get("tasks") or []
            if task.get("kind") == "plugin_scan" and task.get("state") == "running"]
    initial = session.result("plugin.scan_status")
    problems.require(initial.get("total_files") == 0 and initial.get("active") is True,
                     "the first observation must see the run before its planning turn "
                     "(raise START_PACE_MS if the host is too slow): %r" % (initial,))
    problems.require(initial.get("progress") == 0,
                     "an unplanned run reports progress 0: %r" % (initial,))
    problems.require(len(rows) == 1 and rows[0].get("progress") == -1,
                     "an unplanned run's task row must report progress -1 (not 1.0): %r" % (rows,))
    second = session.typed_error("plugin.scan_start")
    problems.require(second.get("kind") == "busy",
                     "a second scan_start while running is busy: %r" % (second,))
    rescan = session.typed_error("plugin.rescan")
    problems.require(rescan.get("kind") == "busy",
                     "plugin.rescan while a background run is active is busy: %r" % (rescan,))
    # The first turn plans the files: the status names a positive count while
    # the run is still on (a build with no plugin files plans 0 and ends done).
    planned = wait_while(session,
                         lambda status: status.get("total_files", 0) > 0
                         or status.get("outcome") != "running",
                         problems, "plan the files")
    # The fresh candidate guarantees at least one planned file, so a run that is
    # not active with files planned means the candidate was not enumerated: a
    # failure, never a quiet pass without the cancel proof.
    problems.require(planned.get("active") is True and planned.get("outcome") == "running"
                     and planned.get("total_files", 0) > 0,
                     "after planning the run must be active with the candidate planned: %r"
                     % (planned,))
    return started


def completed_keys(status):
    """path_key of every file the run reports completed."""
    return path_keys(entry.get("path") for entry in status.get("files") or [])


def check_cached_catalogue_during_run(session, problems, names, candidate):
    """While the run is on, plugin.list still serves the cached catalogue,
    and once the candidate has completed the progress is nonzero."""
    status = session.result("plugin.scan_status")
    problems.require(status.get("outcome") == "running",
                     "the paced run must still be running here (cancellation cannot be skipped): %r"
                     % (status,))
    problems.require(status.get("active") is True,
                     "the run must be active right after scan_start: %r" % (status,))
    progress = status.get("progress", -1)
    problems.require(isinstance(progress, (int, float)) and 0 <= progress <= 1,
                     "progress must be 0..1 while running: %r" % (status,))
    problems.require(device_names(session.result("plugin.list")) == names,
                     "plugin.list must keep serving the pre-run catalogue during the run")
    problems.require(status.get("catalogue_count") == len(names),
                     "scan_status catalogue_count must be the served catalogue: %r" % (status,))
    # The window watched real work: the candidate completed with progress
    # still strictly between 0 and 1 while the run is on. What the cancel
    # proof needs is exactly that - the candidate DONE and the run still
    # RUNNING - so that is what is waited for and asserted, not a path order.
    # The private directory sits beside the binary under a dot-name, which
    # puts the candidate first in practice; if a platform's paths order it
    # later, the wait may take up to one pace per planned file (pre-load plus
    # scan turns) before failing, and the trailer behind it keeps the run
    # running once it is done.
    pace_seconds = START_PACE_MS / 1000.0
    bound = POLL_BOUND + 2 * pace_seconds * status.get("total_files", 0)
    wanted = path_key(candidate)
    status = wait_while(session,
                        lambda seen: wanted in completed_keys(seen)
                        or seen.get("outcome") != "running",
                        problems, "complete the candidate", bound)
    problems.require(status.get("outcome") == "running" and wanted in completed_keys(status),
                     "the candidate must have completed while the run is still running, before "
                     "the cancel: %r" % (status,))
    problems.require(status.get("progress", 0) > 0,
                     "progress must be nonzero after a completed file: %r" % (status,))
    problems.require(device_names(session.result("plugin.list")) == names,
                     "plugin.list must STILL serve the pre-run catalogue after a file: %r"
                     % (device_names(session.result("plugin.list")),))
    return status


def check_cancel(session, problems, names, pre_cache_keys, pre_cache_count, candidate):
    """Cancelling stops the run after real work and keeps the cached catalogue
    and the newly scanned records."""
    cancelled = session.result("plugin.scan_cancel")
    problems.require(cancelled.get("cancelling") is True,
                     "scan_cancel must answer cancelling=true: %r" % (cancelled,))
    status = wait_until(session, ("cancelled",), problems, "cancel")
    problems.require(status.get("active") is False,
                     "after the cancel the run must be inactive: %r" % (status,))
    problems.require(status.get("done_files", 0) >= 1,
                     "the cancel must land after at least one completed file: %r" % (status,))
    # The trailer was still to do when the cancel landed.
    problems.require(status.get("done_files", -1) < status.get("total_files", -2),
                     "the cancel must stop the run before its last file: %r" % (status,))
    problems.require(device_names(session.result("plugin.list")) == names,
                     "after the cancel plugin.list must STILL serve the pre-run catalogue")
    listing = session.result("plugin.scan_cache_list")
    entries = listing.get("entries") or []
    problems.require(listing.get("count") == len(entries),
                     "the cache must still answer read-back after the cancel: %r" % (listing,))
    paths = [entry.get("path") for entry in entries]
    # The engine's own order, compared in the engine's own spelling (every
    # string here came from the scanner), so no normalisation applies.
    problems.require(paths == sorted(paths),
                     "the cache entries must stay in path order: %r" % (paths[:5],))
    # Identity from here on is by path_key on BOTH sides: the candidate is a
    # Python-built path, the cache and run paths are the scanner's.
    cache_keys = path_keys(paths)
    wanted = path_key(candidate)
    # The proof a NEWLY scanned record survives the cancel: the candidate was
    # not in the pre-run cache, this run completed it, and its record is in
    # scan_cache_list now (a record cached before the run could not show it).
    problems.require(wanted not in pre_cache_keys,
                     "the candidate must be absent from the pre-run cache: %r" % (candidate,))
    files = status.get("files") or []
    scanned = [entry for entry in files if path_key(entry.get("path")) == wanted]
    problems.require(len(scanned) == 1 and scanned[0].get("status") in ("load-failed", "not-a-plugin"),
                     "the run must have completed the new candidate before the cancel: %r"
                     % (files,))
    problems.require(wanted in cache_keys,
                     "the newly scanned candidate's record must be in scan_cache_list after the "
                     "cancel: %r not in %r" % (candidate, paths[:5]))
    # Every completed file stored its record live as it was taken (scanned,
    # served, not-a-plugin or load-failed), so the cancelled run's
    # measurements remain in the cache for the next run to serve.
    problems.require(len(files) >= 1,
                     "the cancelled run must report its completed files: %r" % (status,))
    missing = [entry.get("path") for entry in files if path_key(entry.get("path")) not in cache_keys]
    problems.require(not missing,
                     "every completed file must stay cached after the cancel: missing %r" % (missing[:3],))
    problems.require(listing.get("count", -1) >= pre_cache_count,
                     "the cancel must not lose cache records (%r before, %r after)"
                     % (pre_cache_count, listing.get("count")))
    problems.require(pre_cache_keys <= cache_keys,
                     "the cancel must keep the pre-run cache records")
    return status


def check_done_run(session, problems, expect_cancelled):
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
    problems.require("done" in states,
                     "task.list must hold the done run: %r" % (states,))
    if expect_cancelled:
        problems.require("cancelled" in states,
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
    # A per-test plugin directory for the candidate: LMMS_PLUGIN_DIR is one more
    # search path on top of the "plugins" directory beside the binary (which
    # still supplies the real modules), and only THIS instance has it in its
    # environment, so no other test that scans build/plugins can ever see the
    # candidate file.
    directory = plugin_directory(argv[1])
    # The run scans files in the order of the scanner's absolute paths, so the
    # directory sits beside the binary under a dot-name ("." sorts before
    # "plugins"): the candidate is then the run's first file in practice. Nothing
    # asserts that order; the system temp directory is the fallback, where the
    # wait in check_cached_catalogue_during_run is bounded by the planned count
    # and the trailer keeps the run going past the candidate.
    try:
        scratch = tempfile.mkdtemp(prefix=".zene-scan-plugins-",
                                   dir=os.path.dirname(os.path.abspath(argv[1])))
    except OSError:
        scratch = tempfile.mkdtemp(prefix="zene-scan-plugins-")
    extra_env = {"LMMS_PLUGIN_DIR": scratch}
    candidate = os.path.join(os.path.abspath(scratch), CANDIDATE_NAME)
    trailer = os.path.join(os.path.abspath(scratch), TRAILER_NAME)
    problems = H.Problems()
    check_path_key_contract(problems)
    transcript = H.Transcript()
    print("instance:  %s" % argv[1])
    print("plugin dir: %s (modules beside the binary: %s)" % (scratch, directory or "none"))
    with H.start_instance(argv[1], extra_env=extra_env) as instance:
        H.wait_for_socket(instance)
        client = H.connect(instance)
        H.wait_ready(instance, client, transcript)
        session = Session(client, transcript)
        try:
            check_never_run(session, problems)
            catalogue = check_vendor(session, problems)
            names = device_names(catalogue)
            pre_cache = session.result("plugin.scan_cache_list")
            pre_cache_keys = path_keys(entry.get("path") for entry in pre_cache.get("entries") or [])
            pre_cache_count = pre_cache.get("count", 0)
            # The candidate and the trailer appear AFTER the startup scan filled
            # the cache, so only a run that really scans one can put its record
            # there.
            for name, made in ((candidate, b"candidate"), (trailer, b"trailer")):
                with open(name, "wb") as handle:
                    handle.write(b"not a library: q050-090 cancel proof " + made + b"\n")
            check_pace_bounds(session, problems)
            check_start_and_busy(session, problems, catalogue.get("total", len(names)))
            check_cached_catalogue_during_run(session, problems, names, candidate)
            # The cancel always runs; a run that is not running here already
            # failed the checks above.
            check_cancel(session, problems, names, pre_cache_keys, pre_cache_count,
                         candidate)
            check_done_run(session, problems, True)
            check_transactions(session, problems)
            check_quit(session, instance, problems)
        finally:
            transcript.dump()
            for name in (candidate, trailer):
                try:
                    os.remove(name)
                except OSError:
                    pass
            shutil.rmtree(scratch, ignore_errors=True)
    code = report(problems)
    if code == 0:
        H.ok("background plugin-scan socket proof (every check held)")
    return code


if __name__ == "__main__":
    sys.exit(main(sys.argv))
