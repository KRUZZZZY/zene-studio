/*
 * MidiClockMenu.cpp - Edit > MIDI Clock: send, follow, choose the source and
 *                      see the lock
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
#include <QActionGroup>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QSignalBlocker>
#include <QStringList>

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
	QMenu* source;
	QActionGroup* sources;
	QAction* status;
};

ControlResult clockState()
{
	return ControlRegistry::instance()->invoke(QStringLiteral("clock.get_state"), QJsonObject{});
}

//! The readable ports the running client has right now (midi.device_list), which
//! is exactly the vocabulary clock.slave_set's source_port is spelled in - so the
//! submenu cannot offer a source the engine would refuse.
QStringList readableClockPorts()
{
	QStringList ports;
	const QJsonArray readable = ControlRegistry::instance()
		->invoke(QStringLiteral("midi.device_list"), QJsonObject{})
		.result.value(QStringLiteral("readable")).toArray();
	for (const QJsonValue& value : readable)
	{
		const QString port = value.toString();
		if (!port.isEmpty()) { ports.append(port); }
	}
	return ports;
}

//! One entry of the source submenu: the engine port whose name is @a port (empty
//! is "any input"), checkable, and choosing it changes the source WITHOUT
//! touching the mode. The mode is READ BACK when the entry is chosen rather than
//! captured when the entry is built, so a stale submenu cannot stop a follower
//! the user has since switched on: picking a port is one recorded undo step and
//! nothing else.
void addSourceItem(const ClockItems& items, const QString& port, const QString& label,
	const QString& chosen)
{
	QAction* item = items.source->addAction(label);
	item->setData(QStringLiteral("clock.slave_set"));
	item->setCheckable(true);
	// ONE choice: the sources are alternatives, not switches.
	items.sources->addAction(item);
	item->setChecked(port == chosen);
	QObject::connect(item, &QAction::triggered, items.source, [port] {
		const ControlResult state = clockState();
		if (!state.ok) { return; }
		const bool following =
			state.result.value(QStringLiteral("slave")).toObject().value(QStringLiteral("enabled")).toBool();
		ControlRegistry::instance()->invoke(QStringLiteral("clock.slave_set"),
			{{QStringLiteral("enabled"), following}, {QStringLiteral("source_port"), port}});
	});
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

/*! (Re)build the source submenu: "any input" plus one entry per readable client
 *  port, with the configured one ticked. Rebuilt whenever the submenu opens, so
 *  a device plugged in after the menu was built is offered and one unplugged is
 *  dropped - and with no MIDI backend the list is empty, which is why "any
 *  input" is offered alone: a client with one unnamed input (ALSA Raw-MIDI,
 *  JACK) names no port to choose. */
void rebuildSourceItems(const ClockItems& items)
{
	const QJsonObject slave =
		clockState().result.value(QStringLiteral("slave")).toObject();
	const QString chosen = slave.value(QStringLiteral("source_port")).toString();
	for (QAction* item : items.source->actions())
	{
		items.source->removeAction(item);
		item->deleteLater();
	}
	addSourceItem(items, QString(), QMenu::tr("Any Input"), chosen);
	for (const QString& port : readableClockPorts())
	{
		addSourceItem(items, port, port, chosen);
	}
}

} // namespace

QString midiClockStatusText(const QJsonObject& state)
{
	const QJsonObject master = state.value(QStringLiteral("master")).toObject();
	const QJsonObject slave = state.value(QStringLiteral("slave")).toObject();
	const bool enabled = master.value(QStringLiteral("enabled")).toBool();
	const bool routed = master.value(QStringLiteral("port_ready")).toBool();
	// The enabled flag says the clock is on; port_ready says a destination is subscribed to
	// receive it (MidiClock::masterPortReady). An enabled master with nothing subscribed emits
	// to nobody, and saying "sending" there would be a claim the engine cannot back.
	const QString sending = !enabled ? QMenu::tr("not sending")
		: routed ? QMenu::tr("sending") : QMenu::tr("sending, no output port");
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
	QMenu* source = clock->addMenu(QMenu::tr("Clock Source"));
	clock->addSeparator();
	const ClockItems items{send, follow, followTempo, source, new QActionGroup(source),
		clock->addAction(QString())};
	items.sources->setExclusive(true);
	items.status->setEnabled(false);
	// The line shows clock.get_state; declaring it keeps the item a registry action (A15).
	items.status->setData(QStringLiteral("clock.get_state"));

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
	QObject::connect(source, &QMenu::aboutToShow, source, [items] { rebuildSourceItems(items); });
	rebuildSourceItems(items);
	showState(items);
	return clock;
}

} // namespace lmms::gui
