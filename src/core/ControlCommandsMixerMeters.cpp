/*
 * ControlCommandsMixerMeters.cpp - send meters and comparing two channels' EQ
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

#include <algorithm>
#include <cmath>

#include <QJsonArray>

#include "ControlCommandDef.h"
#include "ControlRoom.h"
#include "ControlMixerSupport.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "Song.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "AudioEngine.h"
#include "Mixer.h"
#include "MixerOptions.h"
#include "MixerStrip.h"
#include "PreviewOutput.h"

namespace lmms
{

using namespace control;

namespace
{

double toDb(float level) { return level > 0.f ? 20.0 * std::log10(level) : -120.0; }

QJsonArray sendsOf(MixerChannel* channel)
{
	QJsonArray sends;
	for (MixerRoute* route : channel->m_sends)
	{
		const std::array<float, 2> level = mixerstrip::sendLevel(*route);
		sends.append(QJsonObject{{QStringLiteral("from"), channelIdOf(channel)}, {QStringLiteral("to"), channelIdOf(route->receiver())},
			{QStringLiteral("left_db"), toDb(level[0])}, {QStringLiteral("right_db"), toDb(level[1])}});
	}
	return sends;
}

//! MX-053: the level each send carries this period - the sender's peak after its inserts, times the send (and the
//! fader for a post-fader send); a switched-off send or a muted sender reads silence.
ControlResult sendMeters(const QJsonObject& args)
{
	if (args.contains(QStringLiteral("channel")))  // an empty id is a bad channel, not "every channel"
	{
		ControlResult error;
		MixerChannel* channel = resolveMixerChannel(args.value(QStringLiteral("channel")).toString(), &error);
		if (channel == nullptr) { return error; }
		return ControlResult::success({{QStringLiteral("sends"), sendsOf(channel)}});
	}
	QJsonArray all;
	for (int i = 0; i < Engine::mixer()->numChannels(); ++i)
	{
		for (const QJsonValue& send : sendsOf(Engine::mixer()->mixerChannel(i))) { all.append(send); }
	}
	return ControlResult::success({{QStringLiteral("sends"), all}});
}

QJsonObject eqSettings(const MixerChannel& channel)
{
	return {{QStringLiteral("channel"), channelIdOf(&channel)}, {QStringLiteral("low"), static_cast<double>(channel.m_eqLowModel.value())},
		{QStringLiteral("mid"), static_cast<double>(channel.m_eqMidModel.value())},
		{QStringLiteral("mid_freq"), static_cast<double>(channel.m_eqMidFreqModel.value())},
		{QStringLiteral("high"), static_cast<double>(channel.m_eqHighModel.value())}};
}

//! MX-140: two channels' strip EQ side by side - their settings, B minus A per setting, and both responses
//! at 'points' frequencies spread evenly on a log scale from 20 Hz to 20 kHz.
ControlResult eqCompare(const QJsonObject& args)
{
	ControlResult error;
	const MixerChannel* a = resolveMixerChannel(args.value(QStringLiteral("a")).toString(), &error);
	if (a == nullptr) { return error; }
	const MixerChannel* b = resolveMixerChannel(args.value(QStringLiteral("b")).toString(), &error);
	if (b == nullptr) { return error; }
	const int points = std::clamp(args.value(QStringLiteral("points")).toInt(31), 2, 128);
	const double rate = Engine::audioEngine()->outputSampleRate();
	QJsonArray curve;
	for (int i = 0; i < points; ++i)
	{
		const double hz = 20.0 * std::pow(1000.0, static_cast<double>(i) / (points - 1));
		curve.append(QJsonObject{{QStringLiteral("hz"), hz}, {QStringLiteral("a_db"), mixerstrip::eqResponseDb(*a, hz, rate)},
			{QStringLiteral("b_db"), mixerstrip::eqResponseDb(*b, hz, rate)}});
	}
	const QJsonObject sa = eqSettings(*a);
	const QJsonObject sb = eqSettings(*b);
	QJsonObject difference;
	for (const QString& key : {QStringLiteral("low"), QStringLiteral("mid"), QStringLiteral("mid_freq"), QStringLiteral("high")})
	{
		difference.insert(key, sb.value(key).toDouble() - sa.value(key).toDouble());
	}
	return ControlResult::success({{QStringLiteral("a"), sa}, {QStringLiteral("b"), sb}, {QStringLiteral("difference"), difference},
		{QStringLiteral("curve"), curve}});
}

const QStringList& crossfadeSides()
{
	static const QStringList sides{QStringLiteral("none"), QStringLiteral("A"), QStringLiteral("B")};
	return sides;
}

//! MX-172: the crossfader's position, and which side a channel is on. One undo step.
ControlResult crossfader(const QJsonObject& args)
{
	ControlResult error;
	MixerChannel* channel = nullptr;
	if (args.contains(QStringLiteral("channel")) && (channel = resolveMixerChannel(args.value(QStringLiteral("channel")).toString(), &error)) == nullptr) { return error; }
	const float before = MixerOptions::instance().crossfader();
	const bool sided = args.contains(QStringLiteral("side"));
	if (sided && channel == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'side' puts a 'channel' on a side: name the channel"));
	}
	if (args.contains(QStringLiteral("position")))
	{
		const auto after = static_cast<float>(std::clamp(args.value(QStringLiteral("position")).toDouble(), -1.0, 1.0));
		MixerOptions::instance().setCrossfader(after);
		addUndoStep([before]() { MixerOptions::instance().setCrossfader(before); }, [after]() { MixerOptions::instance().setCrossfader(after); });
	}
	if (sided)
	{
		addUndoStep(QVector<JournallingObject*>{&channel->m_crossfadeSideModel});
		channel->m_crossfadeSideModel.setValue(static_cast<int>(crossfadeSides().indexOf(args.value(QStringLiteral("side")).toString())));
	}
	// A call with neither is a report and leaves the project as it was.
	if (sided || args.contains(QStringLiteral("position"))) { Engine::getSong()->setModified(); }
	QJsonObject result{{QStringLiteral("position"), static_cast<double>(MixerOptions::instance().crossfader())}};
	if (channel != nullptr) { result.insert(QStringLiteral("side"), crossfadeSides().value(channel->m_crossfadeSideModel.value())); }
	return ControlResult::success(result);
}

//! MX-042: the channel the browser's previews play into.
ControlResult previewOutput(const QJsonObject& args)
{
	if (args.contains(QStringLiteral("channel")))
	{
		ControlResult error;
		const MixerChannel* channel = resolveMixerChannel(args.value(QStringLiteral("channel")).toString(), &error);
		if (channel == nullptr) { return error; }
		previewoutput::setChannel(channel->index());
	}
	return ControlResult::success({{QStringLiteral("channel"), channelIdOf(Engine::mixer()->mixerChannel(previewoutput::channel()))}});
}

QJsonObject controlRoomReport(const controlroom::State& s)
{
	return {{QStringLiteral("speaker"), QString(QChar('A' + s.speaker))}, {QStringLiteral("trims_db"), QJsonArray{s.trimsDb[0], s.trimsDb[1], s.trimsDb[2]}},
		{QStringLiteral("dim"), s.dim}, {QStringLiteral("dim_db"), s.dimDb}, {QStringLiteral("mono"), s.mono}, {QStringLiteral("cut"), s.cut}};
}

//! MX-038 / MX-039: the monitor section on the live output.
ControlResult controlRoom(const QJsonObject& args)
{
	controlroom::State s = controlroom::state();
	const QString speaker = args.value(QStringLiteral("speaker")).toString(QString(QChar('A' + s.speaker)));
	if (speaker.size() != 1 || speaker[0] < QLatin1Char('A') || speaker[0] > QLatin1Char('C'))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'speaker' is A, B or C"));
	}
	s.speaker = speaker[0].unicode() - 'A';
	if (args.contains(QStringLiteral("trim_db"))) { s.trimsDb[s.speaker] = static_cast<float>(std::clamp(args.value(QStringLiteral("trim_db")).toDouble(), -24.0, 12.0)); }
	s.dim = args.value(QStringLiteral("dim")).toBool(s.dim);
	s.dimDb = static_cast<float>(std::clamp(args.value(QStringLiteral("dim_db")).toDouble(s.dimDb), -60.0, 0.0));
	s.mono = args.value(QStringLiteral("mono")).toBool(s.mono);
	s.cut = args.value(QStringLiteral("cut")).toBool(s.cut);
	controlroom::setState(s);
	return ControlResult::success(controlRoomReport(s));
}

} // namespace

void registerMixerMeterCommands(ControlRegistry& registry)
{
	defineCommand(registry, QStringLiteral("mixer.send_meters"), QStringLiteral("How loud each send is this period, left and right "
		"in dB (-120 for silence): the sender's level after its inserts times the send amount (and its fader, for a post-fader "
		"send). A switched-off send or a muted sender reads silence. 'channel' limits it to that channel's sends. Read only."),
		{{QStringLiteral("channel"), stringProperty()}}, {}, false, sendMeters);
	defineCommand(registry, QStringLiteral("mixer.eq_compare"), QStringLiteral("Compare the strip EQ of channels 'a' and 'b': each one's "
		"settings, B minus A for each, and both responses in dB at 'points' (2..128, 31 by default) frequencies from 20 Hz to "
		"20 kHz on a log scale. Read only."),
		{{QStringLiteral("a"), stringProperty()}, {QStringLiteral("b"), stringProperty()}, {QStringLiteral("points"), integerProperty()}},
		{QStringLiteral("a"), QStringLiteral("b")}, false, eqCompare);
	controlroom::loadPreferences();
	defineCommand(registry, QStringLiteral("monitor.control_room"), QStringLiteral("The control room - what the speakers get, after the "
		"master, never a render: 'speaker' A, B or C with its calibration 'trim_db' (-24..12, kept per speaker), 'dim' (by "
		"'dim_db', -20 unless set), 'mono' (a mono check) and 'cut'. Exports skip it and projects do not save it; the trims "
		"and the speaker are preferences, dim, mono and cut start off. With no arguments it reports it."),
		{{QStringLiteral("speaker"), stringProperty()}, {QStringLiteral("trim_db"), numberProperty()}, {QStringLiteral("dim"), booleanProperty()},
			{QStringLiteral("dim_db"), numberProperty()}, {QStringLiteral("mono"), booleanProperty()}, {QStringLiteral("cut"), booleanProperty()}},
		{}, false, controlRoom);
	defineCommand(registry, QStringLiteral("mixer.crossfader"), QStringLiteral("The crossfader: 'position' -1 (side A only) .. 1 (side "
		"B only), both sides full at 0; 'channel' with 'side' (A, B or none) puts a channel on a side. Channels on no side are "
		"untouched. Saved with the project. One undo step."),
		{{QStringLiteral("position"), numberProperty()}, {QStringLiteral("channel"), stringProperty()}, {QStringLiteral("side"), enumProperty(crossfadeSides())}},
		{}, true, crossfader);
	defineCommand(registry, QStringLiteral("browser.preview_output"), QStringLiteral("The mixer channel the browser's previews (a sample, "
		"a preset) play into - a cue bus, say, instead of the main mix. The master by default. A preference kept in the config "
		"file; with no arguments it reports it."), {{QStringLiteral("channel"), stringProperty()}}, {}, false, previewOutput);
}

} // namespace lmms
