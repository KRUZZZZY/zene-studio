/*
 * MixerStripInfo.cpp - what a mixer strip is and where it routes, in words: its type, format, routes and modes
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
#include "MixerStripInfo.h"

#include <QCoreApplication>
#include <QJsonArray>

#include "AudioEngine.h"
#include "Engine.h"
#include "HardwareIo.h"
#include "Mixer.h"
#include "MixerChannelView.h"
#include "MixerLatencyText.h"
#include "MixerListen.h"
#include "Song.h"
#include "Track.h"
#include "TrackFolder.h"

namespace lmms::gui::mixerstripinfo
{

namespace
{

QString tr(const char* text)
{
	return QCoreApplication::translate("MixerStripInfo", text);
}

const QChar kTo(0x2192);    // ->
const QChar kFrom(0x2190);  // <-
const QString kDot = QStringLiteral(" \u00b7 ");

ChannelType typeOf(const MixerChannel& channel, int index, const Context& context)
{
	if (index == 0) { return ChannelType::Master; }
	if (channel.isBus()) { return ChannelType::Return; }
	if (context.folderChannels.contains(index)) { return ChannelType::Group; }
	return context.tracksInto.contains(index) ? ChannelType::Track : ChannelType::Channel;
}

//! The format as it is printed on the badge: "LCR", "Quad", "5.1", "7.1"; empty for stereo.
QString formatText(const MixerChannel& channel)
{
	if (!channel.m_surround.isSurround()) { return {}; }
	const QString name = surround::formatName(channel.m_surround.format());
	if (name == QLatin1String("lcr")) { return QStringLiteral("LCR"); }
	return name == QLatin1String("quad") ? QStringLiteral("Quad") : name;
}

//! The direct out in force by its label; the master always has one (its main output), another channel only
//! when a project routed it.
QString hardwareOf(const MixerChannel& channel, bool master, const audioio::Side& outputs)
{
	if (!master && !channel.m_hardwareIo.outputSet) { return {}; }
	const audioio::Destination out = channel.m_hardwareIo.effectiveOutput(master);
	if (out.none()) { return master ? tr("no output") : QString(); }
	return audioio::destinationLabel(out, outputs);
}

QStringList statesOf(const MixerChannel& channel, int index, const Context& context)
{
	QStringList states;
	if (context.listening.contains(index)) { states << (context.preFaderListen ? QStringLiteral("pfl") : QStringLiteral("afl")); }
	if (channel.m_soloSafeModel.value()) { states << QStringLiteral("solo_safe"); }
	if (channel.m_surround.pan().enabled) { states << QStringLiteral("surround_pan"); }
	if (channel.m_hardwareIo.usesCues()) { states << QStringLiteral("cue"); }
	if (channel.m_hardwareIo.insert.enabled()) { states << QStringLiteral("hardware_insert"); }
	return states;
}

QString stateWord(const QString& key)
{
	if (key == QLatin1String("afl")) { return tr("AFL"); }
	if (key == QLatin1String("pfl")) { return tr("PFL"); }
	if (key == QLatin1String("solo_safe")) { return tr("Solo safe"); }
	if (key == QLatin1String("surround_pan")) { return tr("Surround pan"); }
	if (key == QLatin1String("cue")) { return tr("Cue"); }
	return key == QLatin1String("hardware_insert") ? tr("HW insert") : key;
}

//! The first name after an arrow, and how many more ("-> Master +2").
QString arrowLine(QChar arrow, const QStringList& names)
{
	QString text = arrow + QLatin1Char(' ') + names.value(0);
	if (names.size() > 1) { text += QStringLiteral(" +%1").arg(names.size() - 1); }
	return text;
}

//! A send by its receiver's name, its amount (when not full) and what sets it apart.
QString routeLabel(const Route& route, bool withAmount)
{
	QString label = route.to;
	if (withAmount && route.amount < 0.995) { label += QStringLiteral(" %1%").arg(qRound(route.amount * 100.0)); }
	if (route.preFader) { label += tr(" (pre)"); }
	if (!route.enabled) { label += tr(" (off)"); }
	return label;
}

//! One line per name, an arrow before each - the first three, then how many more.
QStringList arrowLines(QChar arrow, const QStringList& names)
{
	constexpr int Shown = 3;
	QStringList lines;
	for (const QString& name : names.mid(0, Shown)) { lines << arrow + QLatin1Char(' ') + name; }
	if (names.size() > Shown) { lines << tr("+%1 more").arg(names.size() - Shown); }
	return lines;
}

QStringList routeLabels(const StripInfo& info, bool withAmount)
{
	QStringList labels;
	for (const Route& route : info.outputs) { labels << routeLabel(route, withAmount); }
	return labels;
}

} // namespace

Context buildContext()
{
	Context context;
	if (Engine::getSong() != nullptr)
	{
		for (Track* track : Engine::getSong()->tracks())
		{
			const auto* folder = dynamic_cast<const TrackFolder*>(track);
			if (folder != nullptr && folder->mixerChannel() >= 0) { context.folderChannels.insert(folder->mixerChannel()); }
			if (IntModel* channel = track->mixerChannelModel()) { context.tracksInto[channel->value()] << track->name(); }
		}
	}
	context.outputs = audioio::setupFor(audioio::currentDeviceKey(), 2).outputs;
	for (const int index : mixerlisten::listeningChannels()) { context.listening.insert(index); }
	context.preFaderListen = mixerlisten::mode() == mixerlisten::Mode::PreFader;
	return context;
}

StripInfo describe(Mixer& mixer, int index, const Context& context)
{
	StripInfo info;
	const MixerChannel& channel = *mixer.mixerChannel(index);
	info.type = typeOf(channel, index, context);
	info.format = formatText(channel);
	info.inputs = context.tracksInto.value(index);
	for (const MixerRoute* route : channel.m_receives) { info.inputs << route->sender()->m_name; }
	for (MixerRoute* route : channel.m_sends)
	{
		info.outputs.append({route->receiver()->m_name, route->amount()->value(), route->preFader(), route->enabled()});
	}
	info.hardware = hardwareOf(channel, index == 0, context.outputs);
	info.states = statesOf(channel, index, context);
	return info;
}

QString typeName(ChannelType type)
{
	switch (type)
	{
	case ChannelType::Master: return tr("Master");
	case ChannelType::Return: return tr("Return");
	case ChannelType::Group: return tr("Group");
	case ChannelType::Track: return tr("Track");
	case ChannelType::Channel: break;
	}
	return tr("Channel");
}

QString typeKey(ChannelType type)
{
	static const QStringList keys{QStringLiteral("master"), QStringLiteral("return"), QStringLiteral("group"),
		QStringLiteral("track"), QStringLiteral("channel")};
	return keys.at(static_cast<int>(type));
}

QString badgeText(const StripInfo& info)
{
	return info.format.isEmpty() ? typeName(info.type) : typeName(info.type) + QLatin1Char(' ') + info.format;
}

QString routingSummary(const StripInfo& info, bool compact)
{
	const QStringList labels = routeLabels(info, false);
	if (compact) { return labels.isEmpty() ? QString() : arrowLine(kTo, labels); }
	QStringList lines = arrowLines(kTo, labels);
	if (!info.hardware.isEmpty()) { lines << tr("HW %1").arg(info.hardware); }
	return lines.join(QLatin1Char('\n'));
}

QString routingDetail(const StripInfo& info, bool compact)
{
	const QStringList labels = routeLabels(info, !compact);
	if (compact)
	{
		QStringList lines;
		if (!info.inputs.isEmpty()) { lines << arrowLine(kFrom, info.inputs); }
		if (!labels.isEmpty()) { lines << arrowLine(kTo, labels); }
		return lines.join(QLatin1Char('\n'));
	}
	QStringList lines = arrowLines(kFrom, info.inputs) + arrowLines(kTo, labels);
	if (!info.hardware.isEmpty()) { lines << tr("HW %1").arg(info.hardware); }
	return lines.join(QLatin1Char('\n'));
}

QString stateLine(const StripInfo& info, bool withHardware)
{
	QStringList words;
	for (const QString& key : info.states) { words << stateWord(key); }
	if (withHardware && !info.hardware.isEmpty()) { words << tr("HW %1").arg(info.hardware); }
	return words.join(kDot);
}

QString spokenText(const StripInfo& info)
{
	QStringList sentences{info.format.isEmpty() ? typeName(info.type) : tr("%1, %2 surround").arg(typeName(info.type), info.format)};
	if (!info.inputs.isEmpty()) { sentences << tr("From %1").arg(info.inputs.join(QStringLiteral(", "))); }
	const QStringList labels = routeLabels(info, true);
	if (!labels.isEmpty()) { sentences << tr("Sends to %1").arg(labels.join(QStringLiteral(", "))); }
	if (!info.hardware.isEmpty()) { sentences << tr("Hardware output %1").arg(info.hardware); }
	if (!info.states.isEmpty()) { sentences << stateLine(info, false).replace(kDot, QStringLiteral(", ")); }
	return sentences.join(QStringLiteral(". ")) + QLatin1Char('.');
}

QJsonObject toJson(const StripInfo& info)
{
	QJsonArray outputs;
	for (const Route& route : info.outputs)
	{
		outputs.append(QJsonObject{{QStringLiteral("to"), route.to}, {QStringLiteral("amount"), route.amount},
			{QStringLiteral("pre_fader"), route.preFader}, {QStringLiteral("enabled"), route.enabled}});
	}
	return {{QStringLiteral("type"), typeKey(info.type)},
		{QStringLiteral("format"), info.format.isEmpty() ? QStringLiteral("stereo") : info.format},
		{QStringLiteral("inputs"), QJsonArray::fromStringList(info.inputs)}, {QStringLiteral("outputs"), outputs},
		{QStringLiteral("hardware"), info.hardware}, {QStringLiteral("states"), QJsonArray::fromStringList(info.states)}};
}

void describeStrip(MixerChannelView* view, Mixer* mixer, int index)
{
	MixerChannel* channel = mixer->mixerChannel(index);
	const int rate = static_cast<int>(Engine::audioEngine()->outputSampleRate());
	QString tip = mixerLatencyText(channel->m_name, channel->m_fxChain.latencyFrames(),
		channel->inputLatencyFrames(), index == 0 ? mixer->totalLatencyFrames() : -1, rate,
		hardwareio::insertLatency(*channel), hardwareio::insertLatencySource(*channel));
	if (!channel->m_note.isEmpty()) { tip += QLatin1Char('\n') + channel->m_note; }
	if (channel->isBus()) { tip += QLatin1Char('\n') + tr("Return - sends into it are pre-fader"); }
	view->setToolTip(tip);
	if (view->accessibleName() != channel->m_name) { view->setAccessibleName(channel->m_name); }
}

} // namespace lmms::gui::mixerstripinfo
