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
	void NodeTopologyEdgeCases();
	void LoadMalformedDocuments();
	void SinkNodeSumsInputs();
	void BaseNodeSettingsDefaults();
	void ClearAndMoveSemantics();
	void ProcessClampsToPreparedSize();
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

void RoutingGraphTest::NodeTopologyEdgeCases()
{
	RoutingGraph graph;

	// a null node is rejected
	QCOMPARE(graph.addNode(nullptr), -1);

	// out-of-range ids resolve to nullptr nodes
	QVERIFY(graph.node(-1) == nullptr);
	QVERIFY(graph.node(0) == nullptr);
	QVERIFY(graph.node(42) == nullptr);

	// the const overload behaves identically for edge cases: negative and
	// out-of-range ids return nullptr there too
	const RoutingGraph& view = graph;
	QVERIFY(view.node(-1) == nullptr);
	QVERIFY(view.node(42) == nullptr);

	// removing a node that was never added fails
	QVERIFY(!graph.removeNode(0));

	// setting the output node to an unknown id fails
	QVERIFY(!graph.setOutputNode(3));
	QCOMPARE(graph.outputNodeId(), -1);

	// add/remove/add reuses the id of the removed node
	const int first = graph.addNode(std::make_unique<GainNode>(1.f));
	QVERIFY(first >= 0);
	QCOMPARE(graph.nodeCount(), 1);
	QVERIFY(graph.removeNode(first));
	QCOMPARE(graph.nodeCount(), 0);
	const int reused = graph.addNode(std::make_unique<GainNode>(0.5f));
	QCOMPARE(reused, first);
	const auto* reusedGain = dynamic_cast<GainNode*>(graph.node(first));
	QVERIFY(reusedGain != nullptr);
	QCOMPARE(reusedGain->gain(), 0.5f);

	// the const overload also resolves a valid id to the same node
	QVERIFY(view.node(first) != nullptr);
	QCOMPARE(view.node(first)->typeName(), QStringLiteral("gain"));

	// disconnect() of an existing and of a non-existent connection
	const int other = graph.addNode(std::make_unique<GainNode>(2.f));
	QVERIFY(other >= 0);
	QVERIFY(graph.connect(first, other));
	QCOMPARE(graph.connections().size(), std::size_t(1));
	QVERIFY(graph.disconnect(first, other));
	QVERIFY(!graph.disconnect(first, other));
	QVERIFY(graph.connections().empty());

	// disconnect() with the wrong ports does not remove the connection
	QVERIFY(graph.connect(first, other, 0, 0));
	QVERIFY(!graph.disconnect(first, other, 0, 1));
	QCOMPARE(graph.connections().size(), std::size_t(1));

	// connects that reference non-existent nodes and ports fail
	QString error;
	QVERIFY(!graph.connect(-1, other, 0, 0, &error));
	QCOMPARE(error, QStringLiteral("node does not exist"));
	error.clear();
	QVERIFY(!graph.connect(first, 99, 0, 0, &error));
	QCOMPARE(error, QStringLiteral("node does not exist"));
	error.clear();
	QVERIFY(!graph.connect(first, other, 5, 0, &error));
	QCOMPARE(error, QStringLiteral("source port out of range"));
	error.clear();
	QVERIFY(!graph.connect(first, other, 0, 7, &error));
	QCOMPARE(error, QStringLiteral("destination port out of range"));
	error.clear();
	QVERIFY(!graph.connect(first, other, 0, 0, &error));
	QCOMPARE(error, QStringLiteral("connection already exists"));

	// negative ports are rejected as well — a negative source port must not be
	// mistaken for a valid one (checked on a fresh pair so no duplicate exists)
	const int third = graph.addNode(std::make_unique<GainNode>(3.f));
	QVERIFY(third >= 0);
	error.clear();
	QVERIFY(!graph.connect(first, third, -1, 0, &error));
	QCOMPARE(error, QStringLiteral("source port out of range"));
	error.clear();
	QVERIFY(!graph.connect(first, third, 0, -1, &error));
	QCOMPARE(error, QStringLiteral("destination port out of range"));
	QCOMPARE(graph.connections().size(), std::size_t(1));

	// the output node is remembered and cleared with the node again
	QVERIFY(graph.setOutputNode(other));
	QCOMPARE(graph.outputNodeId(), other);
	QVERIFY(graph.removeNode(other));
	QCOMPARE(graph.outputNodeId(), -1);
	// ...and every connection that referenced it is dropped with it
	QVERIFY(graph.connections().empty());
}

