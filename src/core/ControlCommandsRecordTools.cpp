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
#include <QMap>
#include <QStorageInfo>

#include "AudioEngine.h"
#include "ConfigManager.h"
#include "ControlCommandDef.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "MetronomeOptions.h"
#include "MultiTrackRecorder.h"
#include "ControlMixerSupport.h"
#include "Mixer.h"
#include "RecorderTaps.h"
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
	// 'edge' says which end of the pass this is, so a call without one is not an
	// "in" by default: it would silently arm a punch region at the play position.
	const QString edge = args.value(QStringLiteral("edge")).toString();
	if (edge != QLatin1String("in") && edge != QLatin1String("out"))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'edge' is 'in' or 'out'"));
	}
	const tick_t here = Engine::getSong()->getPlayPos().getTicks();
	const QJsonObject state = invoke(QStringLiteral("transport.punch_get_state"), {}).result;
	if (edge == QLatin1String("out"))
	{
		// Out: the region ends here; with no region armed there is nothing to close.
		if (!state.value(QStringLiteral("punch_armed")).toBool()) { return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("no punch region is armed")); }
		const qint64 begin = state.value(QStringLiteral("punch_begin")).toInteger();
		if (here <= begin) { return handlerOf(QStringLiteral("transport.punch_clear"), {}); }
		return handlerOf(QStringLiteral("transport.punch_set"), {{QStringLiteral("start"), static_cast<double>(begin)},
			{QStringLiteral("end"), static_cast<double>(here)}, {QStringLiteral("enabled"), true}});
	}
	// In: from here to the end of the song (punched out later, or at the song's end).
	const tick_t end = std::max<tick_t>(here + 1, std::min<tick_t>(MaxSongLength,
		Engine::getSong()->length() * TimePos::ticksPerBar() + TimePos::ticksPerBar()));  // review W3-batchA A4
	return handlerOf(QStringLiteral("transport.punch_set"), {{QStringLiteral("start"), static_cast<double>(here)},
		{QStringLiteral("end"), static_cast<double>(end)}, {QStringLiteral("enabled"), true}});
}

//! Where the armed routes write their takes, counted per directory: an armed route
//! writes to the file name it was armed with (record.arm_track 'file', which may sit
//! on another volume), and a route with no path yet writes beside the recovery file.
//! Routes sharing a directory share its free space.
QMap<QString, int> armedTakePlaces(const MultiTrackRecorder& recorder, const QString& recoveryDir)
{
	QMap<QString, int> places;
	for (int i = 0; i < recorder.trackCount(); ++i)
	{
		const TrackRecorder& route = recorder.track(i);
		if (!route.isArmed()) { continue; }
		const QString path = QString::fromStdString(route.filePath());
		places[path.isEmpty() ? recoveryDir : QFileInfo(path).absolutePath()] += 1;
	}
	return places;
}

ControlResult remaining(const QJsonObject&)
{
	const QString recoveryDir = QFileInfo(ConfigManager::inst()->recoveryFile()).absolutePath();
	const MultiTrackRecorder& recorder = Engine::audioEngine()->recorder();
	int armed = 0;
	for (int i = 0; i < recorder.trackCount(); ++i) { armed += recorder.track(i).isArmed() ? 1 : 0; }
	const double bytesPerRoute = static_cast<double>(Engine::audioEngine()->outputSampleRate()) * 3.0;  // 24-bit mono a route
	QMap<QString, int> places = armedTakePlaces(recorder, recoveryDir);
	if (places.isEmpty()) { places[recoveryDir] = 1; }
	// The pass runs on until the place with the least room left for what its routes
	// write runs out, so report that place - not the recovery file's volume.
	QString dir = recoveryDir;
	qint64 free = 0;
	double seconds = 0.0;
	bool seen = false;
	for (auto it = places.cbegin(); it != places.cend(); ++it)
	{
		const QStorageInfo storage(it.key());
		if (!storage.isValid()) { continue; }
		const double here = static_cast<double>(storage.bytesAvailable()) / (bytesPerRoute * std::max(1, it.value()));
		if (!seen || here < seconds)
		{
			seen = true;
			seconds = here;
			free = storage.bytesAvailable();
			dir = it.key();
		}
	}
	return ControlResult::success({{QStringLiteral("seconds"), seconds}, {QStringLiteral("armed_routes"), armed},
		{QStringLiteral("free_bytes"), static_cast<double>(free)}, {QStringLiteral("directory"), dir}});
}

