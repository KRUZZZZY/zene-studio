/*
 * OutOfProcessHosting.h - out-of-process plugin hosting / crash isolation
 *                         BEYOND ZynAddSubFx: which plugin families this build
 *                         can host in a client process, and the lifecycle and
 *                         crash accounting every hosted slot is held to.
 *
 * Feature row 80 of docs/FEATURE-LIST-0.3.0.md (board card #670). The prior art
 * (`post-alpha/oop-hosting`, docs/OOP-HOSTING.md) landed the machinery for ONE
 * family: ZynAddSubFx's `separateprocess` attribute, a BoolModel on the
 * instrument and a branch in its initPlugin(). Everything about it was
 * Zyn-specific - the decision lived in the plugin's own code and nothing in the
 * tree could answer "which OTHER families could be hosted this way, and why
 * not?", which is the question the row is about.
 *
 * This module is that answer, in the shape the rest of this tree uses: a table
 * of the plugin families a build knows, what each one's out-of-process story
 * actually is IN THIS BUILD, and a process-wide record of what the client
 * processes have done. It is deliberately family-agnostic: nothing here names a
 * plugin class, so a family that grows a client executable becomes hostable by
 * being classified here rather than by editing this code.
 *
 * WHAT THE TABLE IS AND IS NOT. `Availability::ClientAvailable` means a client
 * executable is part of this build and the family has BOTH an in-process and a
 * client implementation - so hosting it out of process is a per-instance
 * choice. `AlwaysSeparate` means the family's only path IS a client process
 * (VST2 in this tree: plugins/VstBase/VstPlugin.cpp:174-181 starts
 * RemoteVstPlugin unconditionally, so there is no choice to offer and no
 * in-process path to fall back to). `NoClientInBuild` means exactly what it
 * says: no client executable for that family is part of this build. It is NOT a
 * claim that the family could never be hosted out of process - for the formats
 * that live behind a host module (CLAP, VST3), it is the honest statement that
 * the host module is loaded into THIS process and no second process exists
 * (yet); the exact next step is named in the family's own `reason`.
 *
 * CRASH ISOLATION, precisely. A client process that dies is already survivable
 * in this tree: RemotePlugin::processFinished() invalidates the plugin
 * (src/core/RemotePlugin.cpp:504-518) and RemotePlugin::process() zero-fills
 * the output planes instead of reading a dead peer, so the audio for that slot
 * stops and the DAW keeps running. What did NOT exist is the *accounting*: the
 * tree logged `Remote plugin crashed` and forgot it, so nothing could tell a
 * slot that died once from one that dies every time, and nothing could refuse
 * to keep re-hosting a client executable that is broken. HostTracker is that
 * accounting, and `refusedByCrashLoop()` is the discipline: after
 * `maxCrashesPerClient()` client deaths in ONE session, the out-of-process path
 * for that client executable is REFUSED - typed, with the count and the last
 * exit code - until `oop.reset_crashes` clears it. The count is keyed by the
 * client executable, not by the plugin instance, because re-instantiating the
 * plugin is exactly what a crash loop does and an instance-keyed count could be
 * dodged by the reload it triggers.
 *
 * REALTIME. Nothing on the audio thread reads or writes any of this: the
 * notifications come from QProcess signals (the application thread) and the
 * reads come from control handlers (the same thread). HostTracker serialises
 * with one QMutex and takes no lock in any audio path. The audio-thread
 * behaviour on a dead client is the pre-existing `failed()` zero-fill, which
 * this change does not touch.
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

#ifndef LMMS_OUT_OF_PROCESS_HOSTING_H
#define LMMS_OUT_OF_PROCESS_HOSTING_H

#include <QHash>
#include <QMutex>
#include <QString>
#include <QVector>

#include "lmms_export.h"

namespace lmms::oop
{

//! What this build can do with one plugin family.
enum class Availability
{
	ClientAvailable, //!< a client executable is part of this build AND the family has an in-process path: per-instance choice
	AlwaysSeparate,  //!< the family's only path IS a client process; there is no in-process alternative to offer
	NoClientInBuild  //!< no client executable for this family is part of this build (the reason says what is missing)
};

//! The wire name of an availability ("client-available", "always-separate", "no-client")
LMMS_EXPORT QString availabilityName(Availability availability);

//! One plugin family, as this build can host it.
struct Family
{
	QString key;  //!< the Plugins' own key: Plugin::Descriptor::name, e.g. "zynaddsubfx"
	QString label; //!< display name of the family
	Availability availability = Availability::NoClientInBuild;
	QString client; //!< client executable name, empty when the family has none
	QString reason; //!< one sentence: what this build has, or the exact thing it does not have
};

//! Every family this build classifies, in a stable order.
LMMS_EXPORT QVector<Family> families();

//! The family `pluginKey` (a Plugin::Descriptor::name) belongs to. An unknown
//! key answers a `NoClientInBuild` family that NAMES the key, so a caller
//! reporting a refusal always names what it refused.
LMMS_EXPORT Family familyFor(const QString& pluginKey);

//! Whether `pluginKey` can be hosted out of process in this build; false fills
//! `reason` with the family's own sentence. A family that is already out of
//! process unconditionally (AlwaysSeparate) answers TRUE - it has a client - and
//! the caller decides whether "the user asked for the mode it already has" is a
//! no-op or a refusal.
LMMS_EXPORT bool canHostOutOfProcess(const QString& pluginKey, QString* reason);

//! The state one device slot's hosting is in, as the control surface reports it.
enum class State
{
	InProcess,         //!< the plugin runs in this process (no client for it)
	SeparateProcess,   //!< a client process is running for it
	ClientExited,      //!< a client ran and has gone away (counted; not yet refused)
	RefusedNoClient,   //!< the family has no client executable in this build
	RefusedCrashLoop   //!< this client executable has died maxCrashesPerClient() times this session
};

//! The wire name of a state ("in-process", "separate-process", "client-exited",
//! "refused-no-client", "refused-crash-loop")
LMMS_EXPORT QString stateName(State state);

//! What one client executable has done in this session. `client` is the key.
struct ClientRecord
{
	QString client;                //!< the client executable's name, e.g. "RemoteZynAddSubFx"
	int starts = 0;                //!< times a client was started
	int exits = 0;                 //!< times one went away (any exit status)
	int crashes = 0;               //!< exits that were a crash (QProcess::CrashExit) or a non-zero exit code
	int restarts = 0;              //!< times oop.restart asked the slot to re-host itself
	qint64 lastPid = 0;            //!< pid of the last client started, 0 once it has gone
	int lastExitCode = 0;          //!< the last exit code seen (0 for a signal death)
	bool lastExitWasCrash = false; //!< whether that last exit was QProcess::CrashExit
	QString lastState;             //!< the state name at the last notification
};

/*! The process-wide record of what the client processes have done.
 *
 *  Keys are CLIENT EXECUTABLE NAMES, not plugin instances, and that is
 *  deliberate: a client that is broken enough to crash is re-instantiated by
 *  the very reload its crash triggers, so an instance-keyed count could be
 *  reset by the failure it is supposed to count. A client executable is one
 *  identity for the whole process, so the count can only be cleared on purpose
 *  (`resetCrashes` / `resetAll`).
 *
 *  The class is a singleton with one QMutex and no audio-thread callers. Every
 *  notification is called from the application thread - RemotePlugin's
 *  `processFinished`/`processErrored` and the control handlers - so the cost of
 *  a lock here is a control-path cost, never an audio-path one.
 */
