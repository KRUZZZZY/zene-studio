/*
 * ControlPluginBusesTest.cpp - plugin.output_buses, output_bus_route and output_buses_create_channels
 *
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

/*! The multi-output commands, driven against an instrument that really owns output buses (the in-tree test
 *  instrument tests/src/plugins/BusesTestInstrument.cpp: Main plus Kick and Snare active, Aux that turns on when
 *  asked, Locked that never does): the listing and its fields, routing a bus to a channel and un-routing it,
 *  the refusals (a parallel bus, a missing channel, a bus past the list, a bus the plug-in will not activate, an
 *  instrument with no buses at all), that routing to the channel a bus already uses records no undo step, and the
 *  one-step "a channel per bus" setup with its undo. */

#include <QtTest>

#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>

#include "ControlEdit.h"
#include "ControlInstrumentCommands.h"
#include "ControlMixerSupport.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "Instrument.h"
#include "InstrumentOutputBuses.h"
#include "InstrumentTrack.h"
#include "Mixer.h"
#include "PluginFactory.h"
#include "Song.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

QString channelId(int mixerIndex) { return QStringLiteral("ch-%1").arg(Engine::mixer()->mixerChannel(mixerIndex)->id()); }

QJsonObject busEntry(const ControlResult& state, int bus)
{
	return state.result.value(QStringLiteral("buses")).toArray().at(bus).toObject();
}

} // namespace

class ControlPluginBusesTest : public QObject
{
	Q_OBJECT

