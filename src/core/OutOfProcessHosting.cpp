/*
 * OutOfProcessHosting.cpp - the family table and the client-process record
 *                           (feature row 80, board card #670)
 *
 * The header (include/OutOfProcessHosting.h) carries the argument; this file is
 * the data and the two mechanisms. Every claim below about what a family IS in
 * this tree is a claim about a named file, so it can be re-checked:
 *
 *   ZynAddSubFx   plugins/ZynAddSubFx/CMakeLists.txt:168 builds RemoteZynAddSubFx,
 *                 and the module ships BOTH implementations (LocalZynAddSubFx is
 *                 the default, RemoteZynAddSubFx is the client) - the one family
 *                 in this tree where the choice is real.
 *   VST2          plugins/VstBase/VstPlugin.cpp:174-181 picks
 *                 REMOTE_VST_PLUGIN_FILEPATH_64 / _32 /
 *                 NATIVE_LINUX_REMOTE_VST_PLUGIN_FILEPATH_64 and calls
 *                 init(remoteVstPluginExecutable, ...) from tryLoad(); there is
 *                 no in-process VST2 host in this tree, so the client process is
 *                 what VST2 IS and there is nothing to toggle
 *                 (docs/OOP-HOSTING.md section 1.3).
 *   CLAP/VST3     the host module is loaded into THIS process
 *                 (plugins/ClapEffect/ClapHost.cpp, plugins/Vst3Effect/Vst3Host.cpp)
 *                 and no client executable exists for either - the honest next
 *                 step, not an impossibility.
 *   native and the remaining hosted formats (LADSPA, LV2, SF2, GIG) have no
 *                 client executable either; each family's `reason` says so in
 *                 its own words rather than sharing one sentence.
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

#include "OutOfProcessHosting.h"

#include <QMutexLocker>

namespace lmms::oop
{

namespace
{

//! How many client deaths in one session make the out-of-process path refused.
//! One death is a fact (a plugin can die once), two are a pattern, three are a
//! loop; the constant is here rather than inline so the number in the docs, in
//! the refusals and in the test all come from one place.
constexpr int kMaxCrashesPerClient = 3;

//! The client executable VST2 always runs in (the platform's own name is chosen
//! by VstPlugin::tryLoad(); this is the family's token, which is what the
//! refusal and the record are keyed by).
constexpr auto Vst2ClientToken = "RemoteVstPlugin";

/*! The family table.
 *
 *  Order is the key order families() reports and it is stable across runs: the
 *  two families that have a client process first, then the hosted formats, then
 *  the in-tree modules. A family whose key is absent from this table is still
 *  answered - see familyFor() - so a new module does not have to be added here
 *  to be refused honestly.
 */
