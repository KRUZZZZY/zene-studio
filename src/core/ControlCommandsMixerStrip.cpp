/*
 * ControlCommandsMixerStrip.cpp - the 0.5.0 L08 strip commands: mute / solo many at once, and the strip's own settings
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

/*! 0.5.0 L08: the mixer strip's depth on the spine. mixer.set_many mutes, unmutes, solos or unsolos
 *  several channels - or every one - in ONE undo step (MX-027: "Unmute all", "Clear solo");
 *  mixer.set_strip writes a channel's own settings - trim, polarity, mono, pan mode, solo safe, note
 *  (MX-024 / MX-031 / MX-032 / MX-016) - as one undo step (the channel is saved with them);
 *  mixer.strip_get reads them and the meters (pre-fader peak, RMS, clip, gain reduction), and
 *  mixer.clear_clips clears the latched clip lights (MX-126); mixer.solo_mode picks exclusive or
 *  additive solo (MX-029). */

#include <cmath>

#include <QJsonArray>
#include <QVector>

#include "ControlCommandDef.h"
#include "ControlEdit.h"
#include "ControlMixerSupport.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Mixer.h"
#include "MixerStrip.h"
#include "Song.h"

namespace lmms
{

using namespace control;

namespace
{

//! One channel's mute and solo as they were, by persistent id (a channel may be renumbered).
struct StripFlags
{
	int id = 0;
	bool muted = false;
	bool soloed = false;
	//! Which of the two a write sets: a call that names only 'soloed' must not write back a mute taken while a solo
	//! held the channel muted (it re-muted it after the solo let go). An undo restores both.
	bool writeMute = true;
	bool writeSolo = true;
};

MixerChannel* channelById(int id)
{
	Mixer* mixer = Engine::mixer();
	for (int i = 0; i < mixer->numChannels(); ++i)
	{
		if (mixer->mixerChannel(i)->id() == id) { return mixer->mixerChannel(i); }
	}
	return nullptr;
}

void restoreFlags(const QVector<StripFlags>& flags)
{
	for (const StripFlags& flag : flags)
	{
		if (MixerChannel* channel = channelById(flag.id))
		{
			if (flag.writeMute) { channel->m_muteModel.setValue(flag.muted); }
			if (flag.writeSolo) { channel->m_soloModel.setValue(flag.soloed); }
		}
	}
}

//! The channels a call names: 'channels' (ch-<n> ids), or every channel but the master with 'all'.
bool namedChannels(const QJsonObject& args, QVector<MixerChannel*>* out, ControlResult* error)
{
	if (args.value(QStringLiteral("all")).toBool())
	{
		for (int i = 1; i < Engine::mixer()->numChannels(); ++i) { out->append(Engine::mixer()->mixerChannel(i)); }
		return true;
	}
	for (const QJsonValue& id : args.value(QStringLiteral("channels")).toArray())
	{
		MixerChannel* channel = resolveMixerChannel(id.toString(), error);
		if (channel == nullptr) { return false; }
		out->append(channel);
	}
	if (out->isEmpty()) { *error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("name 'channels' or set 'all'")); }
	return !out->isEmpty();
}

ControlResult setMany(const QJsonObject& args)
{
	ControlResult error;
	QVector<MixerChannel*> channels;
	if (!namedChannels(args, &channels, &error)) { return error; }
	if (!args.contains(QStringLiteral("muted")) && !args.contains(QStringLiteral("soloed")))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("set 'muted' and/or 'soloed'"));
	}
	// 'all' names every channel but the master, so a mixer left with only its master names
	// none: Unmute All / Clear Solo is then a successful no-op, not an error - and nothing
	// changed, so no undo step is recorded (the empty one would let control.undo claim a
	// step that puts nothing back).
	if (channels.isEmpty())
	{
		return ControlResult::success({{QStringLiteral("channels"), QJsonArray{}}, {QStringLiteral("count"), 0}});
	}
	QVector<StripFlags> before;
	QVector<StripFlags> after;
	for (MixerChannel* channel : channels)
	{
		const StripFlags was{channel->id(), channel->m_muteModel.value(), channel->m_soloModel.value()};
		before.append(was);
		after.append({was.id, args.value(QStringLiteral("muted")).toBool(was.muted), args.value(QStringLiteral("soloed")).toBool(was.soloed),
			args.contains(QStringLiteral("muted")), args.contains(QStringLiteral("soloed"))});
	}
	addUndoStep([before]() { restoreFlags(before); }, [after]() { restoreFlags(after); });
	restoreFlags(after);
	Engine::getSong()->setModified();
	QJsonArray ids;
	for (const MixerChannel* channel : channels) { ids.append(channelIdOf(channel)); }
	QJsonObject result{{QStringLiteral("channels"), ids}, {QStringLiteral("count"), ids.size()}};
	result.insert(QStringLiteral("__transaction"), transactionPayload({{QStringLiteral("channels"), ids}}, QStringLiteral("control.undo"), {},
		true, QStringLiteral("action checkpoint: the recorded undo step puts every channel's mute and solo back")));
	return ControlResult::success(result);
}

