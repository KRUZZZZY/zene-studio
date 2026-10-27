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
#include "ControlMeterSupport.h"
#include "ControlMixerSupport.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Mixer.h"
#include "MixerStrip.h"
#include "Song.h"
#include "StripMeters.h"

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

bool flagsDiffer(const StripFlags& before, const StripFlags& after)
{
	return (after.writeMute && after.muted != before.muted) || (after.writeSolo && after.soloed != before.soloed);
}

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
	bool changed = false;
	for (MixerChannel* channel : channels)
	{
		const StripFlags was{channel->id(), channel->m_muteModel.value(), channel->m_soloModel.value()};
		const StripFlags wanted{was.id, args.value(QStringLiteral("muted")).toBool(was.muted),
			args.value(QStringLiteral("soloed")).toBool(was.soloed), args.contains(QStringLiteral("muted")),
			args.contains(QStringLiteral("soloed"))};
		changed = changed || flagsDiffer(was, wanted);
		before.append(was);
		after.append(wanted);
	}
	QJsonArray ids;
	for (const MixerChannel* channel : channels) { ids.append(channelIdOf(channel)); }
	QJsonObject result{{QStringLiteral("channels"), ids}, {QStringLiteral("count"), ids.size()}};
	if (!changed) { return ControlResult::success(result); }
	addUndoStep([before]() { restoreFlags(before); }, [after]() { restoreFlags(after); });
	restoreFlags(after);
	Engine::getSong()->setModified();
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
	int meterMode = 0;  // Q050-108 (StripMeters.h)
	bool operator==(const StripSettings&) const = default;
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
		c->m_soloSafeModel.value(), c->m_note, c->m_swapModel.value(), c->m_inputMonoModel.value(), stripmeters::mode(*c)};
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
	stripmeters::setMode(*c, s.meterMode);
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
	if (args.contains(QStringLiteral("meter_mode")))  // Q050-108: an unknown name leaves -1, which the caller refuses
	{
		s.meterMode = static_cast<int>(stripmeters::modeNames().indexOf(args.value(QStringLiteral("meter_mode")).toString()));
	}
	return s;
}

bool finiteStripSettings(const StripSettings& s)
{
	return std::isfinite(s.trim) && std::isfinite(s.panLeft) && std::isfinite(s.panRight) && std::isfinite(s.width);
}

//! Q050-108: what the strip's own meter modes read - the VU needle (VU against meter.set_reference) and, while the
//! strip is in 'lufs', its momentary and short-term loudness and true peak; null for no reading.
void addModeReadings(const MixerChannel* c, QJsonObject* meters)
{
	const double reference = meterReferenceDbfs();
	meters->insert(QStringLiteral("vu_left"), meterReadingJson(stripmeters::vu(*c, 0, reference)));
	meters->insert(QStringLiteral("vu_right"), meterReadingJson(stripmeters::vu(*c, 1, reference)));
	const StripLoudness* loudness = stripmeters::loudness(*c);
	const bool live = loudness != nullptr && stripmeters::mode(*c) == static_cast<int>(stripmeters::Mode::Lufs);
	meters->insert(QStringLiteral("lufs_momentary"), meterReadingJson(live ? loudness->momentaryLufs() : LufsMeter::MinusInfinity));
	meters->insert(QStringLiteral("lufs_short_term"), meterReadingJson(live ? loudness->shortTermLufs() : LufsMeter::MinusInfinity));
	meters->insert(QStringLiteral("true_peak_dbtp"), meterReadingJson(live ? loudness->truePeakDbtp() : LufsMeter::MinusInfinity));
	meters->insert(QStringLiteral("vu_reference_dbfs"), reference);
}

