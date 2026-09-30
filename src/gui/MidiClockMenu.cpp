/*
 * MidiClockMenu.cpp - Edit > MIDI Clock: send, follow and see the lock
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

#include "MidiClockMenu.h"

#include <QAction>
#include <QJsonObject>
#include <QMenu>
#include <QSignalBlocker>

#include "ControlRegistry.h"

namespace lmms::gui
{

namespace
{

struct ClockItems
{
	QAction* send;
	QAction* follow;
	QAction* followTempo;
	QAction* status;
};

ControlResult clockState()
{
	return ControlRegistry::instance()->invoke(QStringLiteral("clock.get_state"), QJsonObject{});
}

//! Sets every item from clock.get_state; @a refusal, when non-empty, replaces the indicator.
void showState(const ClockItems& items, const QString& refusal = QString())
{
	const ControlResult state = clockState();
	const QJsonObject master = state.result.value(QStringLiteral("master")).toObject();
	const QJsonObject slave = state.result.value(QStringLiteral("slave")).toObject();
	const bool following = slave.value(QStringLiteral("enabled")).toBool();
	for (QAction* item : {items.send, items.follow, items.followTempo})
	{
		const QSignalBlocker block(item);
		item->setEnabled(state.ok);
	}
	{
		const QSignalBlocker block(items.send);
		items.send->setChecked(master.value(QStringLiteral("enabled")).toBool());
	}
	{
		const QSignalBlocker block(items.follow);
		items.follow->setChecked(following);
	}
	{
		const QSignalBlocker block(items.followTempo);
		items.followTempo->setChecked(slave.value(QStringLiteral("follow_tempo")).toBool());
		items.followTempo->setEnabled(state.ok && following);
	}
	items.status->setText(!refusal.isEmpty() ? refusal
		: state.ok ? midiClockStatusText(state.result) : state.errorMessage);
}

QString refusalOf(const ControlResult& result)
{
	return result.ok ? QString() : QMenu::tr("Refused: %1").arg(result.errorMessage);
}

} // namespace

QString midiClockStatusText(const QJsonObject& state)
{
	const QJsonObject master = state.value(QStringLiteral("master")).toObject();
	const QJsonObject slave = state.value(QStringLiteral("slave")).toObject();
	const QString sending = master.value(QStringLiteral("enabled")).toBool()
		? QMenu::tr("sending") : QMenu::tr("not sending");
	if (!slave.value(QStringLiteral("enabled")).toBool())
	{
		return QMenu::tr("Clock %1, not following").arg(sending);
	}
	if (!slave.value(QStringLiteral("locked")).toBool())
	{
		return QMenu::tr("Clock %1, following: no lock").arg(sending);
	}
	return QMenu::tr("Clock %1, following: locked at %2 BPM").arg(sending)
		.arg(slave.value(QStringLiteral("tempo_bpm")).toDouble(), 0, 'f', 1);
}

QMenu* addMidiClockMenu(QMenu* menu)
{
	QMenu* clock = menu->addMenu(QMenu::tr("MIDI Clock"));
	QAction* send = clock->addAction(QMenu::tr("Send Clock"));
	QAction* follow = clock->addAction(QMenu::tr("Follow External Clock"));
	QAction* followTempo = clock->addAction(QMenu::tr("Follow Its Tempo"));
	clock->addSeparator();
	const ClockItems items{send, follow, followTempo, clock->addAction(QString())};
	items.status->setEnabled(false);

	items.send->setCheckable(true);
	items.send->setData(QStringLiteral("clock.master_set"));
	items.send->setToolTip(QMenu::tr("Send MIDI clock, start, stop and song position on the transport's own edges"));
	items.follow->setCheckable(true);
	items.follow->setData(QStringLiteral("clock.slave_set"));
	items.follow->setToolTip(QMenu::tr("Measure the tempo of an incoming MIDI clock"));
	items.followTempo->setCheckable(true);
	items.followTempo->setData(QStringLiteral("clock.slave_set"));
	items.followTempo->setToolTip(QMenu::tr("Write the measured tempo to the song while following"));

	QObject::connect(items.send, &QAction::toggled, clock, [items](bool on) {
		showState(items, refusalOf(ControlRegistry::instance()->invoke(QStringLiteral("clock.master_set"),
			{{QStringLiteral("enabled"), on}})));
	});
	QObject::connect(items.follow, &QAction::toggled, clock, [items](bool on) {
		showState(items, refusalOf(ControlRegistry::instance()->invoke(QStringLiteral("clock.slave_set"),
			{{QStringLiteral("enabled"), on}})));
	});
	QObject::connect(items.followTempo, &QAction::toggled, clock, [items](bool on) {
		showState(items, refusalOf(ControlRegistry::instance()->invoke(QStringLiteral("clock.slave_set"),
			{{QStringLiteral("enabled"), true}, {QStringLiteral("follow_tempo"), on}})));
	});
	QObject::connect(clock, &QMenu::aboutToShow, clock, [items] { showState(items); });
	showState(items);
	return clock;
}

} // namespace lmms::gui
