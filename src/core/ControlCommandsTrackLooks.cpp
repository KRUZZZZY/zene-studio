/*
 * ControlCommandsTrackLooks.cpp - the track manager's commands: colour, visibility, icon, overview, load
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

/*! 0.5.0 W3 L13, the track manager (WF-097) and what it edits:
 *  - track.overview: every track in one list - name, type, colour, icon, mute / solo / shown, folder,
 *    mixer channel, devices and that channel's processing load;
 *  - track.set_color / track.set_visible: a track's colour and whether the arranger shows it;
 *  - track.set_icon (WF-120): the icon its header shows (TrackLooks.h);
 *  - mixer.cpu_load (WF-104): every mixer channel's share of the period, and each device's, measured on the
 *    audio thread with relaxed atomics (DspLoadProbe.h). An instrument's own synthesis runs before the mixer and is
 *    not in a channel's figure. Each edit is one undo step whose closures name the track by id. */

#include <optional>

#include <QColor>
#include <QJsonArray>

#include "AudioEngine.h"
#include "ControlCommandDef.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Effect.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "Mixer.h"
#include "Song.h"
#include "Track.h"
#include "TrackLooks.h"

namespace lmms
{

using namespace control;

namespace
{

constexpr int MaxIconLength = 40;

Track* trackWithId(int id)
{
	return Engine::getSong()->findTrackById(id);
}

void paintTrack(int id, const std::optional<QColor>& color)
{
	if (Track* track = trackWithId(id)) { track->setColor(color); }
}

void showTrack(int id, bool visible)
{
	if (Track* track = trackWithId(id))
	{
		track->setVisible(visible);
		emit Engine::getSong()->visibilityChanged();
	}
}

//! The mixer channel @a track plays into, or nullptr (a pattern, automation or folder track).
MixerChannel* channelOf(Track* track)
{
	IntModel* model = track->mixerChannelModel();
	const int index = model != nullptr ? model->value() : -1;
	return index >= 0 && index < Engine::mixer()->numChannels() ? Engine::mixer()->mixerChannel(index) : nullptr;
}

QJsonObject overviewEntry(Track* track, int index)
{
	QJsonObject entry = trackEditState(track, index);
	entry.insert(QStringLiteral("color"), track->color().has_value() ? track->color()->name() : QString());
	entry.insert(QStringLiteral("icon"), TrackLooks::instance().icon(track->id()));
	auto* instrument = dynamic_cast<InstrumentTrack*>(track);
	entry.insert(QStringLiteral("instrument"), instrument != nullptr ? instrument->instrumentName() : QString());
	MixerChannel* channel = channelOf(track);
	entry.insert(QStringLiteral("channel"), channelIdOf(channel));
	entry.insert(QStringLiteral("devices"), channel != nullptr ? static_cast<int>(channel->m_fxChain.effects().size()) : 0);
	entry.insert(QStringLiteral("cpu_percent"), channel != nullptr ? static_cast<double>(channel->dspLoad()) : 0.0);
	return entry;
}

ControlResult overview(const QJsonObject&)
{
	QJsonArray tracks;
	const TrackContainer::TrackList& list = Engine::getSong()->tracks();
	for (int i = 0; i < static_cast<int>(list.size()); ++i) { tracks.append(overviewEntry(list[i], i)); }
	return ControlResult::success({{QStringLiteral("tracks"), tracks}, {QStringLiteral("count"), tracks.size()}});
}

ControlResult setColor(const QJsonObject& args)
{
	ControlResult error;
	Track* track = resolveTrack(args.value(QStringLiteral("track")).toString(), &error);
	if (track == nullptr) { return error; }
	const QString text = args.value(QStringLiteral("color")).toString().trimmed();
	if (!text.isEmpty() && !QColor(text).isValid())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'%1' is not a colour (#rrggbb)").arg(text));
	}
	const int id = track->id();
	const std::optional<QColor> before = track->color();
	const std::optional<QColor> after = text.isEmpty() ? std::nullopt : std::optional<QColor>(QColor(text));
	paintTrack(id, after);
	addUndoStep([id, before] { paintTrack(id, before); }, [id, after] { paintTrack(id, after); });
	Engine::getSong()->setModified();
	return ControlResult::success({{QStringLiteral("track"), trackIdOf(track)},
		{QStringLiteral("color"), after.has_value() ? after->name() : QString()}});
}

ControlResult setVisible(const QJsonObject& args)
{
	ControlResult error;
	Track* track = resolveTrack(args.value(QStringLiteral("track")).toString(), &error);
	if (track == nullptr) { return error; }
	const int id = track->id();
	const bool before = track->isVisible();
	const bool after = args.value(QStringLiteral("visible")).toBool();
	if (after != before)
	{
		showTrack(id, after);
		addUndoStep([id, before] { showTrack(id, before); }, [id, after] { showTrack(id, after); });
		Engine::getSong()->setModified();
	}
	return ControlResult::success({{QStringLiteral("track"), trackIdOf(track)}, {QStringLiteral("visible"), after},
		{QStringLiteral("changed"), after != before}});
}

ControlResult setIcon(const QJsonObject& args)
{
	ControlResult error;
	Track* track = resolveTrack(args.value(QStringLiteral("track")).toString(), &error);
	if (track == nullptr) { return error; }
	const QString after = args.value(QStringLiteral("icon")).toString().trimmed();
	if (after.size() > MaxIconLength)
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("an icon name is at most %1 characters").arg(MaxIconLength));
	}
	if (!TrackLooks::instance().knowsIcon(after))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("there is no icon called '%1'").arg(after));
	}
	const int id = track->id();
	const QString before = TrackLooks::instance().icon(id);
	TrackLooks::instance().setIcon(id, after);
	addUndoStep([id, before] { TrackLooks::instance().setIcon(id, before); }, [id, after] { TrackLooks::instance().setIcon(id, after); });
	Engine::getSong()->setModified();
	return ControlResult::success({{QStringLiteral("track"), trackIdOf(track)}, {QStringLiteral("icon"), after}});
}