//! A channel's own settings, as set_strip writes them and its undo puts them back.
struct StripSettings
{
	float trim = 0.f;
	bool polarityLeft = false;
	bool polarityRight = false;
	bool mono = false;
	int panMode = 0;
	float panLeft = 0.f;
	float panRight = 0.f;
	float width = 1.f;
	bool soloSafe = false;
	QString note;
	bool swap = false;  // MX-033
	bool inputMono = false;  // MX-059
};

const QStringList& panModeNames()
{
	static const QStringList names{QStringLiteral("balance"), QStringLiteral("dual"), QStringLiteral("width")};
	return names;
}

StripSettings settingsOf(const MixerChannel* c)
{
	return {c->m_trimModel.value(), c->m_polarityLeftModel.value(), c->m_polarityRightModel.value(), c->m_monoModel.value(),
		c->m_panModeModel.value(), c->m_panModel.value(), c->m_panRightModel.value(), c->m_widthModel.value(),
		c->m_soloSafeModel.value(), c->m_note, c->m_swapModel.value(), c->m_inputMonoModel.value()};
}

void applySettings(int id, const StripSettings& s)
{
	MixerChannel* c = channelById(id);
	if (c == nullptr) { return; }
	c->m_trimModel.setValue(s.trim);
	c->m_polarityLeftModel.setValue(s.polarityLeft);
	c->m_polarityRightModel.setValue(s.polarityRight);
	c->m_monoModel.setValue(s.mono);
	c->m_panModeModel.setValue(s.panMode);
	c->m_panModel.setValue(s.panLeft);
	c->m_panRightModel.setValue(s.panRight);
	c->m_widthModel.setValue(s.width);
	c->m_soloSafeModel.setValue(s.soloSafe);
	c->m_note = s.note;
	c->m_swapModel.setValue(s.swap);
	c->m_inputMonoModel.setValue(s.inputMono);
}

//! @a args over @a s; switching into dual pan from two centred pans spreads them hard left and right.
StripSettings merged(StripSettings s, const QJsonObject& args)
{
	s.trim = static_cast<float>(args.value(QStringLiteral("trim_db")).toDouble(s.trim));
	s.polarityLeft = args.value(QStringLiteral("polarity_left")).toBool(s.polarityLeft);
	s.polarityRight = args.value(QStringLiteral("polarity_right")).toBool(s.polarityRight);
	s.mono = args.value(QStringLiteral("mono")).toBool(s.mono);
	s.swap = args.value(QStringLiteral("swap")).toBool(s.swap);
	s.inputMono = args.value(QStringLiteral("input_mono")).toBool(s.inputMono);
	if (args.contains(QStringLiteral("pan_mode")))
	{
		const int mode = panModeNames().indexOf(args.value(QStringLiteral("pan_mode")).toString());
		if (mode == 1 && s.panMode != 1 && s.panLeft == 0.f && s.panRight == 0.f) { s.panLeft = -1.f; s.panRight = 1.f; }
		s.panMode = mode;
	}
	s.panLeft = static_cast<float>(args.value(QStringLiteral("pan")).toDouble(s.panLeft));
	s.panRight = static_cast<float>(args.value(QStringLiteral("pan_right")).toDouble(s.panRight));
	s.width = static_cast<float>(args.value(QStringLiteral("width")).toDouble(s.width));
	s.soloSafe = args.value(QStringLiteral("solo_safe")).toBool(s.soloSafe);
	s.note = args.value(QStringLiteral("note")).toString(s.note);
	return s;
}

