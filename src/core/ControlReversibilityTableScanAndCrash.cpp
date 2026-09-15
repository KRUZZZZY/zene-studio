/*
 * ControlReversibilityTableScanAndCrash.cpp - the plugin scan-cache group's and
 * the crash-reporter group's rows of the SPEC A16 classification table.
 *
 * This file is data. It holds the two GROUPs' rows, whatever their class, and
 * the class comes from each row rather than from the file - the same arrangement
 * ControlReversibilityTableRouting.cpp uses for the routing surface, and for the
 * same reason: the passive block (ControlReversibilityTablePassive.cpp) is at
 * 477 of the 500 lines the file-length ratchet allows and the live block
 * (ControlReversibilityTable.cpp) is at 499, so a group's rows land together in
 * their own translation unit and reversibilityRowTable() joins them into the one
 * block every consumer reads.
 *
 * What each row argues:
 *   * plugin.scan_cache_get_state / scan_cache_list / scan_cache_lookup write
 *     nothing. The first of them may CONSTRUCT the plugin factory, which by the
 *     engine's own design runs the first scan at that moment (the property
 *     plugin.list has always had); the cache file that may write is derived
 *     state no project checkpoint describes.
 *   * plugin.scan_cache_quarantine_add / scan_cache_quarantine_remove are
 *     recorded-COMMAND snapshots: PluginScanCache is a JSON file outside the
 *     project and is not a JournallingObject, so there is no checkpoint to take
 *     and the inverse is the paired command (`applies: command`), dispatched by
 *     control.undo. This is browser.tag.add / browser.tag.remove's class and
 *     mechanism, for the same reason.
 *   * plugin.rescan is IRREVERSIBLE: a scan re-measures the files on disk and
 *     refreshes the cache, and no command restores a file's previous fingerprint
 *     record once a scan has replaced it. The fallback is the one the cache's own
 *     contract leaves - restore the library and scan again - and nothing in the
 *     project is touched either way.
 *   * crash.list_reports writes nothing; crash.upload_report is declared mutating
 *     and REFUSES every call (the automation.mode_set shape), because this build
 *     has no upload and no network code of any kind in the reporter.
 *   * crash.acknowledge_report / crash.discard_report are IRREVERSIBLE: the
 *     module writes an `offered` sentinel nothing removes, and deletes a report
 *     nothing can write back from a caller's bytes. Each names its fallback.
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

#include "ControlReversibility.h"

namespace lmms
{
namespace control
{

namespace
{

using RC = ReversibilityClass;

//! A literal row: R(id, class, reversible, reason, mechanism, fallback).
#define R(id, cls, rev, reason, mechanism, fallback) \
	{ id, cls, reason, mechanism, fallback, rev, nullptr }

const ReversibilityRow kScanAndCrashRows[] = {
	// =====================================================================
	// The plugin.* scan-cache group (feature row 46) - inspect what the scan
	// remembers, and operate the quarantine list, without editing its JSON.
	// =====================================================================
	R("plugin.scan_cache_get_state", RC::NotMutating, false,
		"reads the scan cache and its quarantine list: the file path, the "
		"persistence and dirty flags, the record and quarantine counts, the "
		"entries themselves and the last scan's own report",
		"no write. The one side effect in this group is the engine's, not the "
		"handler's, and is stated rather than hidden: asking for the plugin "
		"factory CONSTRUCTS it on the first ask and the constructor runs the "
		"first scan, which may write the DERIVED cache file - the property "
		"plugin.list has carried since it landed. No project state, no journal "
		"step",
		""),
	R("plugin.scan_cache_list", RC::NotMutating, false,
		"reads the records the cache remembers - each file's fingerprint, its "
		"status and, for a plugin, the descriptor metadata the scan resolved",
		"no write",
		""),
	R("plugin.scan_cache_lookup", RC::NotMutating, false,
		"reads one file's record and this group's own comparison of it "
		"(PluginScanCache::lookup needs path, size and mtime to match), plus "
		"whether the quarantine list hides that path",
		"no write",
		""),
	R("plugin.scan_cache_quarantine_add", RC::Snapshot, true,
		"the scan cache is a JSON file outside the project and is not a "
		"JournallingObject: no ProjectJournal checkpoint can hold the quarantine "
		"list, so there is nothing to addJournalCheckPoint() on and no live "
		"object whose state could be restored",
		"snapshot: the recorded inverse is the paired COMMAND "
		"plugin.scan_cache_quarantine_remove with this path (applies=command), "
		"dispatched by control.undo through the registry; the before-state "
		"records the path, its absence from the list and the list's count",
		""),
	R("plugin.scan_cache_quarantine_remove", RC::Snapshot, true,
		"the same file and the same absence of a checkpoint; the entry is one "
		"path plus ONE reason, which no other state reconstructs",
		"snapshot: the recorded inverse is the paired COMMAND "
		"plugin.scan_cache_quarantine_add with this path AND THE REASON CAPTURED "
		"BEFORE THE WRITE (applies=command). A path-only re-add would restore the "
		"entry with an empty reason, which looks restored and is not - "
		"tests/control-plugin-scan-commands.py drives that naive inverse and "
		"measures the loss, then drives the recorded one and measures the reason "
		"back",
		""),
	R("plugin.rescan", RC::Irreversible, false,
		"a scan re-MEASURES the files on disk (PluginFactory::discoverPlugins, "
		"which resets its own stats, re-reads the cache and re-loads what "
		"changed) and refreshes the cache; once a scan has replaced a file's "
		"fingerprint record, no command in this engine puts the previous one "
		"back",
		"none. The transaction still records the scan's own numbers and the "
		"cache's state as they stood before the run, so a caller can see exactly "
		"what the scan changed instead of being told nothing",
		"restore the library file and scan again: its fingerprint is recorded "
		"anew. Nothing in the project is touched, and the cache is derived state "
		"by its own contract - discovery never depends on this file, it only "
		"decides how much work a scan repeats (include/PluginScanCache.h)"),

	// =====================================================================
	// The crash.* group (feature row 54) - the local, offline crash reporter.
	// =====================================================================
	R("crash.list_reports", RC::NotMutating, false,
		"reads the reporter's state through its own predicates: whether it is "
		"installed, its report directory and the files it holds with their sizes "
		"and times, the pending/offered flags (hasPendingReport), the session "
		"marker the clean-exit path removes, its two hard bounds and the upload "
		"policy",
		"no write",
		""),
	R("crash.upload_report", RC::NotMutating, false,
		"declared mutating, but the handler REFUSES every call: this build has no "
		"upload and no network code of any kind in the crash reporter, which "
		"include/CrashReporter.h states as a design property of the module (no "
		"socket, no DNS, no telemetry, no network code in the translation unit)",
		"no write happens, so no transaction is recorded",
		"attach the report by hand: crash.list_reports names the file and its "
		"directory"),
	R("crash.acknowledge_report", RC::Irreversible, false,
		"acknowledging writes the reporter's `offered` sentinel so the report is "
		"not offered again; nothing in the module removes that sentinel - "
		"acknowledgePendingReport() only writes it, and discardPendingReport() "
		"deletes it TOGETHER WITH the report, which is not an inverse",
		"none. The transaction records the report's path, its size and mtime and "
		"the sentinel's state before the write, so the caller can see what it "
		"changed",
		"delete the offered sentinel in the report directory "
		"(<working dir>/crash-reports/zene-crash-report.offered, the name "
		"include/CrashReporter.h declares) and the report is pending again; this "
		"command never touches the report file itself"),
	R("crash.discard_report", RC::Irreversible, false,
		"the report file's bytes are gone, and nothing in this engine writes a "
		"report from a caller's bytes - writeReportIfIdle() assembles one from a "
		"CrashInfo the signal handler fills in, and the handler path is "
		"async-signal-safe by design, so it takes no input",
		"none. The transaction records both paths that were removed and the "
		"report's size, mtime and sentinel state, so the caller can see what it "
		"deleted",
		"re-run the action that crashed: the reporter writes a new bounded report "
		"at the same path. The discarded report's CONTENT is not recoverable by "
		"anything in this engine"),
	// ------------------------------------------------------------------
	// The reporter's ARM pair (board task #643): what the two verbs the group
	// above says cannot exist now do, and how each is taken back. Both are
	// `snapshot` with the paired COMMAND for the same reason the plugin
	// quarantine pair is: a signal disposition is PROCESS state, not project
	// state (include/CrashReporter.h), so no ProjectJournal checkpoint can hold
	// it and there is nothing to addJournalCheckPoint() on.
	// ------------------------------------------------------------------
	R("crash.enable", RC::Snapshot, true,
		"arming installs the crash handler's dispositions "
		"(crashreporter::install); the reporter is not a JournallingObject and "
		"what it changes is the process's signal dispositions, so the project's "
		"undo stack holds no state that describes it",
		"snapshot: the recorded inverse is the paired COMMAND crash.disable "
		"(applies=command), dispatched by control.undo through the registry. The "
		"before-state records whether the reporter was already armed and the "
		"report directory this call armed, and the call is REFUSED when it is "
		"already armed - so a recorded inverse is never an inverse of a call that "
		"changed nothing",
		""),
	R("crash.disable", RC::Snapshot, true,
		"disarming restores the default disposition for exactly the signals "
		"install() claimed; the set itself is not state any command of this "
		"engine reconstructs, and no checkpoint can hold a disposition",
		"snapshot: the recorded inverse is the paired COMMAND crash.enable "
		"(applies=command) with the report directory captured BEFORE the write. "
		"The directory rides in the inverse's args because the reporter keeps it "
		"while disarmed while a client's working directory can move under it - "
		"re-arming somewhere else would restore a different reporter than the one "
		"that was there",
		""),
};

constexpr int kScanAndCrashRowCount =
	static_cast<int>(sizeof(kScanAndCrashRows) / sizeof(kScanAndCrashRows[0]));

} // namespace

const ReversibilityRow* reversibilityScanAndCrashRowTable(int* rowCount)
{
	if (rowCount != nullptr) { *rowCount = kScanAndCrashRowCount; }
	return kScanAndCrashRows;
}

} // namespace control
} // namespace lmms
