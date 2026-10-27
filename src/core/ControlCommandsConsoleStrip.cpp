/*
 * ControlCommandsConsoleStrip.cpp - mixer.console_strip: one channel's Console section, set and read
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

/*! Q050-088: mixer.console_strip - what the mixer strip's Console section shows, for ONE channel: its trim,
 *  polarity and three-band EQ, and the drive and glue compressor of the Console on that channel
 *  (ConsoleStrip.h). Every value is checked before anything is written, so a refused call changes
 *  nothing and records no undo step; a call that changes something is one undo step (a knob dragged on
 *  the strip coalesces into one, keyed on the channel - docs/UNDO-BOUNDS.md). mixer.console_apply stays
 *  the across-the-mix action that puts the Console on every channel. */

#include "ConsoleStrip.h"
#include "ControlCommandDef.h"
#include "ControlEdit.h"
#include "ControlMixerSupport.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Mixer.h"
#include "Song.h"

namespace lmms
{

using namespace control;

namespace
{

void applyById(int id, const consolestrip::Settings& settings)
{
	Mixer* mixer = Engine::mixer();
	for (int i = 0; i < mixer->numChannels(); ++i)
	{
		if (mixer->mixerChannel(i)->id() == id) { consolestrip::apply(*mixer->mixerChannel(i), settings); }
	}
}

QJsonObject answer(const MixerChannel* channel, const consolestrip::Settings& settings)
{
	QJsonObject result = consolestrip::toJson(settings);
	result.insert(QStringLiteral("channel"), channelIdOf(channel));
	return result;
}

ControlResult consoleStrip(const QJsonObject& args)
{
	ControlResult error;
	MixerChannel* channel = resolveMixerChannel(args.value(QStringLiteral("channel")).toString(), &error);
	if (channel == nullptr) { return error; }
	const consolestrip::Settings before = consolestrip::read(*channel);
	consolestrip::Settings after = before;
	QString why;
	bool refused = false;
	if (!consolestrip::merge(args, &after, &why, &refused))
	{
		return ControlResult::failure(refused ? ControlErrorKind::Refused : ControlErrorKind::InvalidArgs, why);
	}
	if (after == before) { return ControlResult::success(answer(channel, before)); }
	consolestrip::apply(*channel, after);
	// A value the models round back to what they held (their step) changed nothing: no undo step, not modified.
	const consolestrip::Settings applied = consolestrip::read(*channel);
	if (applied == before) { return ControlResult::success(answer(channel, before)); }
	const int id = channel->id();
	addUndoStep([id, before]() { applyById(id, before); }, [id, applied]() { applyById(id, applied); });
	Engine::getSong()->setModified();
	QJsonObject result = answer(channel, consolestrip::read(*channel));
	result.insert(QStringLiteral("__transaction"), transactionPayload({{QStringLiteral("channel"), channelIdOf(channel)}},
		QStringLiteral("control.undo"), {}, true,
		QStringLiteral("action checkpoint: the recorded undo step puts the channel's previous Console section back")));
	return ControlResult::success(result);
}

} // namespace

void registerConsoleStripCommand(ControlRegistry& registry)
{
	const QJsonObject fields{{QStringLiteral("channel"), stringProperty()}, {QStringLiteral("trim_db"), numberProperty()},
		{QStringLiteral("polarity_left"), booleanProperty()}, {QStringLiteral("polarity_right"), booleanProperty()},
		{QStringLiteral("eq_low_db"), numberProperty()}, {QStringLiteral("eq_mid_db"), numberProperty()},
		{QStringLiteral("eq_mid_freq"), numberProperty()}, {QStringLiteral("eq_high_db"), numberProperty()},
		{QStringLiteral("drive"), numberProperty()}, {QStringLiteral("glue"), booleanProperty()},
		{QStringLiteral("glue_threshold_db"), numberProperty()}, {QStringLiteral("glue_ratio"), numberProperty()},
		{QStringLiteral("glue_makeup_db"), numberProperty()}};
	control::defineCommandWithSchema(registry, QStringLiteral("mixer.console_strip"), QStringLiteral(
		"Q050-088: one channel's Console section, as the mixer strip shows it (window.mixer_view 'show_console'): "
		"'trim_db' (-24..24, before the inserts), 'polarity_left' / 'polarity_right', the strip EQ - 'eq_low_db' (a "
		"shelf at 100 Hz), 'eq_mid_db' (a bell at 'eq_mid_freq', 100..10000 Hz), 'eq_high_db' (a shelf at 8 kHz), each "
		"-18..18 - and, when the channel carries a Console device (mixer.console_apply puts one on every channel), that "
		"Console's 'drive' (0..100 %) and its glue compressor: 'glue' (on or off - off by default, and then the Console "
		"sounds exactly as it did), 'glue_threshold_db' (-40..0), 'glue_ratio' (1..20) and 'glue_makeup_db' (0..24). "
		"Only this channel changes. A Console field on a channel without a Console is refused; any refusal changes "
		"nothing. A field left out keeps its value; with only 'channel' it reports. The answer has every field ('console' "
		"says whether the channel has one; its fields are null without). Saved with the project; one undo step."),
		objectSchema(fields, {QStringLiteral("channel")}), true, consoleStrip);
}

} // namespace lmms