void RoutingGraphTest::LoadMalformedDocuments()
{
	// an XML document with a routinggraph element whose nodes and
	// connections do not form a valid graph must be rejected without
	// changing the target graph
	const QByteArray documents[] = {
		// a connection referencing an unknown file id
		QByteArrayLiteral(
			"<test><routinggraph version=\"1\" frames=\"0\" channels=\"0\" output=\"-1\">"
			"<node id=\"0\" type=\"gain\"/><node id=\"1\" type=\"gain\"/>"
			"<connection from=\"0\" fromport=\"0\" to=\"7\" toport=\"0\"/>"
			"</routinggraph></test>"),
		// the output node id does not exist
		QByteArrayLiteral(
			"<test><routinggraph version=\"1\" frames=\"0\" channels=\"0\" output=\"9\">"
			"<node id=\"0\" type=\"gain\"/>"
			"</routinggraph></test>"),
		// a connection whose source port does not exist on the source node
		// (GainNode has a single output port; fromport=4 is out of range)
		QByteArrayLiteral(
			"<test><routinggraph version=\"1\" frames=\"0\" channels=\"0\" output=\"-1\">"
			"<node id=\"0\" type=\"gain\"/><node id=\"1\" type=\"gain\"/>"
			"<connection from=\"0\" fromport=\"4\" to=\"1\" toport=\"0\"/>"
			"</routinggraph></test>"),
	};

	for (const QByteArray& xml : documents)
	{
		QDomDocument document;
		QVERIFY(document.setContent(xml));
		RoutingGraph graph;
		QVERIFY(!graph.load(document.documentElement()));
		QCOMPARE(graph.nodeCount(), 0);
		QVERIFY(graph.connections().empty());
		QCOMPARE(graph.outputNodeId(), -1);
	}
}

void RoutingGraphTest::SinkNodeSumsInputs()
{
	constexpr f_cnt_t Frames = 32;

	RoutingGraph graph;
	const int left = graph.addNode(std::make_unique<ConstantSourceNode>(0.5f));
	const int right = graph.addNode(std::make_unique<ConstantSourceNode>(0.25f));
	const int sink = graph.addNode(std::make_unique<SinkNode>());
	QVERIFY(left >= 0);
	QVERIFY(right >= 0);
	QVERIFY(sink >= 0);

	// both sources sum into the sink; the sink is the graph output
	QVERIFY(graph.connect(left, sink));
	QVERIFY(graph.connect(right, sink));
	QVERIFY(graph.setOutputNode(sink));

	// three independent nodes keep their ascending ids in the plan
	const std::vector<int>& order = graph.processingOrder();
	QCOMPARE(order.size(), std::size_t(3));
	QCOMPARE(order[0], left);
	QCOMPARE(order[1], right);
	QCOMPARE(order[2], sink);

	graph.prepare(Frames, 1);
	AudioBuffer channel(Frames, 1);
	graph.process(channel);

	// the sink output is the sum of both constants
	QCOMPARE(channel.buffer(0)[0], 0.75f);
	QCOMPARE(channel.buffer(0)[Frames - 1], 0.75f);
}

void RoutingGraphTest::BaseNodeSettingsDefaults()
{
	constexpr f_cnt_t Frames = 32;

	// SinkNode intentionally does not override the optional saveSettings()/
	// loadSettings() hooks, so a round trip through RoutingGraph must execute
	// the RoutingNode base-class defaults for it.
	RoutingGraph original;
	const int sourceId = original.addNode(std::make_unique<ConstantSourceNode>(0.5f));
	const int sinkId = original.addNode(std::make_unique<SinkNode>());
	QVERIFY(original.connect(sourceId, sinkId));
	QVERIFY(original.setOutputNode(sinkId));

	DataFile dataFile(DataFile::Type::InstrumentTrackSettings);
	original.save(dataFile.content());

	RoutingGraph restored;
	QVERIFY(restored.load(dataFile.content()));
	QCOMPARE(restored.nodeCount(), 2);
	QCOMPARE(restored.node(1)->typeName(), QStringLiteral("sink"));
	QVERIFY(dynamic_cast<SinkNode*>(restored.node(1)) != nullptr);
	QCOMPARE(restored.outputNodeId(), sinkId);

	// the base default loadSettings() left the sink fully functional
	restored.prepare(Frames, 1);
	AudioBuffer channel(Frames, 1);
	restored.process(channel);
	QCOMPARE(channel.buffer(0)[0], 0.5f);
	QCOMPARE(channel.buffer(0)[Frames - 1], 0.5f);
}

