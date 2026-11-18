/*
 * PluginHostMetadataTest.cpp - the CLAP host's note counters and the hosted-plug-in vendor table
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

/*! Two small process-wide registries the plug-in hosts write into and the control surface reads: the
 *  CLAP note-path counters (PluginHostNoteCounters - what the last loaded plug-in declared and how many
 *  note events were pushed, dropped, delivered and played) and the vendor table
 *  (pluginvendors::remember / vendorOf, filled while a listing reads each module's factory). */

#include <QtTest>

#include <thread>
#include <vector>

#include "PluginHostNotes.h"
#include "PluginVendors.h"

using namespace lmms;

class PluginHostMetadataTest : public QObject
{
	Q_OBJECT

private slots:
	void aFreshCounterSetReadsZero()
	{
		control::PluginHostNoteCounters counters;
		const control::PluginHostNoteStats stats = counters.read();
		QCOMPARE(stats.ports, 0u);
		QCOMPARE(stats.loads, std::uint64_t{0});
		QCOMPARE(stats.pushed, std::uint64_t{0});
		QCOMPARE(stats.dropped, std::uint64_t{0});
		QCOMPARE(stats.delivered, std::uint64_t{0});
		QCOMPARE(stats.played, std::uint64_t{0});
	}

	void aLoadIsRecordedAsTheLastPluginsLayout()
	{
		control::PluginHostNoteCounters counters;
		counters.recordLoad(2, 1, 0x5, 0, 2);
		control::PluginHostNoteStats stats = counters.read();
		QCOMPARE(stats.ports, 2u);
		QCOMPARE(stats.preferredPort, 1u);
		QCOMPARE(stats.dialects, 5u);
		QCOMPARE(stats.audioInputs, 0u);
		QCOMPARE(stats.audioOutputs, 2u);
		QCOMPARE(stats.loads, std::uint64_t{1});

		// The next load replaces the layout (a per-instance fact) and adds to the process-lifetime load count.
		counters.recordLoad(1, 0, 0x1, 2, 2);
		stats = counters.read();
		QCOMPARE(stats.ports, 1u);
		QCOMPARE(stats.preferredPort, 0u);
		QCOMPARE(stats.audioInputs, 2u);
		QCOMPARE(stats.loads, std::uint64_t{2});
	}

	void theEventCountersAccumulate()
	{
		control::PluginHostNoteCounters counters;
		for (int i = 0; i < 5; ++i) { counters.recordPush(); }
		counters.recordDrop();
		counters.recordDrop();
		counters.recordDelivered(4, 3);
		counters.recordDelivered(2, 1);
		const control::PluginHostNoteStats stats = counters.read();
		QCOMPARE(stats.pushed, std::uint64_t{5});
		QCOMPARE(stats.dropped, std::uint64_t{2});
		QCOMPARE(stats.delivered, std::uint64_t{6});
		QCOMPARE(stats.played, std::uint64_t{4});
		// The loads and the layout are untouched by event traffic.
		QCOMPARE(stats.loads, std::uint64_t{0});
	}

	//! The audio thread writes with relaxed atomics: no update is lost across threads.
	void concurrentWritersLoseNothing()
	{
		control::PluginHostNoteCounters counters;
		std::vector<std::thread> writers;
		for (int t = 0; t < 4; ++t)
		{
			writers.emplace_back([&counters] {
				for (int i = 0; i < 1000; ++i) { counters.recordPush(); }
			});
		}
		for (std::thread& writer : writers) { writer.join(); }
		QCOMPARE(counters.read().pushed, std::uint64_t{4000});
	}

	void theProcessWideSetIsOneObject()
	{
		control::PluginHostNoteCounters& counters = control::clapHostNoteCounters();
		QVERIFY(&counters == &control::clapHostNoteCounters());
		const std::uint64_t before = counters.read().pushed;
		counters.recordPush();
		QCOMPARE(control::clapHostNoteCounters().read().pushed, before + 1);
	}

	void aRememberedVendorIsFoundByModuleAndId()
	{
		pluginvendors::remember(QStringLiteral("vst3"), QStringLiteral("Gain"), QStringLiteral("  Acme Audio  "));
		QCOMPARE(pluginvendors::vendorOf(QStringLiteral("vst3"), QStringLiteral("Gain")), QStringLiteral("Acme Audio"));
		// The same id under another module is a different class.
		QCOMPARE(pluginvendors::vendorOf(QStringLiteral("clap"), QStringLiteral("Gain")), QString());
		QCOMPARE(pluginvendors::vendorOf(QStringLiteral("vst3"), QStringLiteral("Unknown")), QString());
		// Listing again replaces the entry.
		pluginvendors::remember(QStringLiteral("vst3"), QStringLiteral("Gain"), QStringLiteral("Other Co"));
		QCOMPARE(pluginvendors::vendorOf(QStringLiteral("vst3"), QStringLiteral("Gain")), QStringLiteral("Other Co"));
	}
};

QTEST_GUILESS_MAIN(PluginHostMetadataTest)
#include "PluginHostMetadataTest.moc"