QJsonObject stripJson(const MixerChannel* c)
{
	const StripSettings s = settingsOf(c);
	return {{QStringLiteral("channel"), channelIdOf(c)}, {QStringLiteral("trim_db"), static_cast<double>(s.trim)},
		{QStringLiteral("polarity_left"), s.polarityLeft}, {QStringLiteral("polarity_right"), s.polarityRight},
		{QStringLiteral("mono"), s.mono}, {QStringLiteral("swap"), s.swap}, {QStringLiteral("pan_mode"), panModeNames().value(s.panMode)},
		{QStringLiteral("pan"), static_cast<double>(s.panLeft)}, {QStringLiteral("pan_right"), static_cast<double>(s.panRight)},
		{QStringLiteral("width"), static_cast<double>(s.width)}, {QStringLiteral("solo_safe"), s.soloSafe}, {QStringLiteral("note"), s.note},
		{QStringLiteral("meters"), QJsonObject{{QStringLiteral("pre_peak_left"), static_cast<double>(c->m_prePeakLeft.get())},
			{QStringLiteral("pre_peak_right"), static_cast<double>(c->m_prePeakRight.get())},
			{QStringLiteral("rms_left"), static_cast<double>(c->m_rmsLeft.get())}, {QStringLiteral("rms_right"), static_cast<double>(c->m_rmsRight.get())},
			{QStringLiteral("clipped"), c->m_clipped.load(std::memory_order_relaxed)},
			{QStringLiteral("gain_reduction_db"), static_cast<double>(c->m_gainReduction.get())},
			{QStringLiteral("correlation"), static_cast<double>(c->m_correlation.get())}}}, {QStringLiteral("input_mono"), s.inputMono}};
}

ControlResult setStrip(const QJsonObject& args)
{
	ControlResult error;
	MixerChannel* channel = resolveMixerChannel(args.value(QStringLiteral("channel")).toString(), &error);
	if (channel == nullptr) { return error; }
	// Only 'channel': nothing to set, so no undo step (mixer.strip_get is the report).
	if (args.size() == 1) { return ControlResult::success(stripJson(channel)); }
	const StripSettings before = settingsOf(channel);
	const StripSettings after = merged(before, args);
	if (std::abs(after.trim) > 24.f || after.panMode < 0 || after.width < 0.f || after.width > 2.f || std::abs(after.panLeft) > 1.f
		|| std::abs(after.panRight) > 1.f)
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'trim_db' is -24..24, 'pan_mode' balance / dual / width, "
			"'width' 0..2, 'pan' and 'pan_right' -1..1"));
	}
	const int id = channel->id();
	addUndoStep([id, before]() { applySettings(id, before); }, [id, after]() { applySettings(id, after); });
	applySettings(id, after);
	Engine::getSong()->setModified();
	QJsonObject result = stripJson(channel);
	result.insert(QStringLiteral("__transaction"), transactionPayload({{QStringLiteral("channel"), channelIdOf(channel)}}, QStringLiteral("control.undo"),
		{}, true, QStringLiteral("action checkpoint: the recorded undo step puts the channel's previous strip settings back")));
	return ControlResult::success(result);
}

ControlResult getStrip(const QJsonObject& args)
{
	ControlResult error;
	MixerChannel* channel = resolveMixerChannel(args.value(QStringLiteral("channel")).toString(), &error);
	if (channel == nullptr) { return error; }
	return ControlResult::success(stripJson(channel));
}

ControlResult clearClips(const QJsonObject&)
{
	for (int i = 0; i < Engine::mixer()->numChannels(); ++i)
	{
		MixerChannel* channel = Engine::mixer()->mixerChannel(i);
		channel->m_clipped.store(false, std::memory_order_relaxed);
		channel->m_prePeakLeft.set(0.f);
		channel->m_prePeakRight.set(0.f);
	}
	return ControlResult::success({{QStringLiteral("cleared"), Engine::mixer()->numChannels()}});
}

ControlResult soloMode(const QJsonObject& args)
{
	const bool was = mixerstrip::additiveSolo();
	if (args.contains(QStringLiteral("mode")))
	{
		const bool additive = args.value(QStringLiteral("mode")).toString() == QLatin1String("additive");
		if (additive != was)
		{
			addUndoStep([was]() { mixerstrip::setAdditiveSolo(was); }, [additive]() { mixerstrip::setAdditiveSolo(additive); });
			mixerstrip::setAdditiveSolo(additive);
		}
	}
	return ControlResult::success({{QStringLiteral("mode"), mixerstrip::additiveSolo() ? QStringLiteral("additive") : QStringLiteral("exclusive")}});
}