void RoutingGraphTest::ClearAndMoveSemantics()
{
	constexpr f_cnt_t Frames = 16;

	RoutingGraph original;
	const int sourceId = original.addNode(std::make_unique<ConstantSourceNode>(0.25f));
	const int sinkId = original.addNode(std::make_unique<SinkNode>());
	QVERIFY(sourceId >= 0);
	QVERIFY(sinkId >= 0);
	QVERIFY(original.connect(sourceId, sinkId));
	QVERIFY(original.setOutputNode(sinkId));
	original.prepare(Frames, 1);

	// move construction preserves the whole graph state
	RoutingGraph moved{std::move(original)};
	QCOMPARE(moved.nodeCount(), 2);
	QCOMPARE(moved.connections().size(), std::size_t(1));
	QCOMPARE(moved.outputNodeId(), sinkId);
	QVERIFY(moved.isPrepared());
	QCOMPARE(moved.frames(), Frames);
	QCOMPARE(moved.channels(), ch_cnt_t(1));
	QVERIFY(moved.processingOrder().size() == std::size_t(2));

	AudioBuffer channel(Frames, 1);
	moved.process(channel);
	QCOMPARE(channel.buffer(0)[0], 0.25f);

	// clear() empties everything
	moved.clear();
	QCOMPARE(moved.nodeCount(), 0);
	QVERIFY(moved.connections().empty());
	QVERIFY(moved.processingOrder().empty());
	QCOMPARE(moved.outputNodeId(), -1);

	// a cleared graph does not process
	moved.process(channel);
	QCOMPARE(channel.buffer(0)[0], 0.25f);
}

//! process() must write only the prepared frame/channel window. The caller's
//! buffer may legitimately be larger than the graph; everything outside the
//! prepared window must stay untouched.
void RoutingGraphTest::ProcessClampsToPreparedSize()
{
	constexpr f_cnt_t PreparedFrames = 16;
	constexpr f_cnt_t BufferFrames = 32;
	constexpr ch_cnt_t BufferChannels = 2;
	constexpr float Untouched = -123.0f;

	RoutingGraph graph;
	const int sourceId = graph.addNode(std::make_unique<ConstantSourceNode>(0.5f));
	const int gainId = graph.addNode(std::make_unique<GainNode>(1.0f));
	QVERIFY(graph.connect(sourceId, gainId));
	QVERIFY(graph.setOutputNode(gainId));
	graph.prepare(PreparedFrames, 1);
	QCOMPARE(graph.frames(), PreparedFrames);
	QCOMPARE(graph.channels(), ch_cnt_t(1));

	AudioBuffer channel(BufferFrames, BufferChannels);
	for (ch_cnt_t c = 0; c < BufferChannels; ++c)
	{
		for (f_cnt_t f = 0; f < BufferFrames; ++f)
		{
			channel.buffer(c)[f] = Untouched;
		}
	}

	graph.process(channel);

	// the prepared window carries the signal...
	for (f_cnt_t f = 0; f < PreparedFrames; ++f)
	{
		QCOMPARE(channel.buffer(0)[f], 0.5f);
	}
	// ...and neither the extra frames nor the extra channel are written to
	for (f_cnt_t f = PreparedFrames; f < BufferFrames; ++f)
	{
		QCOMPARE(channel.buffer(0)[f], Untouched);
	}
	for (f_cnt_t f = 0; f < BufferFrames; ++f)
	{
		QCOMPARE(channel.buffer(1)[f], Untouched);
	}
}

QTEST_GUILESS_MAIN(RoutingGraphTest)
#include "RoutingGraphTest.moc"