const QStringList& sourceNames() { static const QStringList names{QStringLiteral("input"), QStringLiteral("master"), QStringLiteral("channel")}; return names; }
const QStringList& tapNames() { static const QStringList names{QStringLiteral("post_fader"), QStringLiteral("pre_fx"), QStringLiteral("pre_fader")}; return names; }

QJsonObject tapJson(int route, const recordertaps::Tap& tap)
{
	QJsonObject out{{QStringLiteral("route"), route}, {QStringLiteral("source"), sourceNames().value(static_cast<int>(tap.source))},
		{QStringLiteral("side"), tap.side == 0 ? QStringLiteral("left") : QStringLiteral("right")}};
	if (tap.source == recordertaps::Source::Channel)
	{
		MixerChannel* channel = nullptr;
		for (int i = 0; i < Engine::mixer()->numChannels() && channel == nullptr; ++i)
		{
			if (Engine::mixer()->mixerChannel(i)->id() == tap.channelId) { channel = Engine::mixer()->mixerChannel(i); }
		}
		out.insert(QStringLiteral("channel"), channel != nullptr ? channelIdOf(channel) : QString());
		out.insert(QStringLiteral("tap"), tapNames().value(tap.point));
	}
	return out;
}

ControlResult routeSource(const QJsonObject& args)
{
	const int route = args.value(QStringLiteral("route")).toInt(-1);
	if (route < 0 || route >= Engine::audioEngine()->recorder().trackCount())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'route' is a record route (record.get_state lists them)"));
	}
	const recordertaps::Tap before = recordertaps::get(route);
	if (!args.contains(QStringLiteral("source"))) { return ControlResult::success(tapJson(route, before)); }
	recordertaps::Tap tap;
	tap.source = static_cast<recordertaps::Source>(std::max(0, static_cast<int>(sourceNames().indexOf(args.value(QStringLiteral("source")).toString()))));
	tap.side = args.value(QStringLiteral("side")).toString() == QLatin1String("right") ? 1 : 0;
	tap.point = std::max(0, static_cast<int>(tapNames().indexOf(args.value(QStringLiteral("tap")).toString(QStringLiteral("post_fader")))));
	if (tap.source == recordertaps::Source::Channel)
	{
		ControlResult error;
		MixerChannel* channel = resolveMixerChannel(args.value(QStringLiteral("channel")).toString(), &error);
		if (channel == nullptr) { return error; }
		tap.channelId = channel->id();
	}
	recordertaps::set(route, tap);
	addUndoStep([route, before]() { recordertaps::set(route, before); }, [route, tap]() { recordertaps::set(route, tap); });
	return ControlResult::success(tapJson(route, tap));
}

void add(ControlRegistry& registry, const char* id, const QString& description, const QJsonObject& args, bool mutating,
	ControlResult (*handler)(const QJsonObject&))
{
	control::defineCommandWithSchema(registry, QLatin1String(id), description, objectSchema(args), mutating, handler);
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
	add(registry, "record.route_source", QStringLiteral("Where record route 'route' takes its audio from: 'source' input (the "
		"engine's input, its input channel - the default), master (the mix, resampling) or channel (mixer channel 'channel' at "
		"'tap' pre_fx, pre_fader or post_fader); 'side' left or right (a route records one channel). Interface state, not saved "
		"with the project. With only 'route' it reports it. One undo step."),
		{{QStringLiteral("route"), integerProperty(0, 1024)}, {QStringLiteral("source"), enumProperty(sourceNames())},
			{QStringLiteral("channel"), stringProperty()}, {QStringLiteral("tap"), enumProperty(tapNames())},
			{QStringLiteral("side"), enumProperty({QStringLiteral("left"), QStringLiteral("right")})}}, true, routeSource);
	add(registry, "record.remaining", QStringLiteral("How long recording can go on: the free space where takes are written, at the "
		"rate the armed routes write (24-bit, one channel a route; one route when none is armed), in seconds."), {}, false, remaining);
}

} // namespace lmms
