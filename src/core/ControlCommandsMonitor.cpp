/*
 * ControlCommandsMonitor.cpp - R2.1: track.set_monitor, the input-monitoring verb
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

/*! An audio track takes all three modes (InputMonitor.h). An instrument track takes Off and
 *  In: its live notes arrive on the MIDI thread, and Auto would have to read the record
 *  route's arm there - the song's track list, which only the render is excluded from while
 *  it changes. Refused rather than quietly treated as In. Other track types have no input.
 */

#include <QJsonObject>

#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "InputMonitor.h"
#include "SampleTrack.h"
#include "Track.h"

namespace lmms
{

namespace
{

const QString ClauseMonitor = QStringLiteral("ProjectJournal (Track checkpoint: Track::loadTrack "
	"resets the mode on absence, so restoring a checkpoint taken before the change takes it back)");

ControlResult trackSetMonitor(const QJsonObject& args)
{
	ControlResult error;
	Track* track = control::resolveTrack(args.value(QStringLiteral("track")).toString(), &error);
	if (track == nullptr) { return error; }
	MonitorMode mode = MonitorMode::Off;
	if (!monitorModeFromName(args.value(QStringLiteral("mode")).toString(), &mode))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'mode' is off, auto or in"));
	}
	const bool audio = track->type() == Track::Type::Sample;
	if (!audio && track->type() != Track::Type::Instrument)
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("%1 has no live input to monitor: audio and instrument tracks do")
				.arg(control::trackIdOf(track)));
	}
	if (!audio && mode == MonitorMode::Auto)
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("an instrument track monitors off or in in this build: auto would read "
				"the record route's arm on the MIDI thread, which cannot see the song's track list "
				"without a lock"));
	}
	const MonitorMode previous = track->monitorMode();
	if (previous != mode)
	{
		track->addJournalCheckPoint();
		track->setMonitorMode(mode);
	}
	QJsonObject result;
	result.insert(QStringLiteral("track"), control::trackIdOf(track));
	result.insert(QStringLiteral("mode"), monitorModeName(mode));
	result.insert(QStringLiteral("previous"), monitorModeName(previous));
	result.insert(QStringLiteral("changed"), previous != mode);
	if (previous != mode)
	{
		QJsonObject before;
		before.insert(QStringLiteral("track"), control::trackIdOf(track));
		before.insert(QStringLiteral("mode"), monitorModeName(previous));
		result.insert(QStringLiteral("__transaction"), control::transactionPayload(before,
			QStringLiteral("track.set_monitor"), before, true, ClauseMonitor));
	}
	return ControlResult::success(result);
}

} // namespace

void registerTrackMonitorCommands(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("track.set_monitor");
	cmd.group = QStringLiteral("track");
	cmd.verb = QStringLiteral("set_monitor");
	cmd.description = QStringLiteral("Set how a track treats its live input: `off` never heard, "
		"`in` always heard, `auto` heard while the track is record-armed (track.set_arm) and not "
		"playing a clip. An audio track's input is its record route's input channel, played "
		"through the track's own devices, fader, pan and mixer channel; its default is off. An "
		"instrument track takes off or in (default in: every live note plays, as before); off "
		"keeps live notes out of the instrument. track.get_state reports `monitor`, and "
		"`monitor_frames` - the frames an audio track's gate has passed. Saved with the project "
		"only when not the default; reversible through the ProjectJournal (Track checkpoint).");
	cmd.argsSchema = control::objectSchema({
		{QStringLiteral("track"), control::stringProperty()},
		{QStringLiteral("mode"), control::enumProperty({QStringLiteral("off"), QStringLiteral("auto"),
			QStringLiteral("in")})},
	}, {QStringLiteral("track"), QStringLiteral("mode")});
	cmd.resultSchema = control::objectSchema({
		{QStringLiteral("track"), control::stringProperty()},
		{QStringLiteral("mode"), control::stringProperty()},
		{QStringLiteral("previous"), control::stringProperty()},
		{QStringLiteral("changed"), control::booleanProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) { return trackSetMonitor(args); };
	registry.registerCommand(cmd);
}

} // namespace lmms