QJsonObject stripJson(const MixerChannel* c)
{
	const StripSettings s = settingsOf(c);
	QJsonObject meters{{QStringLiteral("pre_peak_left"), static_cast<double>(c->m_prePeakLeft.get())},
		{QStringLiteral("pre_peak_right"), static_cast<double>(c->m_prePeakRight.get())},
		{QStringLiteral("rms_left"), static_cast<double>(c->m_rmsLeft.get())}, {QStringLiteral("rms_right"), static_cast<double>(c->m_rmsRight.get())},
		{QStringLiteral("clipped"), c->m_clipped.load(std::memory_order_relaxed)},
		{QStringLiteral("gain_reduction_db"), static_cast<double>(c->m_gainReduction.get())},
		{QStringLiteral("correlation"), static_cast<double>(c->m_correlation.get())}};
	addModeReadings(c, &meters);
	return {{QStringLiteral("channel"), channelIdOf(c)}, {QStringLiteral("trim_db"), static_cast<double>(s.trim)},
		{QStringLiteral("polarity_left"), s.polarityLeft}, {QStringLiteral("polarity_right"), s.polarityRight},
		{QStringLiteral("mono"), s.mono}, {QStringLiteral("swap"), s.swap}, {QStringLiteral("pan_mode"), panModeNames().value(s.panMode)},
		{QStringLiteral("pan"), static_cast<double>(s.panLeft)}, {QStringLiteral("pan_right"), static_cast<double>(s.panRight)},
		{QStringLiteral("width"), static_cast<double>(s.width)}, {QStringLiteral("solo_safe"), s.soloSafe}, {QStringLiteral("note"), s.note},
		{QStringLiteral("meters"), meters}, {QStringLiteral("input_mono"), s.inputMono},
		{QStringLiteral("meter_mode"), stripmeters::modeNames().value(s.meterMode)}};
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
	if (!finiteStripSettings(after) || std::abs(after.trim) > 24.f || after.panMode < 0 || after.width < 0.f || after.width > 2.f || std::abs(after.panLeft) > 1.f
		|| std::abs(after.panRight) > 1.f || after.meterMode < 0)
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'trim_db' is -24..24, 'pan_mode' balance / dual / width, "
			"'width' 0..2, 'pan' and 'pan_right' -1..1, 'meter_mode' %1").arg(stripmeters::modeNames().join(QStringLiteral(" / "))));
	}
	if (after == before) { return ControlResult::success(stripJson(channel)); }
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
		"(soloing another channel never mutes this one), 'note' (free text, shown as the strip's tooltip) and, Q050-108, "
		"'meter_mode' - what this strip's meter shows: default (the mixer view's meter_mode, as every older project), peak "
		"(after the fader), rms, pre_fader, vu (a VU meter: 300 ms integration, 0 VU at meter.set_reference's level for a "
		"steady sine) or lufs (BS.1770 short-term loudness and true peak of this channel, after the fader); other strips "
		"keep theirs. A field left out keeps its value. Saved with the project; one undo step."),
		objectSchema({{QStringLiteral("channel"), stringProperty()}, {QStringLiteral("trim_db"), numberProperty()},
			{QStringLiteral("polarity_left"), booleanProperty()}, {QStringLiteral("polarity_right"), booleanProperty()},
			{QStringLiteral("mono"), booleanProperty()}, {QStringLiteral("swap"), booleanProperty()}, {QStringLiteral("input_mono"), booleanProperty()},
			{QStringLiteral("pan_mode"), enumProperty(panModeNames())},
			{QStringLiteral("pan"), numberProperty()}, {QStringLiteral("pan_right"), numberProperty()},
			{QStringLiteral("width"), numberProperty()}, {QStringLiteral("solo_safe"), booleanProperty()},
			{QStringLiteral("note"), stringProperty()}, {QStringLiteral("meter_mode"), enumProperty(stripmeters::modeNames())}},
			{QStringLiteral("channel")}), setStrip, true);
	add(registry, "strip_get", QStringLiteral("A channel's own settings (as mixer.set_strip writes them) and its meters: "
		"pre-fader peak (held since the last clear), RMS after the fader (this period), the latched clip light, and the "
		"deepest gain reduction (dB) an insert reported; Q050-108: its 'meter_mode' and, under 'meters', the VU needle "
		"('vu_left' / 'vu_right', in VU against 'vu_reference_dbfs') and - while the strip is in lufs - 'lufs_momentary', "
		"'lufs_short_term' and 'true_peak_dbtp' (null with no reading yet). meter.get_state and the shell's loudness "
		"readout stay the master's."), objectSchema({{QStringLiteral("channel"), stringProperty()}},
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
