/*
 * RoutingGraphTest.cpp
 *
 * Copyright (c) 2026 Zachariah Markusson <zachariahmarkusson@gmail.com>
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

#include "RoutingGraph.h"
#include "RoutingNodes.h"

#include <QtTest>

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <new>

#include <QDomDocument>
#include <QTemporaryDir>

#include "DataFile.h"

using namespace lmms;

namespace
{

//! Global allocation counter, so ProcessingDoesNotAllocate() can prove the
//! audio-thread path allocates nothing at all.
std::atomic<long long> s_allocationCount{0};

} // namespace

void* operator new(std::size_t size)
{
	s_allocationCount.fetch_add(1, std::memory_order_relaxed);
	if (void* ptr = std::malloc(size)) { return ptr; }
	throw std::bad_alloc();
}

void* operator new[](std::size_t size)
{
	return ::operator new(size);
}

void operator delete(void* ptr) noexcept
{
	std::free(ptr);
}

void operator delete[](void* ptr) noexcept
{
	std::free(ptr);
}

void operator delete(void* ptr, std::size_t) noexcept
{
	std::free(ptr);
}

void operator delete[](void* ptr, std::size_t) noexcept
{
	std::free(ptr);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
	s_allocationCount.fetch_add(1, std::memory_order_relaxed);
	return std::malloc(size);
}

void* operator new[](std::size_t size, const std::nothrow_t& tag) noexcept
{
	return ::operator new(size, tag);
}

void operator delete(void* ptr, const std::nothrow_t&) noexcept
{
	std::free(ptr);
}

void operator delete[](void* ptr, const std::nothrow_t&) noexcept
{
	std::free(ptr);
}

class RoutingGraphTest : public QObject
{
	Q_OBJECT

private slots:
	void ProcessesKnownBlock_data();
	void ProcessesKnownBlock();
	void ContinuesAcrossBlocks();
	void RejectsCycle();
	void RoundTripsSaveLoad();
	void ProcessingDoesNotAllocate();
	void RejectsUnknownNodeType();
};

void RoutingGraphTest::ProcessesKnownBlock_data()
{
	QTest::addColumn<float>("source");
	QTest::addColumn<float>("alpha");
	QTest::addColumn<float>("gain");

	QTest::newRow("dc-1.0-alpha-0.5-gain-0.25") << 1.0f << 0.5f << 0.25f;
	QTest::newRow("dc-0.5-alpha-0.3-gain-2.0") << 0.5f << 0.3f << 2.0f;
	QTest::newRow("dc-0.25-alpha-0.9-gain-0.5") << 0.25f << 0.9f << 0.5f;
}

//! source -> onepole low-pass -> gain, evaluated into one 48-frame channel
void RoutingGraphTest::ProcessesKnownBlock()
{
	QFETCH(float, source);
	QFETCH(float, alpha);
	QFETCH(float, gain);

	RoutingGraph graph;
	const int sourceId = graph.addNode(std::make_unique<ConstantSourceNode>(source));
	const int filterId = graph.addNode(std::make_unique<OnePoleLowPassNode>(alpha));
	const int gainId = graph.addNode(std::make_unique<GainNode>(gain));

	QVERIFY(sourceId >= 0);
	QVERIFY(filterId >= 0);
	QVERIFY(gainId >= 0);
	QVERIFY(graph.connect(sourceId, filterId));
	QVERIFY(graph.connect(filterId, gainId));
	QVERIFY(graph.setOutputNode(gainId));

	const std::vector<int>& order = graph.processingOrder();
	QCOMPARE(order.size(), std::size_t(3));
	QCOMPARE(order[0], sourceId);
	QCOMPARE(order[1], filterId);
	QCOMPARE(order[2], gainId);

	constexpr f_cnt_t Frames = 48;
	graph.prepare(Frames, 2);
	QVERIFY(graph.isPrepared());

	// Stands in for one instrument channel's AudioBuffer
	AudioBuffer channel(Frames, 2);
	graph.process(channel);

	double expected = 0.0;
	for (f_cnt_t f = 0; f < Frames; ++f)
	{
		expected += static_cast<double>(alpha) * (static_cast<double>(source) - expected);
		const double value = expected * static_cast<double>(gain);
		for (ch_cnt_t c = 0; c < 2; ++c)
		{
			const double actual = channel.buffer(c)[f];
			if (std::abs(actual - value) > 1e-5)
			{
				QFAIL(qPrintable(QStringLiteral("frame %1 channel %2: %3 != %4")
					.arg(f).arg(c).arg(actual).arg(value)));
			}
		}
	}
}

//! The filter state must carry over between successive blocks
void RoutingGraphTest::ContinuesAcrossBlocks()
{
	constexpr f_cnt_t Frames = 48;
	constexpr float Source = 1.0f;
	constexpr float Alpha = 0.5f;
	constexpr float Gain = 0.25f;

	RoutingGraph graph;
	const int sourceId = graph.addNode(std::make_unique<ConstantSourceNode>(Source));
	const int filterId = graph.addNode(std::make_unique<OnePoleLowPassNode>(Alpha));
	const int gainId = graph.addNode(std::make_unique<GainNode>(Gain));
	QVERIFY(graph.connect(sourceId, filterId));
	QVERIFY(graph.connect(filterId, gainId));
	QVERIFY(graph.setOutputNode(gainId));
	graph.prepare(Frames, 1);

	AudioBuffer channel(Frames, 1);
	double expected = 0.0;
	for (int block = 0; block < 2; ++block)
	{
		graph.process(channel);
		for (f_cnt_t f = 0; f < Frames; ++f)
		{
			expected += static_cast<double>(Alpha) * (static_cast<double>(Source) - expected);
			const double value = expected * static_cast<double>(Gain);
			const double actual = channel.buffer(0)[f];
			if (std::abs(actual - value) > 1e-5)
			{
				QFAIL(qPrintable(QStringLiteral("block %1 frame %2: %3 != %4")
					.arg(block).arg(f).arg(actual).arg(value)));
			}
		}
	}
}

void RoutingGraphTest::RejectsCycle()
{
	RoutingGraph graph;
	const int a = graph.addNode(std::make_unique<GainNode>());
	const int b = graph.addNode(std::make_unique<GainNode>());
	const int c = graph.addNode(std::make_unique<GainNode>());

	QVERIFY(graph.connect(a, b));
	QVERIFY(graph.connect(b, c));
	QCOMPARE(graph.connections().size(), std::size_t(2));
	QCOMPARE(graph.processingOrder().size(), std::size_t(3));

	// c -> a would close a three-node cycle
	QString error;
	QVERIFY(!graph.connect(c, a, 0, 0, &error));
	QVERIFY(!error.isEmpty());
	QCOMPARE(graph.connections().size(), std::size_t(2));
	QCOMPARE(graph.processingOrder().size(), std::size_t(3));

	// self connection is rejected too
	error.clear();
	QVERIFY(!graph.connect(a, a, 0, 0, &error));
	QVERIFY(!error.isEmpty());

	// two-node cycle
	QVERIFY(!graph.connect(c, b, 0, 0));
	QCOMPARE(graph.connections().size(), std::size_t(2));

	// duplicate connection is rejected as well
	QVERIFY(!graph.connect(a, b, 0, 0));

	// the graph is still usable after rejected edits
	graph.prepare(4, 1);
	AudioBuffer channel(4, 1);
	graph.process(channel);
	QCOMPARE(graph.processingOrder().size(), std::size_t(3));
}

void RoutingGraphTest::RoundTripsSaveLoad()
{
	constexpr f_cnt_t Frames = 48;

	RoutingGraph original;
	const int sourceId = original.addNode(std::make_unique<ConstantSourceNode>(0.75f));
	const int filterId = original.addNode(std::make_unique<OnePoleLowPassNode>(0.25f));
	const int gainId = original.addNode(std::make_unique<GainNode>(0.5f));
	QVERIFY(original.connect(sourceId, filterId));
	QVERIFY(original.connect(filterId, gainId));
	QVERIFY(original.setOutputNode(gainId));
	original.prepare(Frames, 2);

	AudioBuffer first(Frames, 2);
	original.process(first);

	DataFile dataFile(DataFile::Type::InstrumentTrackSettings);
	original.save(dataFile.content());

	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	const QString path = directory.filePath(QStringLiteral("patch.xpf"));
	QVERIFY(dataFile.writeFile(path));

	DataFile loaded(path);
	RoutingGraph restored;
	QVERIFY(restored.load(loaded.content()));

	QCOMPARE(restored.nodeCount(), 3);
	QCOMPARE(restored.connections().size(), std::size_t(2));
	QVERIFY(restored.outputNodeId() >= 0);
	QCOMPARE(restored.node(restored.outputNodeId())->typeName(), QStringLiteral("gain"));

	// parameters survive the round-trip
	auto* restoredSource = dynamic_cast<ConstantSourceNode*>(restored.node(0));
	auto* restoredFilter = dynamic_cast<OnePoleLowPassNode*>(restored.node(1));
	auto* restoredGain = dynamic_cast<GainNode*>(restored.node(restored.outputNodeId()));
	QVERIFY(restoredSource != nullptr);
	QVERIFY(restoredFilter != nullptr);
	QVERIFY(restoredGain != nullptr);
	QCOMPARE(restoredSource->value(), 0.75f);
	QCOMPARE(restoredFilter->alpha(), 0.25f);
	QCOMPARE(restoredGain->gain(), 0.5f);

	// and the restored graph produces bit-identical output
	restored.prepare(Frames, 2);
	AudioBuffer second(Frames, 2);
	restored.process(second);

	for (ch_cnt_t c = 0; c < 2; ++c)
	{
		QVERIFY(std::memcmp(first.buffer(c).data(), second.buffer(c).data(),
			Frames * sizeof(float)) == 0);
	}
}

void RoutingGraphTest::ProcessingDoesNotAllocate()
{
	constexpr f_cnt_t Frames = 48;

	RoutingGraph graph;
	const int sourceId = graph.addNode(std::make_unique<ConstantSourceNode>(0.5f));
	const int filterId = graph.addNode(std::make_unique<OnePoleLowPassNode>(0.5f));
	const int gainId = graph.addNode(std::make_unique<GainNode>(0.5f));
	QVERIFY(graph.connect(sourceId, filterId));
	QVERIFY(graph.connect(filterId, gainId));
	QVERIFY(graph.setOutputNode(gainId));
	graph.prepare(Frames, 2);

	AudioBuffer channel(Frames, 2);
	graph.process(channel); // warm up, first call may touch lazy internals

	const long long before = s_allocationCount.load(std::memory_order_relaxed);
	graph.process(channel);
	const long long after = s_allocationCount.load(std::memory_order_relaxed);
	QCOMPARE(after - before, 0LL);
}

void RoutingGraphTest::RejectsUnknownNodeType()
{
	QDomDocument document;
	QDomElement root = document.createElement(QStringLiteral("test"));
	document.appendChild(root);
	QDomElement graphElement = document.createElement(QStringLiteral("routinggraph"));
	QDomElement nodeElement = document.createElement(QStringLiteral("node"));
	nodeElement.setAttribute(QStringLiteral("id"), 0);
	nodeElement.setAttribute(QStringLiteral("type"), QStringLiteral("not_a_node"));
	graphElement.appendChild(nodeElement);
	root.appendChild(graphElement);

	RoutingGraph graph;
	QVERIFY(!graph.load(root));
	QCOMPARE(graph.nodeCount(), 0);

	// a missing routinggraph element is not a crash either
	QDomElement emptyRoot = document.createElement(QStringLiteral("empty"));
	document.appendChild(emptyRoot);
	QVERIFY(!graph.load(emptyRoot));
}

QTEST_GUILESS_MAIN(RoutingGraphTest)
#include "RoutingGraphTest.moc"
