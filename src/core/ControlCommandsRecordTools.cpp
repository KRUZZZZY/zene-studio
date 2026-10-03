/*
 * ControlCommandsRecordTools.cpp - metronome options, punch at the playhead, remaining record time
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
 *
 */
/*! 0.5.0 L03 (W3), recording's working tools:
 *  - transport.metronome_options (WF-081): the click's level, accent, sound, and clicking only while
 *    recording (MetronomeOptions.h);
 *  - transport.punch_here (WF-079): punch in or out at the play position, one action while a pass
 *    runs - through transport.punch_set / punch_clear, so the region and its undo are theirs;
 *  - record.remaining (WF-085): how long the free disk space lasts at the rate the armed routes write. */

#include <QFileInfo>
#include <QJsonObject>
#include <QStorageInfo>

#include "AudioEngine.h"
#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "MetronomeOptions.h"
#include "MultiTrackRecorder.h"
#include "Song.h"
#include "TrackRecorder.h"

namespace lmms
{

using namespace control;

namespace
{

ControlResult invoke(const QString& id, const QJsonObject& args) { return ControlRegistry::instance()->invoke(id, args); }

//! Run command @a id's own handler inside this command - its undo step is this command's one step, and
//! its transaction is what this command reports (a nested invoke() would record a second one).
ControlResult handlerOf(const QString& id, const QJsonObject& args)
{
	const ControlCommand* command = ControlRegistry::instance()->command(id);
	return command != nullptr ? command->handler(args) : ControlResult::failure(ControlErrorKind::NotFound, id);
}

ControlResult metronomeOptions(const QJsonObject& args)
{
	const metronomeoptions::Options before = metronomeoptions::options();
	metronomeoptions::Options next = before;
	next.level = static_cast<float>(args.value(QStringLiteral("level")).toDouble(before.level));
	next.accent = args.value(QStringLiteral("accent")).toBool(before.accent);
	next.recordOnly = args.value(QStringLiteral("record_only")).toBool(before.recordOnly);
	if (args.contains(QStringLiteral("sound"))) { next.sound = static_cast<int>(metronomeoptions::soundNames().indexOf(args.value(QStringLiteral("sound")).toString())); }
	if (next.level < 0.f || next.level > 2.f) { return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'level' is 0..2")); }
	const bool changed = next.level != before.level || next.accent != before.accent || next.recordOnly != before.recordOnly || next.sound != before.sound;
	if (changed)
	{
		metronomeoptions::setOptions(next);
		addUndoStep([before]() { metronomeoptions::setOptions(before); }, [next]() { metronomeoptions::setOptions(next); });
	}
	return ControlResult::success(metronomeoptions::toJson(metronomeoptions::options()));
}

ControlResult punchHere(const QJsonObject& args)
{
	const tick_t here = Engine::getSong()->getPlayPos().getTicks();
	const QJsonObject state = invoke(QStringLiteral("transport.punch_get_state"), {}).result;
	if (args.value(QStringLiteral("edge")).toString() == QLatin1String("out"))
	{
		// Out: the region ends here; with no region armed there is nothing to close.
		if (!state.value(QStringLiteral("punch_armed")).toBool()) { return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("no punch region is armed")); }
		const qint64 begin = state.value(QStringLiteral("punch_begin")).toInteger();
		if (here <= begin) { return handlerOf(QStringLiteral("transport.punch_clear"), {}); }
		return handlerOf(QStringLiteral("transport.punch_set"), {{QStringLiteral("start"), static_cast<double>(begin)},
			{QStringLiteral("end"), static_cast<double>(here)}, {QStringLiteral("enabled"), true}});
	}
	// In: from here to the end of the song (punched out later, or at the song's end).
	const tick_t end = std::max<tick_t>(here + 1, Engine::getSong()->length() * TimePos::ticksPerBar() + TimePos::ticksPerBar());
	return handlerOf(QStringLiteral("transport.punch_set"), {{QStringLiteral("start"), static_cast<double>(here)},
		{QStringLiteral("end"), static_cast<double>(end)}, {QStringLiteral("enabled"), true}});
}

ControlResult remaining(const QJsonObject&)
{
	const QString dir = QFileInfo(ConfigManager::inst()->recoveryFile()).absolutePath();
	QStorageInfo storage(dir);
	const MultiTrackRecorder& recorder = Engine::audioEngine()->recorder();
	int armed = 0;
	for (int i = 0; i < recorder.trackCount(); ++i) { armed += recorder.track(i).isArmed() ? 1 : 0; }
	const double bytesPerSecond = static_cast<double>(Engine::audioEngine()->outputSampleRate()) * 3.0 * std::max(1, armed);  // 24-bit mono a route
	const double seconds = storage.isValid() ? static_cast<double>(storage.bytesAvailable()) / bytesPerSecond : 0.0;
	return ControlResult::success({{QStringLiteral("seconds"), seconds}, {QStringLiteral("armed_routes"), armed},
		{QStringLiteral("free_bytes"), static_cast<double>(storage.bytesAvailable())}, {QStringLiteral("directory"), dir}});
}

void add(ControlRegistry& registry, const char* id, const QString& description, const QJsonObject& args, bool mutating,
	ControlResult (*handler)(const QJsonObject&))
{
	ControlCommand cmd;
	cmd.id = QLatin1String(id);
	const int dot = cmd.id.indexOf(QLatin1Char('.'));
	cmd.group = cmd.id.left(dot);
	cmd.verb = cmd.id.mid(dot + 1);
	cmd.description = description;
	cmd.argsSchema = objectSchema(args);
	cmd.resultSchema = objectProperty();
	cmd.mutating = mutating;
	cmd.handler = handler;
	registry.registerCommand(cmd);
}

} // namespace

void registerRecordToolCommands(ControlRegistry& registry)
{
	metronomeoptions::options();  // read the stored options on this thread, before any click
	add(registry, "transport.metronome_options", QStringLiteral("How the metronome clicks: 'level' (0..2), 'accent' (the bar's first "
		"beat accented), 'sound' (classic, rim, clave or stick) and 'record_only' (click only while recording; a count-in "
		"clicks either way). transport.set_metronome switches it on. Preferences, kept in the config file. With no arguments "
		"it reports them. One undo step."),
		{{QStringLiteral("level"), numberProperty()}, {QStringLiteral("accent"), booleanProperty()},
			{QStringLiteral("sound"), enumProperty(metronomeoptions::soundNames())}, {QStringLiteral("record_only"), booleanProperty()}},
		true, metronomeOptions);
	add(registry, "transport.punch_here", QStringLiteral("Punch at the play position, one action while a pass runs: 'edge' in starts "
		"the punch region here (to the song's end), out ends it here. Through transport.punch_set, so it is that command's undo "
		"step."), {{QStringLiteral("edge"), enumProperty({QStringLiteral("in"), QStringLiteral("out")})}}, true, punchHere);
	add(registry, "record.remaining", QStringLiteral("How long recording can go on: the free space where takes are written, at the "
		"rate the armed routes write (24-bit, one channel a route; one route when none is armed), in seconds."), {}, false, remaining);
}

} // namespace lmms
