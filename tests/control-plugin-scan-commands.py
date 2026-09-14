#!/usr/bin/env python3
"""END-TO-END proof that the plugin scan cache and its quarantine list are
OBSERVABLE and OPERABLE by an agent - and that the quarantine no longer needs a
hand-edited JSON file.

THE CLAIM UNDER TEST (docs/FEATURE-LIST-0.3.0.md row 46, "Plugin scan cache and
quarantine"). The audit's row says the engine is "in the tree with a registered
`PluginScanCacheTest`; no command group, and the documented quarantine route is
hand-editing a JSON file". This transcript is the group's proof, and it asserts
the two halves separately:

  * OBSERVABLE: plugin.scan_cache_get_state / scan_cache_list / scan_cache_lookup
    report the cache file and its flags, the records it holds (fingerprint,
    status, descriptor metadata), one file's record with the engine's own
    fingerprint comparison (`cached` vs `stale`) and the quarantine list.
  * OPERABLE: plugin.scan_cache_quarantine_add / scan_cache_quarantine_remove
    write the list through PluginScanCache's own API and the cache file, and
    plugin.rescan applies it - so a quarantined plugin is really gone from the
    device catalogue, and comes back when it is un-quarantined.

WHY IT IS A REGISTERED CTEST AND NOT A UNIT TEST. tests/src/core/
PluginScanCacheTest.cpp already proves the store in process: the JSON
round-trip, the fingerprint miss, the quarantine surviving save/load, the
corrupt-cache degradation. What it cannot prove is the release contract's
section 3.1 - that the capability is reachable THROUGH THE SOCKET by an agent -
nor that the file ON DISK changes when a command says it did. So this runs the
REAL binary ($<TARGET_FILE:zene>, QT_QPA_PLATFORM=offscreen, the shared
control_socket_harness) and reads the state back off the wire AND off disk.

WHAT IT ASSERTS, in numbers:
  * a fresh instance: the cache is persistent, its file is named
    plugin-scan-cache.json under the instance's working directory, its format
    version is 1, entry_count/quarantine_count are counts of what list/get_state
    return, and the last scan's own report names it (candidate files, served
    from cache, scanned, descriptors);
  * the entries are SORTED by path and each status is one of the three the
    engine serialises; the status counts add up to count;
  * lookup: a path that was never scanned has no record, `cached` false,
    `stale` false; a record that IS in the cache is `cached` true with its
    record returned; an empty path is a typed invalid_args;
  * the WRITE path: quarantine_add writes the entry, the command's result and
    the CACHE FILE ON DISK both name it with its reason, get_state reports it,
    and control.undo dispatches the recorded inverse and takes it off both;
  * THE REASON IS NOT LOST: removing an entry and undoing RESTORES ITS REASON,
    because the removal captures it before the write - and the negative control
    runs the other way, showing that a path-only re-add comes back with an EMPTY
    reason, which is what a naive inverse would have recorded;
  * the quarantine is OPERATIVE, not just stored: with a real module from the
    build's plugin directory quarantined, plugin.rescan reports it as skipped
    and plugin.list no longer offers the device it provided; removing it and
    rescanning brings it back;
  * every mutating id records an A16 transaction with a non-empty mechanism,
    and control.undo on plugin.rescan FAILS, typed `irreversible`, naming the
    fallback instead of silently unwinding an older step.

THE BOUNDS THIS TEST STATES RATHER THAN HIDES: the records the cache holds are
files the SCAN found, so on an instance whose plugin search paths are empty the
list is empty and the "quarantine hides a real plugin" checks are reported as
SKIPPED rather than passed (they are driven with LMMS_PLUGIN_DIR pointed at the
build's own plugins/ when it exists). The fingerprint `stale` distinction is
proven in process by PluginScanCacheTest.testRecordKeepsTheFingerprintLookupRejects,
because making a record stale here would mean modifying a built module.

Usage:
    QT_QPA_PLATFORM=offscreen python3 control-plugin-scan-commands.py <zene-binary>

Exit codes: 0 every check held; 1 a check failed (the app log is printed);
2 cannot run (no binary at the path).
"""

from __future__ import annotations

import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import control_socket_harness as H  # noqa: E402  (path set above)