	QTemporaryDir m_cache;
	QString m_target;   //!< a track holding the test instrument
	QString m_plain;    //!< a track whose instrument has a single output

private slots:
	void initTestCase()
	{
		QVERIFY(m_cache.isValid());
		qputenv("LMMS_PLUGIN_DIR", QByteArrayLiteral(LMMS_BUSES_TEST_INSTRUMENT_DIR));
		qputenv("LMMS_PLUGIN_SCAN_CACHE", QFile::encodeName(m_cache.filePath(QStringLiteral("scan-cache.json"))));
		Engine::init(true);
		ControlRegistry::setReady(true);

		const PluginFactory::PluginInfo info = PluginFactory::instance()->pluginInfo("buses_test_instrument");
		QVERIFY2(info.descriptor != nullptr, qPrintable(PluginFactory::instance()->scanReport()));
		Song* song = Engine::getSong();
		auto* withBuses = qobject_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, song));
		Instrument* loaded = withBuses->loadInstrument(QStringLiteral("buses_test_instrument"));
		QVERIFY2(loaded != nullptr && loaded->outputBuses() != nullptr, "the test instrument did not load");
		m_target = control::trackIdOf(withBuses);
		m_plain = control::trackIdOf(Track::create(Track::Type::Instrument, song));
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void cleanup()
	{
		// Every case starts from unrouted buses and the channels it found.
		InstrumentOutputBuses* buses = model();
		for (int bus = 1; bus < buses->busCount(); ++bus) { buses->route(bus, -1); }
	}

	InstrumentOutputBuses* model()
	{
		ControlResult ignored;
		return control::resolveInstrumentTarget(m_target, &ignored)->instrument()->outputBuses();
	}

	void theBusesAreListedWithTheirFields()
	{
		const ControlResult state = run(QStringLiteral("plugin.output_buses"), {{QStringLiteral("target"), m_target}});
		QVERIFY2(state.ok, qPrintable(state.errorMessage));
		QCOMPARE(state.result.value(QStringLiteral("target")).toString(), m_target);
		QCOMPARE(state.result.value(QStringLiteral("bus_count")).toInt(), 5);
		QCOMPARE(state.result.value(QStringLiteral("routed_count")).toInt(), 0);
		const QJsonObject main = busEntry(state, 0);
		QVERIFY(main.value(QStringLiteral("main")).toBool());
		QCOMPARE(main.value(QStringLiteral("name")).toString(), QStringLiteral("Main"));
		QCOMPARE(main.value(QStringLiteral("channel")).toString(), QString());  // the main bus is never routed here
		QCOMPARE(busEntry(state, 1).value(QStringLiteral("name")).toString(), QStringLiteral("Kick"));
		QVERIFY(busEntry(state, 1).value(QStringLiteral("active")).toBool());
		QVERIFY(!busEntry(state, 1).value(QStringLiteral("main")).toBool());
		QCOMPARE(busEntry(state, 1).value(QStringLiteral("channels")).toInt(), 2);
		QVERIFY(!busEntry(state, 3).value(QStringLiteral("active")).toBool());  // Aux is off until routed
	}

	void anInstrumentWithASingleOutputIsRefusedTyped()
	{
		for (const char* id : {"plugin.output_buses", "plugin.output_buses_create_channels"})
		{
			const ControlResult refused = run(QString::fromLatin1(id), {{QStringLiteral("target"), m_plain}});
			QCOMPARE(refused.errorKind, ControlErrorKind::Refused);
			QVERIFY(refused.errorMessage.contains(QStringLiteral("single output")));
		}
		QCOMPARE(run(QStringLiteral("plugin.output_bus_route"), {{QStringLiteral("target"), m_plain}, {QStringLiteral("bus"), 1}}).errorKind,
			ControlErrorKind::Refused);
		QCOMPARE(run(QStringLiteral("plugin.output_buses"), {{QStringLiteral("target"), QStringLiteral("trk-999999")}}).errorKind,
			ControlErrorKind::NotFound);
	}

	void aBusIsRoutedToAChannelUnroutedAndUndone()
	{
		Mixer* mixer = Engine::mixer();
		const int index = mixer->createChannel();
		const QString channel = channelId(index);

		const ControlResult routed = run(QStringLiteral("plugin.output_bus_route"), {{QStringLiteral("target"), m_target},
			{QStringLiteral("bus"), 1}, {QStringLiteral("channel"), channel}});
		QVERIFY2(routed.ok, qPrintable(routed.errorMessage));
		QCOMPARE(routed.result.value(QStringLiteral("routed_count")).toInt(), 1);
		QCOMPARE(busEntry(routed, 1).value(QStringLiteral("channel")).toString(), channel);
		QCOMPARE(QStringLiteral("ch-%1").arg(model()->routeOf(1)), channel);

		// The same route again is not an edit: it answers the state and records no step.
		const ControlResult again = run(QStringLiteral("plugin.output_bus_route"), {{QStringLiteral("target"), m_target},
			{QStringLiteral("bus"), 1}, {QStringLiteral("channel"), channel}});
		QVERIFY2(again.ok, qPrintable(again.errorMessage));
		QCOMPARE(again.result.value(QStringLiteral("routed_count")).toInt(), 1);

		// Un-route (empty channel): one more step; undo puts the route back, redo takes it away again.
		const ControlResult cleared = run(QStringLiteral("plugin.output_bus_route"), {{QStringLiteral("target"), m_target},
			{QStringLiteral("bus"), 1}, {QStringLiteral("channel"), QString()}});
		QVERIFY2(cleared.ok, qPrintable(cleared.errorMessage));
		QCOMPARE(cleared.result.value(QStringLiteral("routed_count")).toInt(), 0);
		QCOMPARE(model()->routeOf(1), -1);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(QStringLiteral("ch-%1").arg(model()->routeOf(1)), channel);  // the un-route was undone, the route is back
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(model()->routeOf(1), -1);  // and the first undo was the route itself: the duplicate left no step between
		QVERIFY(run(QStringLiteral("control.redo")).ok);
		QCOMPARE(QStringLiteral("ch-%1").arg(model()->routeOf(1)), channel);
	}

	void aBusThatIsOffIsActivatedWhenRoutedAndOneThePluginWillNotTurnOnIsRefused()
	{
		Mixer* mixer = Engine::mixer();
		const QString channel = channelId(mixer->createChannel());
		const ControlResult aux = run(QStringLiteral("plugin.output_bus_route"), {{QStringLiteral("target"), m_target},
			{QStringLiteral("bus"), 3}, {QStringLiteral("channel"), channel}});
		QVERIFY2(aux.ok, qPrintable(aux.errorMessage));
		QVERIFY(busEntry(aux, 3).value(QStringLiteral("active")).toBool());
		QCOMPARE(busEntry(aux, 3).value(QStringLiteral("channel")).toString(), channel);

		const ControlResult locked = run(QStringLiteral("plugin.output_bus_route"), {{QStringLiteral("target"), m_target},
			{QStringLiteral("bus"), 4}, {QStringLiteral("channel"), channel}});
		QCOMPARE(locked.errorKind, ControlErrorKind::Refused);
		QVERIFY(locked.errorMessage.contains(QStringLiteral("did not activate")));
		QCOMPARE(model()->routeOf(4), -1);
	}

	void routingRefusesWhatItCannotResolve()
	{
		Mixer* mixer = Engine::mixer();
		const QString channel = channelId(mixer->createChannel());
		const ControlResult bus = run(QStringLiteral("bus.create"));
		QVERIFY2(bus.ok, qPrintable(bus.errorMessage));

		// A bus past the list: NotFound with the range spelled out. Bus 0 (the main output) is not routable.
		const ControlResult past = run(QStringLiteral("plugin.output_bus_route"), {{QStringLiteral("target"), m_target},
			{QStringLiteral("bus"), 9}, {QStringLiteral("channel"), channel}});
		QCOMPARE(past.errorKind, ControlErrorKind::NotFound);
		QVERIFY(past.errorMessage.contains(QStringLiteral("1..4")));
		QVERIFY(!run(QStringLiteral("plugin.output_bus_route"), {{QStringLiteral("target"), m_target}, {QStringLiteral("bus"), 0},
			{QStringLiteral("channel"), channel}}).ok);

		// A parallel bus takes sends only, never an instrument's output; a channel that does not exist is NotFound.
		const ControlResult parallel = run(QStringLiteral("plugin.output_bus_route"), {{QStringLiteral("target"), m_target},
			{QStringLiteral("bus"), 1}, {QStringLiteral("channel"), bus.result.value(QStringLiteral("channel")).toString()}});
		QCOMPARE(parallel.errorKind, ControlErrorKind::Refused);
		QVERIFY(parallel.errorMessage.contains(QStringLiteral("parallel bus")));
		QCOMPARE(run(QStringLiteral("plugin.output_bus_route"), {{QStringLiteral("target"), m_target}, {QStringLiteral("bus"), 1},
			{QStringLiteral("channel"), QStringLiteral("ch-999999")}}).errorKind, ControlErrorKind::NotFound);
		QCOMPARE(model()->routedCount(), 0);
	}

	void oneStepMakesAChannelForEveryUnroutedBusAndOneUndoRemovesThem()
	{
		Mixer* mixer = Engine::mixer();
		const auto channelsBefore = mixer->numChannels();
		const ControlResult created = run(QStringLiteral("plugin.output_buses_create_channels"), {{QStringLiteral("target"), m_target}});
		QVERIFY2(created.ok, qPrintable(created.errorMessage));
		// Kick, Snare and Aux get a channel; Locked cannot be routed, so its channel is deleted again at once.
		const QJsonArray made = created.result.value(QStringLiteral("created")).toArray();
		QCOMPARE(made.size(), 3);
		QCOMPARE(made.at(0).toObject().value(QStringLiteral("bus")).toInt(), 1);
		QCOMPARE(created.result.value(QStringLiteral("routed_count")).toInt(), 3);
		QCOMPARE(mixer->numChannels(), channelsBefore + 3);
		QVERIFY(mixer->mixerChannel(mixer->numChannels() - 3)->m_name.endsWith(QStringLiteral(" Kick")));
		QCOMPARE(QStringLiteral("ch-%1").arg(model()->routeOf(1)), made.at(0).toObject().value(QStringLiteral("channel")).toString());
		QCOMPARE(model()->routeOf(4), -1);

		// Nothing left to create (Locked still refuses): no channel, no created entries, no step to undo.
		const ControlResult nothing = run(QStringLiteral("plugin.output_buses_create_channels"), {{QStringLiteral("target"), m_target}});
		QVERIFY2(nothing.ok, qPrintable(nothing.errorMessage));
		QVERIFY(nothing.result.value(QStringLiteral("created")).toArray().isEmpty());
		QCOMPARE(mixer->numChannels(), channelsBefore + 3);

		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(mixer->numChannels(), channelsBefore);
		QCOMPARE(model()->routedCount(), 0);
		QVERIFY(run(QStringLiteral("control.redo")).ok);
		QCOMPARE(mixer->numChannels(), channelsBefore + 3);
		QCOMPARE(model()->routedCount(), 3);
	}
};

QTEST_MAIN(ControlPluginBusesTest)
#include "ControlPluginBusesTest.moc"
