/*
 * RoutingGraphLiveTest.cpp - RoutingGraph on a mixer channel's effect chain
 *
 * Copyright (c) 2026 LMMS developers
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

//! Proves that the mixer channel's effect chain is processed through a
//! RoutingGraph, and pins both halves of that claim:
//!
//!   * equivalence - the render of a channel whose chain goes through the graph
//!     is byte-identical to the render captured before the graph was put on the
//!     path (tests/reference/routing-graph-live-render.raw);
//!   * liveness - re-directing a connection in that same graph changes the
//!     render, in exactly the way the re-directed wiring says it should.
//!
//! Evidence is printed unconditionally (stdout, flushed) so the SHA-256 of every
//! render can be pasted into docs/ROUTING-GRAPH-LIVE.md:
//!
//!   LMMS_ROUTING_GRAPH_RENDER_OUT=/tmp/linear.raw ./RoutingGraphLiveTest

#include <QtTest>

#include <QByteArray>
#include <QCryptographicHash>
#include <QFile>
#include <QString>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "AllocationProbe.h"
#include "AudioBuffer.h"
#include "AudioBus.h"
#include "AudioDevice.h"
#include "AudioEngine.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "Mixer.h"
#include "Plugin.h"
#include "RoutingChainNodes.h"
#include "RoutingGraph.h"
#include "SampleFrame.h"

#ifndef ROUTING_GRAPH_LIVE_REFERENCE_FILE
#define ROUTING_GRAPH_LIVE_REFERENCE_FILE "tests/reference/routing-graph-live-render.raw"
#endif

namespace lmms
{

namespace
{

//! Deterministic per-channel gain (L *= left, R *= right). Exercises the chain's
//! DSP without depending on any plugin being loadable in a headless test.
class LiveScaleEffect : public Effect
{
public:
	LiveScaleEffect(Model* parent, float left, float right) :
		Effect{&s_descriptor, parent, nullptr},
		m_left(left),
		m_right(right)
	{
	}

	EffectControls* controls() override { return nullptr; }

protected:
	ProcessStatus processImpl(SampleFrame* buf, const f_cnt_t frames) override
	{
		for (f_cnt_t f = 0; f < frames; ++f)
		{
			buf[f][0] *= m_left;
			buf[f][1] *= m_right;
		}
		return ProcessStatus::Continue;
	}

private:
	float m_left;
	float m_right;

	static const Plugin::Descriptor s_descriptor;
};

const Plugin::Descriptor LiveScaleEffect::s_descriptor
{
	"livescaletest",
	"Live routing-graph scale effect",
	"Deterministic per-channel gain used by RoutingGraphLiveTest",
	"LMMS",
	0x0100,
	Plugin::Type::Effect,
	nullptr,
	nullptr,
	nullptr
};

//! 16 * 256 frames = 4096 frames = 32768 bytes of output.
constexpr int kPeriods = 16;
//! Master + the one channel that carries the chain.
constexpr int kChannels = 2;
//! The channel under test.
constexpr int kChannel = 1;

//! Gains of the two effects in the chain, in chain order.
constexpr float kFirstLeft = 2.0f;
constexpr float kFirstRight = 2.0f;
constexpr float kSecondLeft = 0.5f;
constexpr float kSecondRight = 0.25f;

//! Deterministic signal in [-0.5, 0.5). Integer arithmetic only (no libm), so
//! the exact bit pattern is platform-independent.
inline float liveSignal(int period, int frame, int side)
{
	const int n = period * 7919 + frame * 131 + side * 4093;
	return static_cast<float>((n % 2048) - 1024) * (1.0f / 2048.0f);
}

QByteArray sha256Hex(const QByteArray& data)
{
	return QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
}

void printEvidence(const char* label, const QByteArray& data)
{
	std::fprintf(stdout, "ROUTING_GRAPH_EVIDENCE %s bytes=%d sha256=%s\n",
		label, static_cast<int>(data.size()), sha256Hex(data).constData());
	std::fflush(stdout);
}

//! True when every sample of @a scaled is @a base scaled by @a factor. The
//! re-directed render is expected to differ from the linear one by exactly the
//! work of the effect the re-wiring took out of the path, which is a stronger
//! claim than "the bytes differ".
bool isScaledBy(const QByteArray& scaled, const QByteArray& base, float factor)
{
	if (scaled.size() != base.size() || scaled.isEmpty()) { return false; }

	const auto* scaledSamples = reinterpret_cast<const float*>(scaled.constData());
	const auto* baseSamples = reinterpret_cast<const float*>(base.constData());
	const int count = scaled.size() / static_cast<int>(sizeof(float));
	for (int i = 0; i < count; ++i)
	{
		if (scaledSamples[i] != baseSamples[i] * factor) { return false; }
	}
	return true;
}

//! The graph's source node: the node no connection feeds.
int inputNodeId(const RoutingGraph& graph)
{
	for (const RoutingConnection& connection : graph.connections())
	{
		bool fed = false;
		for (const RoutingConnection& other : graph.connections())
		{
			if (other.destNode == connection.sourceNode) { fed = true; break; }
		}
		if (!fed) { return connection.sourceNode; }
	}
	return -1;
}

} // namespace

} // namespace lmms

using namespace lmms;

class RoutingGraphLiveTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		// The dummy device thread renders in the background; this test drives
		// the mixer synchronously, so stop it to keep the buffers stable.
		Engine::audioEngine()->audioDev()->stopProcessing();
		QCOMPARE(Engine::audioEngine()->framesPerPeriod(), static_cast<f_cnt_t>(256));

		buildGraph();
	}

	void cleanupTestCase()
	{
		Engine::destroy();
	}

	//! The render must be reproducible, and must match the reference captured
	//! before the chain was routed through the graph.
	void rendersLikeTheCommittedReference()
	{
		// The equivalence claim is only worth anything if the graph is on the
		// path while the byte-identical render is produced.
		QVERIFY2(chainUnderTest()->routesThroughGraph(),
			"the chain is not routed through the graph - the equivalence proof would be vacuous");

		const QByteArray first = render();
		const QByteArray second = render();

		QVERIFY2(first.size() == kPeriods * 256 * static_cast<int>(sizeof(SampleFrame)),
			"unexpected render size");
		QVERIFY2(first != QByteArray(first.size(), '\0'),
			"render is silent - the chain did not produce any audio");
		QCOMPARE(second, first);

		printEvidence("render", first);

		const QString outPath = qEnvironmentVariable("LMMS_ROUTING_GRAPH_RENDER_OUT");
		if (!outPath.isEmpty())
		{
			QFile out{outPath};
			QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
			QCOMPARE(out.write(first), static_cast<qint64>(first.size()));
			out.close();
		}

		const QString refPath = QString::fromUtf8(ROUTING_GRAPH_LIVE_REFERENCE_FILE);
		if (!QFile::exists(refPath))
		{
			// Bootstrap mode: the reference is captured and committed before the
			// chain is routed through the graph. Never reached once it is in the
			// tree.
			std::fprintf(stdout,
				"ROUTING_GRAPH_EVIDENCE reference MISSING at %s (bootstrap capture)\n",
				refPath.toUtf8().constData());
			std::fflush(stdout);
			QWARN("reference render missing - bootstrap capture mode");
			return;
		}

		QFile ref{refPath};
		QVERIFY(ref.open(QIODevice::ReadOnly));
		const QByteArray expected = ref.readAll();
		ref.close();

		printEvidence("reference", expected);

		QCOMPARE(first.size(), expected.size());
		QVERIFY2(first == expected,
			"render is NOT byte-identical to the committed reference");
	}

	//! The graph is built from the chain's own effects, in chain order, with the
	//! connections the plain loop used to walk.
	void chainIsRoutedThroughTheGraph()
	{
		auto* chain = chainUnderTest();
		QVERIFY(chain->routesThroughGraph());

		const RoutingGraph& graph = chain->routingGraph();
		QVERIFY(graph.isPrepared());
		QCOMPARE(graph.frames(), Engine::audioEngine()->framesPerPeriod());
		// One source node plus one node per effect: input -> effect 0 -> effect 1
		QCOMPARE(graph.nodeCount(), 3);
		QCOMPARE(graph.outputNodeId(), 2);

		const auto& connections = graph.connections();
		QCOMPARE(connections.size(), std::size_t{2});
		QCOMPARE(connections[0].sourceNode, 0);
		QCOMPARE(connections[0].destNode, 1);
		QCOMPARE(connections[1].sourceNode, 1);
		QCOMPARE(connections[1].destNode, 2);
		QCOMPARE(inputNodeId(graph), 0);
	}

	//! A chain with nothing in it has nothing to route, so it keeps the plain
	//! loop: the graph is not on the path of a project that configures none.
	void chainWithoutEffectsIsNotRouted()
	{
		EffectChain empty{nullptr};
		QVERIFY(!empty.routesThroughGraph());
		QCOMPARE(empty.routingGraph().nodeCount(), 0);
		QVERIFY(!empty.routingGraph().isPrepared());
	}

	//! Liveness: re-directing a connection changes the render, and changes it by
	//! exactly the work the re-wiring takes out of the path. The node set is
	//! untouched, so nothing but the wiring can be responsible.
	void redirectingAConnectionChangesTheRender()
	{
		auto* chain = chainUnderTest();
		auto& graph = chain->routingGraph();
		QVERIFY(chain->routesThroughGraph());

		const QByteArray linear = render();
		printEvidence("render-linear", linear);

		const int sourceId = inputNodeId(graph);
		const int firstEffectId = 1;
		const int secondEffectId = 2;
		QCOMPARE(sourceId, 0);
		QCOMPARE(graph.outputNodeId(), secondEffectId);

		// Re-direct the chain's input straight to the second effect, bypassing
		// the first one. Same nodes, same output node, different wiring.
		QVERIFY(graph.disconnect(sourceId, firstEffectId, 0, 0));
		QVERIFY(graph.connect(sourceId, secondEffectId, 0, 0));

		QVERIFY(chain->routesThroughGraph());
		QCOMPARE(graph.nodeCount(), 3);

		const QByteArray redirected = render();
		printEvidence("render-redirected", redirected);

		QCOMPARE(redirected.size(), linear.size());
		QVERIFY2(redirected != linear, "the re-directed wiring did not change the render");
		QVERIFY2(isScaledBy(linear, redirected, kFirstLeft),
			"the re-directed render is not the linear render without effect 0's gain");

		// Put the wiring back and confirm the render comes back with it.
		QVERIFY(graph.disconnect(sourceId, secondEffectId, 0, 0));
		QVERIFY(graph.connect(sourceId, firstEffectId, 0, 0));
		QCOMPARE(render(), linear);
	}

	//! The audio thread's path through the graph allocates nothing.
	void processingThroughTheGraphAllocatesNothing()
	{
		auto* chain = chainUnderTest();
		QVERIFY(chain->routesThroughGraph());

		const f_cnt_t fpp = Engine::audioEngine()->framesPerPeriod();
		std::vector<SampleFrame> block(fpp);
		for (f_cnt_t f = 0; f < fpp; ++f)
		{
			block[f][0] = liveSignal(0, f, 0);
			block[f][1] = liveSignal(0, f, 1);
		}
		SampleFrame* busData[1] = {block.data()};
		AudioBus bus{busData, 1, fpp};

		// The first block may touch lazy internals; measure the second.
		chain->processAudioBuffer(bus);

		lmms::test::tlCountAllocations = true;
		lmms::test::resetAllocationCount();
		chain->processAudioBuffer(bus);
		const std::uint64_t allocations = lmms::test::tlAllocationCount;
		lmms::test::tlCountAllocations = false;

		QCOMPARE(allocations, std::uint64_t{0});
	}

private:
	EffectChain* chainUnderTest()
	{
		return &Engine::mixer()->mixerChannel(kChannel)->m_fxChain;
	}

	void buildGraph()
	{
		auto mixer = Engine::mixer();
		while (mixer->numChannels() < kChannels)
		{
			mixer->createChannel();
		}

		// The channel's default send to master (Mixer::clearChannel) and its
		// default unity fader both stay as they are: the chain under test is the
		// only thing this test varies.
		auto* chain = &mixer->mixerChannel(kChannel)->m_fxChain;
		chain->appendEffect(new LiveScaleEffect(chain, kFirstLeft, kFirstRight));
		chain->appendEffect(new LiveScaleEffect(chain, kSecondLeft, kSecondRight));
	}

	QByteArray render()
	{
		auto mixer = Engine::mixer();
		const f_cnt_t fpp = Engine::audioEngine()->framesPerPeriod();

		QByteArray out;
		out.reserve(kPeriods * static_cast<int>(fpp * sizeof(SampleFrame)));

		std::vector<SampleFrame> masterOut(fpp);
		std::vector<SampleFrame> input(fpp);
		SampleFrame* busData[1] = {input.data()};

		for (int p = 0; p < kPeriods; ++p)
		{
			for (f_cnt_t f = 0; f < fpp; ++f)
			{
				input[f][0] = liveSignal(p, f, 0);
				input[f][1] = liveSignal(p, f, 1);
			}
			const AudioBus bus{busData, 1, fpp};
			mixer->mixToChannel(bus, kChannel);

			mixer->prepareMasterMix();
			zeroSampleFrames(masterOut.data(), fpp);
			mixer->masterMix(masterOut.data());

			out.append(reinterpret_cast<const char*>(masterOut.data()),
				static_cast<int>(fpp * sizeof(SampleFrame)));
		}

		return out;
	}
};

QTEST_GUILESS_MAIN(RoutingGraphLiveTest)
#include "RoutingGraphLiveTest.moc"