//! Each device of @a channel's chain with its own share of the period.
QJsonArray deviceLoads(const MixerChannel* channel)
{
	QJsonArray out;
	for (const Effect* effect : channel->m_fxChain.effects())
	{
		out.append(QJsonObject{{QStringLiteral("device"), effectIdOf(effect)}, {QStringLiteral("name"), effect->displayName()},
			{QStringLiteral("cpu_percent"), static_cast<double>(effect->dspLoad())}});
	}
	return out;
}

ControlResult cpuLoad(const QJsonObject&)
{
	QJsonArray channels;
	double total = 0.0;
	for (int i = 0; i < Engine::mixer()->numChannels(); ++i)
	{
		const MixerChannel* channel = Engine::mixer()->mixerChannel(i);
		const double percent = channel->dspLoad();
		total += percent;
		channels.append(QJsonObject{{QStringLiteral("channel"), channelIdOf(channel)}, {QStringLiteral("name"), channel->m_name},
			{QStringLiteral("cpu_percent"), percent}, {QStringLiteral("devices"), deviceLoads(channel)}});
	}
	return ControlResult::success({{QStringLiteral("channels"), channels}, {QStringLiteral("total_percent"), total},
		{QStringLiteral("engine_percent"), static_cast<double>(Engine::audioEngine()->cpuLoad())}});
}

void add(ControlRegistry& registry, const char* id, const QString& description, const QJsonObject& args, bool mutating,
	ControlResult (*handler)(const QJsonObject&))
{
	control::defineCommandWithSchema(registry, QLatin1String(id), description, args, mutating, handler);
}

} // namespace

void registerTrackLookCommands(ControlRegistry& registry)
{
	TrackLooks::instance();  // registers the section before any project loads
	add(registry, "track.overview", QStringLiteral("Every track in one list, for the track manager: id, index, name, type, "
		"colour, icon, muted, soloed, visible, folder, clip count, instrument, its mixer channel, that channel's device count and "
		"its processing load (cpu_percent, mixer.cpu_load's figure)."), objectSchema({}), false, overview);
	add(registry, "track.set_color", QStringLiteral("Give a track the colour 'color' (#rrggbb), or none with 'color' empty - "
		"its header and clips take it. Saved with the project; one undo step."),
		objectSchema({{QStringLiteral("track"), stringProperty()}, {QStringLiteral("color"), stringProperty()}},
			{QStringLiteral("track"), QStringLiteral("color")}), true, setColor);
	add(registry, "track.set_visible", QStringLiteral("Show a track in the arranger or hide it ('visible'). Hiding mutes "
		"nothing: a hidden track still plays. Saved with the project; one undo step."),
		objectSchema({{QStringLiteral("track"), stringProperty()}, {QStringLiteral("visible"), booleanProperty()}},
			{QStringLiteral("track"), QStringLiteral("visible")}), true, setVisible);
	add(registry, "track.set_icon", QStringLiteral("Give a track an icon - one of the interface's icon names (drums, piano, "
		"automation, record, ...) - that its header shows instead of its type's; empty removes it. Saved with the project; "
		"one undo step."),
		objectSchema({{QStringLiteral("track"), stringProperty()}, {QStringLiteral("icon"), stringProperty()}},
			{QStringLiteral("track"), QStringLiteral("icon")}), true, setIcon);
	add(registry, "mixer.cpu_load", QStringLiteral("How much of each audio period every mixer channel's devices and strip take, "
		"in percent of the period (smoothed over the last few dozen periods), each of its devices' own share ('devices': "
		"device id, name, cpu_percent), their sum, and the engine's whole load. An instrument's own synthesis runs before "
		"the mixer and is in engine_percent, not in its channel's figure. Read-only."),
		objectSchema({}), false, cpuLoad);
}

} // namespace lmms