class LMMS_EXPORT HostTracker
{
public:
	static HostTracker& instance();

	//! A client process was started for `client` (RemotePlugin::init() on the
	//! success path). `slot` names the device or plugin the client belongs to,
	//! for the record's own reporting only.
	void noteStarted(const QString& client, qint64 pid, const QString& slot);
	//! A client process went away. `crashed` is QProcess::CrashExit; a non-zero
	//! `exitCode` counts as a crash as well (a client that bails out is as
	//! broken as one that is killed).
	void noteExited(const QString& client, int exitCode, bool crashed, const QString& slot);
	//! A client process went away because the host took it down on purpose
	//! (RemotePlugin's own destructor: IdQuit, then terminate/kill). Counted as
	//! an exit, NEVER as a crash - a mode switch that stops a healthy client is
	//! not evidence that the client is broken.
	void noteShutdown(const QString& client, int exitCode);
	//! oop.restart asked the slot to re-host itself.
	void noteRestart(const QString& client);
	//! Whether the out-of-process path for `client` must be refused, and why.
	bool refusedByCrashLoop(const QString& client, QString* reason) const;
	//! The record for one client executable (a default-constructed one when it has none).
	ClientRecord record(const QString& client) const;
	//! Every record, in the order Qt's hash iterates (stable within a process).
	QVector<ClientRecord> records() const;
	//! Forgets the crash history of one client executable. This is the inverse of
	//! nothing - it clears a refusal, it does not undo a crash.
	void resetCrashes(const QString& client);
	//! Forgets every client's crash history.
	void resetAll();

	//! How many client deaths in one session make the path refused. 3: one death
	//! is a fact, two are a pattern, three are a loop.
	static int maxCrashesPerClient();

private:
	HostTracker() = default;
	ClientRecord* find(const QString& client);

	mutable QMutex m_mutex;
	QHash<QString, ClientRecord> m_records;
};

/*! The invokable-method convention a hosting-capable plugin implements.
 *
 *  A plugin (an Instrument or an Effect - both are QObjects through Plugin) is
 *  drivable out of process when it exposes, as Q_INVOKABLE members:
 *
 *    QString hostingState() const;              //!< "in-process" | "separate-process" | "separate-process-exited"
 *    bool    setHostingMode(bool separate);     //!< choose the mode, re-hosting if it changed
 *    void    reloadPlugin();                    //!< re-create what is hosted (a fresh client)
 *
 *  ZynAddSubFx implemented the first and third before this change (its
 *  `hostingState` is read by tests, its `reloadPlugin` private slot is what its
 *  view and its sample-rate signal call); `setHostingMode` is what this change
 *  adds to it. A family WITHOUT these is not silently "not hostable" - the
 *  control surface refuses the request and names the missing half, which is the
 *  honesty this row requires. The names are looked up through the meta-object,
 *  so no header for a plugin class is needed on either side.
 */
inline constexpr auto HostingStateInvokable = "hostingState";
inline constexpr auto SetHostingModeInvokable = "setHostingMode";
inline constexpr auto ReloadInvokable = "reloadPlugin";

} // namespace lmms::oop

#endif // LMMS_OUT_OF_PROCESS_HOSTING_H