#: The file PluginScanCache::defaultFilePath() names, under the working directory.
CACHE_FILE_NAME = "plugin-scan-cache.json"
#: The three statuses PluginScanCache serialises (statusToString, PluginScanCache.cpp).
STATUS_VALUES = ("has-descriptor", "not-a-plugin", "load-failed")
#: A reason long enough that a path-only re-add cannot guess it.
REASON = "hangs the host on load - measured by control-plugin-scan-commands.py"


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
    """The build's own plugin modules, when they sit beside the binary.

    Pointing the instance at them is what makes "the quarantine HIDES a plugin"
    measurable rather than assumed; without them the cache still works (it is a
    list of files the scan found) and the dependent checks report SKIPPED.
    """
    candidate = os.path.join(os.path.dirname(os.path.abspath(binary)), "plugins")
    return candidate if os.path.isdir(candidate) else None


def read_cache_file(path):
    """The cache as it is ON DISK, or None when it is not there or is not JSON."""
    try:
        with open(path, encoding="utf-8") as handle:
            return json.load(handle)
    except (OSError, ValueError):
        return None


def entries_by_path(entries):
    """{path: entry} for a list of quarantine entries."""
    return {entry.get("path"): entry for entry in entries or []}


def device_names(catalogue):
    """Every device name plugin.list offers."""
    return {entry.get("name") for entry in catalogue.get("devices") or []}


def first_descriptor_record(listing):
    """The first record that is a real plugin, with the path it describes."""
    for entry in listing.get("entries") or []:
        if entry.get("status") == "has-descriptor" and entry.get("name"):
            return entry
    return None


# ---------------------------------------------------------------------------
# the checks
# ---------------------------------------------------------------------------


def check_state(session, instance, problems):
    """The cache's own state, and the boundary it lives behind."""
    state = session.result("plugin.scan_cache_get_state")
    cache_file = state.get("cache_file") or ""
    problems.require(state.get("persistent") is True,
                     "the cache must be persistent: %r" % (state.get("cache_file"),))
    problems.require(cache_file.endswith(CACHE_FILE_NAME)
                     and os.path.dirname(cache_file) == instance.workspace,
                     "the cache file must be %s under the working directory, not %r"
                     % (CACHE_FILE_NAME, cache_file))
    problems.require(state.get("format_version") == 1,
                     "format_version: %r" % (state.get("format_version"),))
    problems.require(state.get("quarantine_count") == 0
                     and state.get("quarantine") == [],
                     "a fresh instance has an empty quarantine list: %r" % (state.get("quarantine"),))
    scan = state.get("last_scan") or {}
    problems.require(isinstance(scan, dict) and scan.get("report", "").startswith("plugin-scan:"),
                     "the last scan's own report must be on the wire: %r" % (scan,))
    problems.require(scan.get("candidate_files", 0) >= scan.get("descriptors", 0),
                     "the scan cannot emit more descriptors than candidate files: scan=%r" % (scan,))
    return state


def check_list(session, state, problems):
    """The cache's CONTENTS, which is what this group added (records())."""
    listing = session.result("plugin.scan_cache_list")
    entries = listing.get("entries") or []
    problems.require(listing.get("count") == len(entries) == state.get("entry_count"),
                     "count, the entries and get_state's entry_count must agree: %r/%r/%r"
                     % (listing.get("count"), len(entries), state.get("entry_count")))
    paths = [entry.get("path") for entry in entries]
    problems.require(paths == sorted(paths), "the entries must be in path order: %r" % (paths[:5],))
    statuses = {entry.get("status") for entry in entries}
    problems.require(statuses <= set(STATUS_VALUES),
                     "every status is one the engine serialises: %r" % (statuses,))
    counted = (listing.get("has_descriptor_count", 0) + listing.get("not_a_plugin_count", 0)
               + listing.get("load_failed_count", 0))
    problems.require(counted == listing.get("count"),
                     "the three status counts must add up to count: %r/%r"
                     % (counted, listing.get("count")))
    return listing


def check_lookup(session, instance, listing, problems):
    """One file's record, and the engine's own fingerprint comparison."""
    missing = session.result("plugin.scan_cache_lookup",
                             {"path": os.path.join(instance.workspace, "libnever-scanned.so")})
    problems.require(missing.get("cached") is False and missing.get("stale") is False
                     and missing.get("record") is None and missing.get("file_exists") is False,
                     "a path that was never scanned has no record: %r" % (missing,))
    empty = session.typed_error("plugin.scan_cache_lookup", {"path": ""})
    problems.require(empty.get("kind") == "invalid_args",
                     "an empty path is invalid_args, not a silent miss: %r" % (empty,))

    record = first_descriptor_record(listing)
    if record is None:
        print("  SKIPPED: no plugin record in this instance's cache, so the positive lookup "
              "half (cached=true for a real module) could not be measured")
        return None
    found = session.result("plugin.scan_cache_lookup", {"path": record.get("path")})
    problems.require(found.get("file_exists") is True and found.get("cached") is True,
                     "a module the scan just recorded must be cached: %r" % (found,))
    returned = found.get("record") or {}
    problems.require(returned.get("name") == record.get("name")
                     and returned.get("status") == "has-descriptor",
                     "lookup must return the same record the list reports: %r vs %r"
                     % (returned.get("name"), record.get("name")))
    return record