void add(ControlRegistry& registry, const char* verb, const QString& description, const QJsonObject& args,
	ControlResult (*handler)(const QJsonObject&), bool mutating)
{
	control::defineCommandWithSchema(registry, QStringLiteral("mixer.") + QLatin1String(verb), description, args, mutating, handler);
}

} // namespace

void registerMixerStripCommands(ControlRegistry& registry)
{
	add(registry, "set_strip", QStringLiteral("A channel's own settings: 'trim_db' (-24..24, an input gain before the inserts), "
		"'polarity_left' / 'polarity_right', 'mono', 'swap' (left and right exchanged), 'input_mono' (a mono source, summed "
		"before the inserts), 'pan_mode' (balance - the classic pan -, dual - 'pan' places the left "
		"side and 'pan_right' the right, so both centred sum the sides to mono; entering dual from centred pans spreads "
		"them hard left and right -, width - 'width' 0..2 scales the stereo side, then 'pan' balances), 'solo_safe' "
		"(soloing another channel never mutes this one) and 'note' (free text, shown as the strip's tooltip). A field left "
		"out keeps its value. Saved with the project; one undo step."),
		objectSchema({{QStringLiteral("channel"), stringProperty()}, {QStringLiteral("trim_db"), numberProperty()},
			{QStringLiteral("polarity_left"), booleanProperty()}, {QStringLiteral("polarity_right"), booleanProperty()},
			{QStringLiteral("mono"), booleanProperty()}, {QStringLiteral("swap"), booleanProperty()}, {QStringLiteral("input_mono"), booleanProperty()},
			{QStringLiteral("pan_mode"), enumProperty(panModeNames())},
			{QStringLiteral("pan"), numberProperty()}, {QStringLiteral("pan_right"), numberProperty()},
			{QStringLiteral("width"), numberProperty()}, {QStringLiteral("solo_safe"), booleanProperty()},
			{QStringLiteral("note"), stringProperty()}}, {QStringLiteral("channel")}), setStrip, true);
	add(registry, "strip_get", QStringLiteral("A channel's own settings (as mixer.set_strip writes them) and its meters: "
		"pre-fader peak (held since the last clear), RMS after the fader (this period), the latched clip light, and the "
		"deepest gain reduction (dB) an insert reported."), objectSchema({{QStringLiteral("channel"), stringProperty()}},
		{QStringLiteral("channel")}), getStrip, false);
	add(registry, "solo_mode", QStringLiteral("The solo mode: 'mode' exclusive (soloing a channel unsolos the others - the "
		"default) or additive (each solo adds to the ones already on). Left out, it reads the mode. A preference in the config "
		"file, not the project; the change is one undo step."), objectSchema({{QStringLiteral("mode"),
		enumProperty({QStringLiteral("exclusive"), QStringLiteral("additive")})}}), soloMode, true);
	add(registry, "clear_clips", QStringLiteral("Clear every channel's latched clip light and held pre-fader peak. Meter "
		"state: no undo step."), objectSchema({}), clearClips, false);

	ControlCommand cmd;  // mixer.set_many
	cmd.id = QStringLiteral("mixer.set_many");
	cmd.group = QStringLiteral("mixer");
	cmd.verb = QStringLiteral("set_many");
	cmd.description = QStringLiteral("Mute or solo several channels at once ('channels', ch-<n> ids) or every channel but the "
		"master ('all'): 'muted' and/or 'soloed' true or false - {all, muted:false} is Unmute All, {all, soloed:false} is "
		"Clear Solo. ONE undo step.");
	cmd.argsSchema = objectSchema({{QStringLiteral("channels"), arrayProperty()}, {QStringLiteral("all"), booleanProperty()},
		{QStringLiteral("muted"), booleanProperty()}, {QStringLiteral("soloed"), booleanProperty()}});
	cmd.resultSchema = objectProperty();
	cmd.mutating = true;
	cmd.handler = setMany;
	registry.registerCommand(cmd);
}

} // namespace lmms