QVector<Family> buildFamilies()
{
	return {
		{QStringLiteral("zynaddsubfx"), QStringLiteral("ZynAddSubFx"),
			Availability::ClientAvailable, QStringLiteral("RemoteZynAddSubFx"),
			QStringLiteral("this build ships BOTH implementations for this family - the in-process synth "
				"(LocalZynAddSubFx, the default) and the client executable RemoteZynAddSubFx - so running "
				"an instance in a separate process is a per-instance choice (the `separateprocess` "
				"attribute, driven by oop.set_mode)")},
		{QStringLiteral("vestige"), QStringLiteral("VeSTige (VST2 instrument)"),
			Availability::AlwaysSeparate, QString::fromLatin1(Vst2ClientToken),
			QStringLiteral("VST2 has no in-process path in this tree: VstPlugin::tryLoad() starts the "
				"remote VST client unconditionally (plugins/VstBase/VstPlugin.cpp:174-181), so every "
				"VST2 instance is ALREADY out of process and there is no in-process mode to fall back to")},
		{QStringLiteral("vsteffect"), QStringLiteral("VST2 effect"),
			Availability::AlwaysSeparate, QString::fromLatin1(Vst2ClientToken),
			QStringLiteral("same as the VST2 instrument half: the client process is the only VST2 path "
				"this build has (plugins/VstEffect/VstEffect.cpp, plugins/VstBase/VstPlugin.cpp)")},
		{QStringLiteral("clapeffect"), QStringLiteral("CLAP effect"),
			Availability::NoClientInBuild, QString(),
			QStringLiteral("the CLAP host module runs inside THIS process (plugins/ClapEffect/ClapHost.cpp) "
				"and this build has no CLAP client executable; hosting it out of process needs the host to "
				"move behind a client boundary first, and nothing else does")},
		{QStringLiteral("clapinstrument"), QStringLiteral("CLAP instrument"),
			Availability::NoClientInBuild, QString(),
			QStringLiteral("as the CLAP effect half: the instrument host is loaded into this process and "
				"the build ships no client executable for it")},
		{QStringLiteral("vst3effect"), QStringLiteral("VST3 effect"),
			Availability::NoClientInBuild, QString(),
			QStringLiteral("the VST3 host module runs inside THIS process (plugins/Vst3Effect/Vst3Host.cpp) "
				"and this build has no VST3 client executable")},
		{QStringLiteral("vst3instrument"), QStringLiteral("VST3 instrument"),
			Availability::NoClientInBuild, QString(),
			QStringLiteral("as the VST3 effect half: no client executable is part of this build, so the "
				"instrument is hosted in this process")},
		{QStringLiteral("ladspaeffect"), QStringLiteral("LADSPA effect"),
			Availability::NoClientInBuild, QString(),
			QStringLiteral("the LADSPA plugin's own library is dlopen(3)ed into THIS process; a client "
				"would have to implement the LADSPA host side itself and the build has none")},
		{QStringLiteral("lv2effect"), QStringLiteral("LV2 effect"),
			Availability::NoClientInBuild, QString(),
			QStringLiteral("same shape as LADSPA: the LV2 world lives in this process and there is no LV2 "
				"client executable in this build")},
		{QStringLiteral("lv2instrument"), QStringLiteral("LV2 instrument"),
			Availability::NoClientInBuild, QString(),
			QStringLiteral("as the LV2 effect half: no client executable in this build")},
		{QStringLiteral("sf2player"), QStringLiteral("SF2 Player"),
			Availability::NoClientInBuild, QString(),
			QStringLiteral("an in-tree module: its code is in this process, and an out-of-process host "
				"would need a client executable that this build does not ship")},
		{QStringLiteral("gigplayer"), QStringLiteral("GIG Player"),
			Availability::NoClientInBuild, QString(),
			QStringLiteral("an in-tree module, as the SF2 player: no client executable in this build")},
		{QStringLiteral("carlarack"), QStringLiteral("Carla Rack"),
			Availability::NoClientInBuild, QString(),
			QStringLiteral("an in-tree module: no client executable in this build")},
		{QStringLiteral("carlapatchbay"), QStringLiteral("Carla Patchbay"),
			Availability::NoClientInBuild, QString(),
			QStringLiteral("an in-tree module: no client executable in this build")},
		{QStringLiteral("carlabase"), QStringLiteral("Carla base"),
			Availability::NoClientInBuild, QString(),
			QStringLiteral("an in-tree module: no client executable in this build")},
	};
}

} // namespace

QString availabilityName(Availability availability)
{
	switch (availability)
	{
	case Availability::ClientAvailable: return QStringLiteral("client-available");
	case Availability::AlwaysSeparate: return QStringLiteral("always-separate");
	case Availability::NoClientInBuild: return QStringLiteral("no-client");
	}
	return QStringLiteral("no-client");
}

QString stateName(State state)
{
	switch (state)
	{
	case State::InProcess: return QStringLiteral("in-process");
	case State::SeparateProcess: return QStringLiteral("separate-process");
	case State::ClientExited: return QStringLiteral("client-exited");
	case State::RefusedNoClient: return QStringLiteral("refused-no-client");
	case State::RefusedCrashLoop: return QStringLiteral("refused-crash-loop");
	}
	return QStringLiteral("in-process");
}

QVector<Family> families()
{
	// Built once: the table is static data and every caller reads the same one.
	static const QVector<Family> table = buildFamilies();
	return table;
}

Family familyFor(const QString& pluginKey)
{
	for (const Family& family : families())
	{
		if (family.key == pluginKey) { return family; }
	}

	// An unknown key is answered, not ignored: the fallback NAMES the key, so a
	// refusal quoting this family reads "no client executable for '<key>' in
	// this build" rather than a sentence about a family the caller never named.
	Family unknown;
	unknown.key = pluginKey;
	unknown.label = pluginKey;
	unknown.availability = Availability::NoClientInBuild;
	unknown.reason = QStringLiteral("'%1' is not in this build's out-of-process family table and ships no "
		"client executable, so an instance of it runs in this process").arg(pluginKey);
	return unknown;
}