def check_add(session, instance, problems, record):
    """The write: the command, the state, and the FILE ON DISK."""
    target = record.get("path") if record else os.path.join(instance.workspace, "libquarantine-me.so")
    added = session.result("plugin.scan_cache_quarantine_add", {"path": target, "reason": REASON})
    problems.require(added.get("quarantined") is True and added.get("reason") == REASON
                     and added.get("quarantine_count") == 1,
                     "quarantine_add must report the entry it wrote: %r" % (added,))
    state = session.result("plugin.scan_cache_get_state")
    problems.require(entries_by_path(state.get("quarantine")).get(target, {}).get("reason") == REASON,
                     "get_state must report the entry with its reason: %r" % (state.get("quarantine"),))

    # THE FILE. This is the half the audit's "hand-editing a JSON file" names:
    # the same state is now written by a command, and it is really on disk.
    on_disk = read_cache_file(state.get("cache_file"))
    problems.require(isinstance(on_disk, dict) and on_disk.get("version") == 1,
                     "the cache file must exist and carry the format version: %r" % (on_disk,))
    stored = entries_by_path((on_disk or {}).get("quarantine")).get(target)
    problems.require(stored is not None and stored.get("reason") == REASON,
                     "the quarantined path and its reason must be in the file on disk: %r" % (stored,))
    return target


def check_refusals(session, problems, target):
    """An edit that would change nothing is refused, and writes nothing."""
    again = session.typed_error("plugin.scan_cache_quarantine_add",
                               {"path": target, "reason": "a second reason"})
    problems.require(again.get("kind") == "refused",
                     "adding an already-quarantined path is refused: %r" % (again,))
    absent = session.typed_error("plugin.scan_cache_quarantine_remove",
                                 {"path": os.path.join(os.path.dirname(target), "libabsent.so")})
    problems.require(absent.get("kind") == "not_found",
                     "removing what is not quarantined is not_found: %r" % (absent,))
    no_path = session.typed_error("plugin.scan_cache_quarantine_add", {})
    problems.require(no_path.get("kind") == "invalid_args",
                     "an add with no path is invalid_args: %r" % (no_path,))


def check_undo_removes(session, problems, target):
    """The recorded inverse, dispatched by control.undo."""
    undone = session.result("control.undo")
    problems.require(undone.get("undone") is True
                     and undone.get("restored_by") == "plugin.scan_cache_quarantine_remove",
                     "control.undo must dispatch the recorded inverse command: %r" % (undone,))
    state = session.result("plugin.scan_cache_get_state")
    problems.require(target not in entries_by_path(state.get("quarantine")),
                     "the entry must be gone from the state after the undo: %r"
                     % (state.get("quarantine"),))
    on_disk = read_cache_file(state.get("cache_file"))
    problems.require(target not in entries_by_path((on_disk or {}).get("quarantine")),
                     "the entry must be gone from the FILE as well: %r"
                     % ((on_disk or {}).get("quarantine"),))
    # Undo again: the dispatch of the inverse recorded its own inverse, and that
    # one carries the reason - so the pair is a faithful toggle, not a one-way.
    back = session.result("control.undo")
    state = session.result("plugin.scan_cache_get_state")
    restored = entries_by_path(state.get("quarantine")).get(target) or {}
    problems.require(back.get("restored_by") == "plugin.scan_cache_quarantine_add"
                     and restored.get("reason") == REASON,
                     "a second undo re-adds the entry WITH its reason: %r / %r"
                     % (back.get("restored_by"), restored))
    session.result("plugin.scan_cache_quarantine_remove", {"path": target})


def check_reason_survives_remove_and_undo(session, problems, target):
    """THE TRAP: the reason is captured before the write and carried by the inverse."""
    session.result("plugin.scan_cache_quarantine_add", {"path": target, "reason": REASON})
    removed = session.result("plugin.scan_cache_quarantine_remove", {"path": target})
    problems.require(removed.get("reason") == REASON and removed.get("quarantined") is False,
                     "the removal must report the reason the entry carried: %r" % (removed,))
    undone = session.result("control.undo")
    state = session.result("plugin.scan_cache_get_state")
    restored = entries_by_path(state.get("quarantine")).get(target) or {}
    problems.require(undone.get("restored_by") == "plugin.scan_cache_quarantine_add"
                     and restored.get("reason") == REASON,
                     "the recorded inverse must restore the entry WITH its reason: %r / %r"
                     % (undone.get("restored_by"), restored))


