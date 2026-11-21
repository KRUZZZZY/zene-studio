/*
 * MixerHardwareMenu.cpp - a mixer channel's "Hardware I/O" menu: output, cue sends, hardware insert
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

#include "MixerHardwareMenu.h"

#include <algorithm>

#include <QJsonArray>
#include <QAction>
#include <QMenu>

#include "CommandForm.h"
#include "ControlRegistry.h"
#include "MixerSurround.h"
#include "ShellMenuParts.h"
#include "SurroundPanner.h"

namespace lmms::gui
{

using namespace menuparts;

namespace
{

QJsonObject read(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args).result;
}

//! One output choice: runs mixer.hardware_output at once, checked when it is the channel's route.
void outputChoice(QMenu* menu, const QString& label, const QString& output, const QString& channelId, bool current)
{
	QAction* action = addCommandAction(menu, label, QStringLiteral("mixer.hardware_output"), [channelId, output] {
		return CommandCall{QStringLiteral("mixer.hardware_output"), {{QStringLiteral("channel"), channelId}, {QStringLiteral("output"), output}},
			{}, QString(), false};
	});
	action->setCheckable(true);
	action->setChecked(current);
}

void outputMenu(QMenu* menu, const QString& channelId, const QJsonObject& state)
{
	QMenu* outputs = menu->addMenu(tr("Output"));
	const QJsonObject route = state.value(QStringLiteral("output")).toObject();
	const bool isDefault = route.value(QStringLiteral("default")).toBool(true);
	const QString channels = route.value(QStringLiteral("channels")).toString();
	outputChoice(outputs, tr("Default"), QStringLiteral("default"), channelId, isDefault);
	outputChoice(outputs, tr("None"), QStringLiteral("none"), channelId, !isDefault && channels == QStringLiteral("none"));
	outputs->addSeparator();
	const QJsonArray buses = read(QStringLiteral("audio.io_list")).value(QStringLiteral("outputs")).toObject().value(QStringLiteral("buses")).toArray();
	for (const QJsonValue& value : buses)
	{
		const QJsonObject bus = value.toObject();
		const QString pair = bus.value(QStringLiteral("channels")).toString();
		QString label = QStringLiteral("%1 (%2)").arg(bus.value(QStringLiteral("name")).toString(), pair);
		if (!bus.value(QStringLiteral("available")).toBool()) { label += tr(" - not open"); }
		outputChoice(outputs, label, pair, channelId, !isDefault && channels == pair);
	}
}

void cueSendMenu(QMenu* menu, const QString& channelId, const QJsonArray& cues)
{
	QMenu* sends = menu->addMenu(tr("Cue Sends"));
	for (const QJsonValue& value : cues)
	{
		const QJsonObject cue = value.toObject();
		const QString letter = cue.value(QStringLiteral("cue")).toString();
		const QString label = QStringLiteral("%1 (%2)...").arg(cue.value(QStringLiteral("name")).toString(), letter);
		addCommandAction(sends, label, QStringLiteral("mixer.cue_send"), [channelId, letter] {
			return CommandCall{QStringLiteral("mixer.cue_send"), {{QStringLiteral("channel"), channelId}, {QStringLiteral("cue"), letter}},
				{{QStringLiteral("level"), 1.0}}, QString(), true};
		});
	}
}

/*! Q050-155.1: the Format submenu - one entry per name mixer.channel_format's schema enum advertises
 *  (surround::formatNames() is that enum). The named layouts, mono and discrete-2 .. discrete-8 stay on it;
 *  the wide discrete layouts (9 .. 64 channels) are grouped into submenus of sixteen counts so the list
 *  stays short. Every entry runs the registered command, exactly as the agent does. */
void formatMenu(QMenu* menu, const QString& channelId)
{
	QMenu* formats = menu->addMenu(tr("Format"));
	QMenu* group = nullptr;
	int groupEnd = 0;
	for (const QString& name : surround::formatNames())
	{
		surround::Format format = surround::Format::Stereo;
		surround::formatFromName(name, &format);
		const int count = surround::discreteChannels(format);
		QMenu* target = formats;
		if (count > surround::MaxSpeakers)
		{
			if (count > groupEnd)
			{
				groupEnd = std::min(surround::MaxDiscreteChannels, (count + 15) / 16 * 16);
				group = formats->addMenu(tr("Discrete %1-%2").arg(count).arg(groupEnd));
			}
			target = group;
		}
		addCommandAction(target, name, QStringLiteral("mixer.channel_format"), [channelId, name] {
			return CommandCall{QStringLiteral("mixer.channel_format"),
				{{QStringLiteral("channel"), channelId}, {QStringLiteral("format"), name}}, {}, QString(), false};
		});
	}
}

} // namespace

void addHardwareMenu(QMenu* menu, const QString& channelId)
{
	QMenu* hardware = menu->addMenu(tr("Hardware I/O"));
	const QJsonObject all = read(QStringLiteral("mixer.hardware_get"), {{QStringLiteral("channel"), channelId}});
	const QJsonArray channels = all.value(QStringLiteral("channels")).toArray();
	const QJsonObject state = channels.isEmpty() ? QJsonObject() : channels.at(0).toObject();
	const QJsonObject self{{QStringLiteral("channel"), channelId}};
	outputMenu(hardware, channelId, state);
	cueSendMenu(hardware, channelId, all.value(QStringLiteral("cues")).toArray());
	hardware->addSeparator();
	add(hardware, "Hardware Insert...", "mixer.hardware_insert", self, true);
	QAction* ping = add(hardware, "Measure Insert Latency", "mixer.hardware_insert_ping", self, false, {}, true);
	ping->setEnabled(state.value(QStringLiteral("insert")).toObject().value(QStringLiteral("enabled")).toBool());
	// Q050-089.2: the project rendered through the device, so the inserts that are on are heard (render.realtime's form:
	// the file and the tail; the reply, or the refusal when the device cannot carry them, is shown in it).
	add(hardware, "Render Through Hardware (Real Time)...", "render.realtime", {}, true, {}, true);
	hardware->addSeparator();
	add(hardware, "Cue Mixes...", "mixer.cue_master", {}, true);
	add(hardware, "Interface Channels...", "audio.io_setup", {}, true);
	add(hardware, "Hardware Routing", "audio.io_list", {}, false, {}, true);
}

void addSurroundMenu(QMenu* menu, const QString& channelId)
{
	QMenu* surround = menu->addMenu(tr("Surround"));
	formatMenu(surround, channelId);
	QAction* panner = surround->addAction(tr("Surround Panner..."));
	panner->setProperty("controlCommand", QStringLiteral("mixer.surround_pan"));
	QObject::connect(panner, &QAction::triggered, panner, [channelId] { showSurroundPanner(channelId); });
	surround->addSeparator();
	add(surround, "Downmix Options...", "mixer.downmix_options", {}, true);
	add(surround, "Surround Routing", "mixer.surround_get", {}, false, {}, true);
}

} // namespace lmms::gui