bool canHostOutOfProcess(const QString& pluginKey, QString* reason)
{
	const Family family = familyFor(pluginKey);
	if (family.availability != Availability::NoClientInBuild) { return true; }
	if (reason != nullptr) { *reason = family.reason; }
	return false;
}

HostTracker& HostTracker::instance()
{
	static HostTracker tracker;
	return tracker;
}

int HostTracker::maxCrashesPerClient() { return kMaxCrashesPerClient; }

ClientRecord* HostTracker::find(const QString& client)
{
	const auto it = m_records.find(client);
	return it == m_records.end() ? nullptr : &it.value();
}

void HostTracker::noteStarted(const QString& client, qint64 pid, const QString& slot)
{
	if (client.isEmpty()) { return; }
	const QMutexLocker lock(&m_mutex);
	ClientRecord& record = m_records[client];
	record.client = client;
	++record.starts;
	record.lastPid = pid;
	record.lastState = slot.isEmpty()
		? stateName(State::SeparateProcess)
		: QStringLiteral("%1 (%2)").arg(stateName(State::SeparateProcess), slot);
}

void HostTracker::noteExited(const QString& client, int exitCode, bool crashed, const QString& slot)
{
	if (client.isEmpty()) { return; }
	const QMutexLocker lock(&m_mutex);
	ClientRecord& record = m_records[client];
	record.client = client;
	++record.exits;
	// A client that exits non-zero is as broken as one that is killed by a
	// signal: both are a plugin that did not do its job. The two counts stay
	// separate so a report can tell them apart.
	if (crashed || exitCode != 0) { ++record.crashes; }
	record.lastExitCode = exitCode;
	record.lastExitWasCrash = crashed;
	record.lastPid = 0;
	record.lastState = slot.isEmpty()
		? stateName(State::ClientExited)
		: QStringLiteral("%1 (%2)").arg(stateName(State::ClientExited), slot);
}

void HostTracker::noteShutdown(const QString& client, int exitCode)
{
	if (client.isEmpty()) { return; }
	const QMutexLocker lock(&m_mutex);
	ClientRecord& record = m_records[client];
	record.client = client;
	++record.exits;
	record.lastExitCode = exitCode;
	record.lastExitWasCrash = false;
	record.lastPid = 0;
	record.lastState = stateName(State::InProcess);
}

void HostTracker::noteRestart(const QString& client)
{
	if (client.isEmpty()) { return; }
	const QMutexLocker lock(&m_mutex);
	ClientRecord& record = m_records[client];
	record.client = client;
	++record.restarts;
}

bool HostTracker::refusedByCrashLoop(const QString& client, QString* reason) const
{
	const QMutexLocker lock(&m_mutex);
	const auto it = m_records.constFind(client);
	const int crashes = it == m_records.constEnd() ? 0 : it->crashes;
	if (crashes < kMaxCrashesPerClient) { return false; }
	if (reason != nullptr)
	{
		*reason = QStringLiteral("the client executable '%1' has died %2 times in this session (the last "
			"exit: code %3%4), which is this build's crash-loop bound (%5); the out-of-process path for it "
			"is REFUSED until oop.reset_crashes clears the count. The in-process path is unaffected")
			.arg(client)
			.arg(crashes)
			.arg(it->lastExitCode)
			.arg(it->lastExitWasCrash ? QStringLiteral(", a crash") : QString())
			.arg(kMaxCrashesPerClient);
	}
	return true;
}

ClientRecord HostTracker::record(const QString& client) const
{
	const QMutexLocker lock(&m_mutex);
	const auto it = m_records.constFind(client);
	return it == m_records.constEnd() ? ClientRecord{} : *it;
}

QVector<ClientRecord> HostTracker::records() const
{
	const QMutexLocker lock(&m_mutex);
	QVector<ClientRecord> out;
	out.reserve(m_records.size());
	for (auto it = m_records.constBegin(); it != m_records.constEnd(); ++it) { out.append(it.value()); }
	return out;
}

void HostTracker::resetCrashes(const QString& client)
{
	const QMutexLocker lock(&m_mutex);
	const auto it = m_records.find(client);
	if (it == m_records.end()) { return; }
	it->crashes = 0;
	it->lastExitWasCrash = false;
}

void HostTracker::resetAll()
{
	const QMutexLocker lock(&m_mutex);
	for (auto it = m_records.begin(); it != m_records.end(); ++it)
	{
		it->crashes = 0;
		it->lastExitWasCrash = false;
	}
}

} // namespace lmms::oop