def check_reason_negative_control(session, problems, target):
    """The negative control: a path-only re-add LOSES the reason.

    This is the shape a naive inverse would have recorded - the path and nothing
    else - driven directly, so the loss is measured rather than asserted about a
    hypothetical. If the reason could not be lost, the row's mechanism would be
    claiming a trap it does not have.
    """
    session.result("plugin.scan_cache_quarantine_remove", {"path": target})
    session.result("plugin.scan_cache_quarantine_add", {"path": target})
    state = session.result("plugin.scan_cache_get_state")
    naive = entries_by_path(state.get("quarantine")).get(target) or {}
    problems.require(naive.get("reason") == "",
                     "a path-only re-add must come back with an EMPTY reason "
                     "(that is what the recorded inverse exists to prevent): %r" % (naive,))
    session.result("plugin.scan_cache_quarantine_remove", {"path": target})


def check_rescan_hides_and_restores(session, problems, record):
    """plugin.rescan APPLIES the quarantine - measured against plugin.list."""
    if record is None:
        print("  SKIPPED: no real module record, so 'the quarantine hides a plugin' could not "
              "be measured (this instance's plugin search paths found no plugin files)")
        return
    target = record.get("path")
    name = record.get("name")
    session.result("plugin.scan_cache_quarantine_add", {"path": target, "reason": REASON})
    rescan = session.result("plugin.rescan")
    problems.require(rescan.get("quarantined", 0) >= 1
                     and target in (rescan.get("quarantined_paths") or []),
                     "the rescan must report the quarantined file as skipped: %r" % (rescan.get("report"),))
    problems.require(name not in device_names(session.result("plugin.list")),
                     "the quarantined plugin must be GONE from the device catalogue: %r" % (name,))

    session.result("plugin.scan_cache_quarantine_remove", {"path": target})
    session.result("plugin.rescan")
    problems.require(name in device_names(session.result("plugin.list")),
                     "un-quarantining and rescanning must bring the plugin back: %r" % (name,))


def check_transactions_and_rescan_undo(session, problems):
    """The A16 records, and the typed refusal an irreversible command produces."""
    records = [record for record in session.result("control.transactions").get("transactions") or []
               if record.get("command", "").startswith(("plugin.scan_cache", "plugin.rescan"))]
    classes = {(record.get("command"), record.get("class"), bool(record.get("mechanism")))
               for record in records}
    problems.require(("plugin.scan_cache_quarantine_add", "snapshot", True) in classes,
                     "the add must record a snapshot transaction with a mechanism: %r" % (classes,))
    problems.require(("plugin.rescan", "irreversible", True) in classes,
                     "the rescan must record an irreversible transaction: %r" % (classes,))

    refused = session.typed_error("control.undo")
    problems.require(refused.get("kind") == "irreversible"
                     and "fingerprint" in (refused.get("message") or ""),
                     "control.undo after a rescan must fail typed and name the fallback: %r" % (refused,))


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
    if problems.report("the plugin scan-cache socket proof has %d failed check(s)" % len(problems.items)):
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
    print("plugin dir: %s" % (directory or "(none: the cache is driven without plugin files)"))
    with H.start_instance(argv[1], extra_env=extra_env) as instance:
        H.wait_for_socket(instance)
        client = H.connect(instance)
        H.wait_ready(instance, client, transcript)
        session = Session(client, transcript)
        try:
            state = check_state(session, instance, problems)
            listing = check_list(session, state, problems)
            record = check_lookup(session, instance, listing, problems)
            target = check_add(session, instance, problems, record)
            check_refusals(session, problems, target)
            check_undo_removes(session, problems, target)
            check_reason_survives_remove_and_undo(session, problems, target)
            check_reason_negative_control(session, problems, target)
            check_rescan_hides_and_restores(session, problems, record)
            check_transactions_and_rescan_undo(session, problems)
            check_quit(session, instance, problems)
        finally:
            transcript.dump()
    code = report(problems)
    if code == 0:
        H.ok("plugin scan-cache socket proof (every check held)")
    return code


if __name__ == "__main__":
    sys.exit(main(sys.argv))
