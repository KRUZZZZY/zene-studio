/*
 * SafeStart.h - safe-start mode after a crash: the crash marker, and the
 *               load-time predicate that skips third-party plugin INSTANCES
 *               for the session that follows an unclean exit.
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of Zene Studio (an LMMS-derived product); it is derived
 * from LMMS - https://lmms.io
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
 *
 */

#ifndef ZENE_SAFE_START_H
#define ZENE_SAFE_START_H

#include <string>
#include <vector>

namespace lmms::safestart
{

// ---------------------------------------------------------------------------
// What this is
// ---------------------------------------------------------------------------
// A DAW whose previous run died often dies again on the next launch, because
// the thing that killed it is loaded during startup: a third-party plugin a
// saved project references. Safe-start mode breaks that loop. An abnormal exit
// leaves a MARKER behind; the next launch sees it and loads the project with
// THIRD-PARTY PLUGIN INSTANCES SKIPPED (the engine's own DummyPlugin stands in
// for each one, exactly as it already does for a plugin that is missing), so
// the session comes up; the operator is offered the normal start, and a session
// that exits cleanly clears the marker.
//
// The two halves of the feature and where they live:
//   * the MARKER + the decision state  - this module (src/core/SafeStart.cpp);
//   * the load-time PREDICATE          - shouldSkipPluginInstance() below,
//     consulted by Plugin::instantiate() (src/core/Plugin.cpp), which is the
//     single funnel every instrument, effect, tool, import filter and exporter
//     is created through. One call site, so "skipped" cannot mean one thing for
//     a track and another for an effect.
// Both are on the GUI thread only. Nothing here is called from an audio-thread
// path, and nothing here allocates or locks on one.
//
// ---------------------------------------------------------------------------
// The marker, and why it is written at session START
// ---------------------------------------------------------------------------
// "Write a crash marker on abnormal exit" cannot mean "write it inside a signal
// handler": SIGKILL is uncatchable, a power cut runs no code at all, and a
// process that dies inside dlopen() of a plugin may never reach a handler it
// installed. So the marker is written when the session BEGINS and unlinked on
// the clean-exit path (endSession()). A session that dies abnormally therefore
// leaves its marker behind, and "the marker is on disk at launch" is exactly
// "the previous run did not exit cleanly" - the construction the crash reporter
// already uses for its own session marker (include/CrashReporter.h), for the
// same reason.
//
// It is a SEPARATE file from the crash reporter's session marker on purpose: the
// crash reporter's marker is a diagnostic ("the last run was unclean", kept for
// the report and for bug reports), and this one is the INPUT to a decision that
// is consumed once. Folding them together would make safestart.clear unlink a
// file the crash module owns, and would make an acknowledged decision
// indistinguishable from a diagnostic record.
//
// ---------------------------------------------------------------------------
// The acknowledgement, and what "offer the normal start" means
// ---------------------------------------------------------------------------
// While the marker is present and UNACKNOWLEDGED, the session runs safe. The
// offer of a normal start is `safestart.get_state`'s `offer` block plus the one
// stderr line main() prints; accepting it (safestart.acknowledge) writes the
// acknowledgement file, which only ever means "the NEXT launch is a normal one".
// The next launch CONSUMES it - reads it, deletes it, and loads with third-party
// plugins enabled - so a second crash cannot be masked by a decision taken about
// the first. A clean exit clears both files; that is the whole lifecycle.
//
// ---------------------------------------------------------------------------
// What "third-party" means here (a definition, not a vibe)
// ---------------------------------------------------------------------------
// A plugin module is THIRD-PARTY when it is not a file THIS BUILD ships: not
// under the application's own plugin directories (the ones PluginFactory's
// search paths are built from - see ownPluginDirectories()), and not under a
// directory a packager pointed the build at (PLUGIN_DIR at compile time,
// LMMS_PLUGIN_DIR in the environment). A module dropped into the user's working
// directory (`<working dir>/plugins`, which is where a user installs an LMMS
// plugin) or loaded from anywhere else is third-party - and that is the set that
// safe-start skips, because those are the files whose version and origin this
// build does not control.
//
// Symlinks are resolved when they can be (QFileInfo::canonicalFilePath), so a
// link to one of our own modules counts as ours: the identity of a plugin file
// is the file it really is.
//
// ---------------------------------------------------------------------------
// Reversibility (SPEC A16)
// ---------------------------------------------------------------------------
// Nothing here is project state: it is files outside the project and one
// session-scoped switch. No ProjectJournal checkpoint describes any of it, so
// the A16 rows are the classes the crash reporter's rows are
// (src/core/ControlReversibilityTableSafeStart.cpp) and each writer names its
// fallback rather than pretending control.undo can put a deleted marker back.

// Marker / acknowledgement file names, relative to the working directory.
// Both are created lazily: a run that never crashes and never acknowledges
// leaves neither.
constexpr const char* kMarkerName = "zene-safe-start.marker";
constexpr const char* kAcknowledgedName = "zene-safe-start.acknowledged";

// Hard cap on the marker's bytes. The project path is truncated to
// kMaxProjectPathBytes when the marker is written, so the cap is reachable only
// in principle, never by long input. Proven by SafeStartTest.bounds_*.
constexpr unsigned long kMaxMarkerBytes = 1024;
constexpr unsigned long kMaxProjectPathBytes = 512;

//! The session a marker describes. Trivially copyable; the strings are bounded
//! when they are read as well as when they are written.
struct SessionRecord
{
	//! True when a marker was actually on disk (i.e. this record is real).
	bool present = false;
	unsigned long long processId = 0;
	unsigned long long unixTime = 0;
	//! How many consecutive sessions have started safe; 1 for the first.
	unsigned long long safeStartRuns = 0;
	//! The project that was open, when the crashed session had got that far.
	std::string projectPath;
};

//! One plugin instance this session did NOT create because of safe-start mode.
struct SkippedInstance
{
	std::string pluginName; //!< the name the project asked for
	std::string file;       //!< the module file it would have been loaded from
	std::string reason;     //!< why it was skipped, for the report
};

// ---------------------------------------------------------------------------
// lifecycle
// ---------------------------------------------------------------------------

//! Point the module at the working directory (the directory the crash reporter
//! and the plugin scan cache already use). The directory must already exist: the
//! module never creates it, so it cannot pre-empt the first-run prompt. Safe to
//! call again. Returns false when the directory does not exist, in which case
//! nothing is installed and every predicate below is false.
bool install(const std::string& workingDirectory);

//! True once install() has succeeded.
bool isInstalled();

//! The directory this module is pointed at ("" when it is not installed).
std::string workingDirectory();

//! MARKER PATH and ACKNOWLEDGEMENT PATH, for a client that wants to look at the
//! files itself. Both are absolute when the module is installed.
std::string markerPath();
std::string acknowledgedPath();

//! Start a session: READ the marker an earlier run left behind (if any), CONSUME
//! the acknowledgement if one is present, then write THIS session's marker.
//! Main thread, once per run, before any project is loaded - main() calls it
//! beside the crash reporter's own beginSession().
void beginSession();

//! End a session cleanly: unlink the marker and the acknowledgement. Main
//! thread, on every clean-exit path (the crash reporter's endSession() calls).
void endSession();

// ---------------------------------------------------------------------------
// state
// ---------------------------------------------------------------------------

//! Is a marker on disk right now?
bool markerExists();
//! Is the acknowledgement on disk right now?
bool acknowledged();
//! Did the session BEFORE this one exit cleanly? (False means a crash marker
//! was found at beginSession(), whatever the cause - a signal, SIGKILL, a power
//! cut.) Answerable before beginSession() too: it then means "is a marker on
//! disk", which is the same question asked before the marker is rewritten.
bool previousRunExitedCleanly();
//! Is THIS session a safe start? True only when the previous run left a marker
//! and no acknowledgement was found. This is the flag the predicate reads.
bool safeStartActive();
//! The record read at beginSession() (present=false when there was no marker).
SessionRecord lastSession();
//! How many sessions in a row have started safe (0 when this one did not).
unsigned long long safeStartRunCount();

//! The project this session has open, for the marker's own record: the hint the
//! next launch reads back and reports (the crash reporter keeps the same hint
//! for its report, include/CrashReporter.h's setProjectPath). Main thread only,
//! called where the crash reporter's own hint is set; the marker is rewritten
//! only when it is already there, so this can never create crash state.
void setProjectPath(const std::string& path);
//! The project hint THIS session's marker carries ("" when none is set).
std::string projectPath();

//! Accept the offer of a normal start: write the acknowledgement, so the NEXT
//! launch loads with third-party plugins enabled. Keeps the marker (the crash is
//! still there to look at) and does NOT un-skip instances already loaded.
void acknowledge();

//! Clear the marker now: unlink the marker and the acknowledgement, so the next
//! launch is a normal one, and leave safe-start mode in this session, so a
//! project loaded after this call loads its third-party plugins. The verb for
//! "the cause is known and fixed".
void clear();

// ---------------------------------------------------------------------------
// the load-time predicate
// ---------------------------------------------------------------------------

//! True when \a filePath is a plugin module this build does NOT ship (see the
//! header comment). An empty path is not third-party: a plugin that resolved to
//! no file at all is the missing-plugin case Plugin::instantiate() already
//! handles, and safe-start has nothing to add to it.
bool isThirdPartyPluginFile(const std::string& filePath);

//! THE PREDICATE. True when a plugin instance must NOT be created: safe-start is
//! active, the session-scoped switch is on (skipEnabled()) and the module is
//! third-party.
bool shouldSkipPluginInstance(const std::string& pluginName, const std::string& filePath);

//! Record one skipped instance (called by Plugin::instantiate() when the
//! predicate above said yes). The record is what makes "did safe-start skip
//! anything, and what?" answerable over the socket instead of a claim.
void noteSkippedInstance(const std::string& pluginName, const std::string& filePath,
	const std::string& reason);
//! Every instance this session skipped, in the order they were skipped.
const std::vector<SkippedInstance>& skippedInstances();
int skippedCount();
//! Forget the records (does not change the mode).
void resetSkippedInstances();

//! The session-scoped half of the predicate: false turns the skip off for the
//! rest of this session WITHOUT touching the marker, so a project loaded now
//! loads its third-party plugins (safestart.set_skip). Re-armed by
//! beginSession() for the next run.
bool skipEnabled();
void setSkipEnabled(bool enabled);

//! The plugin directories this build ships (see the header comment). Exposed so
//! the classification is observable rather than a claim about a black box.
std::vector<std::string> ownPluginDirectories();

} // namespace lmms::safestart

#endif // ZENE_SAFE_START_H
