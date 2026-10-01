/*
 * RoutingGraphScheduleTest.cpp - the published schedule versus the plan-order
 * execution (multicore Slice 0 + Slice 1, SPEC-MULTICORE-SCHEDULING-DRAFT §6)
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

//! Slice 1: topology as data, serial execution, SAME BYTES. The schedule
//! rebuildPlan() publishes (required/pending/class per node plus the ready
//! list) equals the plan-order execution - checked against an in-degree
//! recompute made from connections() here, not against rebuildPlan's own
//! bookkeeping - and BOTH walks (the ready list node by node, and process()
//! over m_plan) reproduce the committed reference render byte for byte.
//!
//! Slice 0 runtime halves: AllocationProbe armed ON a pool worker reading the
//! declared graph path as zero allocations (a deliberate allocation on that
//! same thread proves the probe was really on), and the queue-full branch of
//! addJob() refusing exactly one counted job instead of a qWarning.
//!
//! Evidence lines are printed unconditionally (stdout, flushed) for
//! docs/ROUTING-GRAPH-LIVE.md §9.

#include <QtTest>

#include <QByteArray>
#include <QCryptographicHash>
#include <QFile>
#include <QString>
#include <QThread>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <map>
#include <vector>

#include "AllocationProbe.h"
#include "AudioBuffer.h"
#include "AudioDevice.h"
#include "AudioEngine.h"
#include "AudioEngineWorkerThread.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "Mixer.h"
#include "Plugin.h"
#include "RoutingChainNodes.h"
#include "RoutingGraph.h"
#include "RoutingNodes.h"
#include "SampleFrame.h"

#ifndef ROUTING_GRAPH_LIVE_REFERENCE_FILE
#define ROUTING_GRAPH_LIVE_REFERENCE_FILE "tests/reference/routing-graph-live-render.raw"
#endif

namespace lmms
{

//! Defined in src/core/AudioEngineWorkerThread.cpp: the queue-full refusal
//! counter, with external linkage so this test can read it without a header
//! change (multicore Slice 0).
extern std::atomic<std::uint64_t> queueFullRefusals;

namespace
{

//! Deterministic per-channel gain, the same fixture RoutingGraphLiveTest uses
//! so the reference bytes and the evidence lines line up with its report.
class ScheduleScaleEffect : public Effect
{
public:
	ScheduleScaleEffect(Model* parent, float left, float right) :
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

const Plugin::Descriptor ScheduleScaleEffect::s_descriptor
{
	"schedulescaletest",
	"Schedule scale effect",
	"Deterministic per-channel gain used by RoutingGraphScheduleTest",
	"LMMS",
	0x0100,
	Plugin::Type::Effect,
	nullptr,
	nullptr,
	nullptr
};

constexpr int kPeriods = 16;          //!< 16 * 256 * 2ch * 4B = 32768 bytes
constexpr int kChannels = 2;          //!< Master + the channel carrying the chain
constexpr int kChannel = 1;           //!< The channel under test
constexpr float kFirstLeft = 2.0f;    //!< The reference chain's gains, in order
constexpr float kFirstRight = 2.0f;
constexpr float kSecondLeft = 0.5f;
constexpr float kSecondRight = 0.25f;

//! Deterministic signal in [-0.5, 0.5). Integer arithmetic only (no libm).
inline float liveSignal(int period, int frame, int side)
{
	const int n = period * 7919 + frame * 131 + side * 4093;
	return static_cast<float>((n % 2048) - 1024) * (1.0f / 2048.0f);
}

void printEvidence(const char* label, const QByteArray& data)
{
	const QByteArray hash = QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
	std::fprintf(stdout, "ROUTING_GRAPH_EVIDENCE %s bytes=%d sha256=%s\n",
		label, static_cast<int>(data.size()), hash.constData());
	std::fflush(stdout);
}

//! Interleaves a 2-channel planar block the way the bus publishes it.
QByteArray interleave(const AudioBuffer& planar, f_cnt_t frames)
{
	QByteArray bytes(static_cast<int>(frames * 2 * sizeof(float)), Qt::Uninitialized);
	auto* samples = reinterpret_cast<float*>(bytes.data());
	const float* left = planar.buffer(0).data();
	const float* right = planar.buffer(1).data();
	for (f_cnt_t f = 0; f < frames; ++f)
	{
		samples[2 * f] = left[f];
		samples[2 * f + 1] = right[f];
	}
	return bytes;
}

//! Period @a period of the deterministic signal, mirrored the way
//! EffectChain::processThroughGraph mirrors the bus (flags included).
void mirrorSignalInto(AudioBuffer& planar, int period, f_cnt_t frames)
{
	planar.silenceAllChannels();
	float* left = planar.buffer(0).data();
	float* right = planar.buffer(1).data();
	for (f_cnt_t f = 0; f < frames; ++f)
	{
		left[f] = liveSignal(period, f, 0);
		right[f] = liveSignal(period, f, 1);
	}
	planar.assumeNonSilent(0);
	planar.assumeNonSilent(1);
}

//! The published schedule executed node by node, then the same output copy
//! RoutingGraph::process() performs. If the published order were not a valid
//! topological order the byte comparisons would notice.
void walkPublishedSchedule(RoutingGraph& graph, AudioBuffer& out)
{
	for (const int id : graph.readyList())
	{
		RoutingNode* node = graph.node(id);
		if (node != nullptr) { node->process(graph.frames()); }
	}
	const RoutingNode* output = graph.node(graph.outputNodeId());
	if (output == nullptr) { return; }

	const AudioBuffer& source = output->output(0);
	const f_cnt_t frames = std::min(graph.frames(), out.frames());
	const ch_cnt_t channels = std::min(graph.channels(), out.totalChannels());
	for (ch_cnt_t c = 0; c < channels; ++c)
	{
		float* dst = out.buffer(c).data();
		const float* src = source.buffer(c).data();
		for (f_cnt_t f = 0; f < frames; ++f) { dst[f] = src[f]; }
	}
	for (ch_cnt_t c = 0; c < out.totalChannels(); ++c) { out.assumeNonSilent(c); }
}

//! Slice 0's worker-side probe: arms AllocationProbe ON the pool thread that
//! takes it, runs the declared graph path, then allocates once deliberately -
//! a zero count cannot mean "the probe was never armed on this thread".
class WorkerProbeJob : public ThreadableJob
{
public:
	WorkerProbeJob(RoutingGraph* graph, AudioBuffer* block) :
		m_graph(graph),
		m_block(block)
	{
	}

	bool requiresProcessing() const override { return true; }

	bool finished() const { return m_finished.load(); }
	auto runner() const -> QThread* { return m_runner; }
	auto graphAllocations() const -> std::uint64_t { return m_graphAllocations; }
	auto controlAllocations() const -> std::uint64_t { return m_controlAllocations; }

protected:
	void doProcessing() override
	{
		m_runner = QThread::currentThread();
		m_graph->process(*m_block); // warm-up on this thread, probe off
		lmms::test::resetAllocationCount();
		lmms::test::tlCountAllocations = true;
		m_graph->process(*m_block);
		m_graphAllocations = lmms::test::tlAllocationCount;
		void* deliberate = ::operator new(64);
		m_controlAllocations = lmms::test::tlAllocationCount;
		::operator delete(deliberate);
		lmms::test::tlCountAllocations = false;
		m_finished.store(true);
	}

private:
	RoutingGraph* m_graph;
	AudioBuffer* m_block;
	std::atomic<bool> m_finished{false};
	QThread* m_runner = nullptr;
	std::uint64_t m_graphAllocations = 0;
	std::uint64_t m_controlAllocations = 0;
};

//! Always claimable so every push counts even while a worker drains earlier
//! slots: a Done job would make addJob() skip and the refusal count would
//! depend on timing.
class QueueFillerJob : public ThreadableJob
{
public:
	bool requiresProcessing() const override { return true; }

protected:
	void doProcessing() override {}
};

} // namespace

} // namespace lmms

using namespace lmms;

class RoutingGraphScheduleTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		// The dummy device renders in the background; this test drives the
		// mixer's paths synchronously, so stop it to keep buffers stable.
		Engine::audioEngine()->audioDev()->stopProcessing();
		QCOMPARE(Engine::audioEngine()->framesPerPeriod(), static_cast<f_cnt_t>(256));
		buildGraph();
	}

	void cleanupTestCase()
	{
		Engine::destroy();
	}

	//! Slice 1 data claim: ready list == plan order, and required/pending/class
	//! match an in-degree recompute made here from connections().
	void publishedScheduleMatchesThePlanOrderExecution()
	{
		auto* chain = chainUnderTest();
		QVERIFY(chain->routesThroughGraph());
		const RoutingGraph& graph = chain->routingGraph();

		const auto& ready = graph.readyList();
		const auto& plan = graph.processingOrder();
		QCOMPARE(ready.size(), plan.size());
		for (std::size_t i = 0; i < plan.size(); ++i) { QCOMPARE(ready[i], plan[i]); }
		QCOMPARE(ready.size(), std::size_t{3});

		std::map<int, int> indegree;
		for (const RoutingConnection& c : graph.connections()) { ++indegree[c.destNode]; }
		std::map<int, int> position;
		for (std::size_t i = 0; i < ready.size(); ++i)
		{
			position[ready[i]] = static_cast<int>(i);
		}

		const auto& schedule = graph.schedule();
		QCOMPARE(schedule.size(), std::size_t{3});
		for (int id = 0; id < 3; ++id) // the fixture uses ids 0..2, all live
		{
			QVERIFY(graph.node(id) != nullptr);
			QCOMPARE(schedule[id].required, indegree[id]);
			QCOMPARE(schedule[id].pending, schedule[id].required);
		}
		for (const RoutingConnection& c : graph.connections())
		{
			QVERIFY(position.at(c.sourceNode) < position.at(c.destNode));
		}

		// The classes as this fixture declares them: the boundary mirror is
		// instance-free DSP, a wrapped effect's work is chain-scheduled.
		QCOMPARE(graph.node(0)->typeName(), QStringLiteral("chain_input"));
		QCOMPARE(schedule[0].schedulingClass, RoutingScheduleClass::InstanceFree);
		QCOMPARE(graph.node(1)->typeName(), QStringLiteral("effect"));
		QCOMPARE(schedule[1].schedulingClass, RoutingScheduleClass::Chain);
		QCOMPARE(graph.node(2)->typeName(), QStringLiteral("effect"));
		QCOMPARE(schedule[2].schedulingClass, RoutingScheduleClass::Chain);

		// required only means something above zero: two sources into one sink.
		RoutingGraph diamond;
		const int left = diamond.addNode(std::make_unique<ConstantSourceNode>(0.5f));
		const int right = diamond.addNode(std::make_unique<ConstantSourceNode>(0.25f));
		const int sink = diamond.addNode(std::make_unique<SinkNode>());
		QVERIFY(diamond.connect(left, sink));
		QVERIFY(diamond.connect(right, sink));
		QVERIFY(diamond.setOutputNode(sink));
		QCOMPARE(diamond.schedule().size(), std::size_t{3});
		QCOMPARE(diamond.schedule()[left].required, 0);
		QCOMPARE(diamond.schedule()[right].required, 0);
		QCOMPARE(diamond.schedule()[sink].required, 2);
		QCOMPARE(diamond.schedule()[sink].pending, 2);
		QVERIFY(diamond.readyList() == diamond.processingOrder());
		QCOMPARE(diamond.readyList().back(), sink);
	}

	//! Both walks of the same prepared graph reproduce the committed bytes:
	//! the published ready list node by node, and process() over m_plan.
	void publishedScheduleExecutionReproducesTheCommittedReference()
	{
		auto* chain = chainUnderTest();
		QVERIFY2(chain->routesThroughGraph(),
			"the chain is not routed through the graph - the proof would be vacuous");
		RoutingGraph& graph = chain->routingGraph();
		const f_cnt_t fpp = Engine::audioEngine()->framesPerPeriod();

		auto* chainInput = dynamic_cast<ChainInputNode*>(graph.node(0));
		QVERIFY(chainInput != nullptr);
		const AudioBuffer* originalSource = chainInput->source();
		QVERIFY(originalSource != nullptr);

		AudioBuffer input(fpp, 2);
		AudioBuffer planOut(fpp, 2);
		AudioBuffer scheduleOut(fpp, 2);
		QByteArray planBytes;
		QByteArray scheduleBytes;
		planBytes.reserve(kPeriods * static_cast<int>(fpp * 2 * sizeof(float)));
		scheduleBytes.reserve(kPeriods * static_cast<int>(fpp * 2 * sizeof(float)));

		// Point the boundary node at this test's mirror so both walks see the
		// signal the reference was captured from.
		chainInput->setSource(&input);
		for (int p = 0; p < kPeriods; ++p)
		{
			mirrorSignalInto(input, p, fpp);
			graph.process(planOut);
			walkPublishedSchedule(graph, scheduleOut);
			planBytes.append(interleave(planOut, fpp));
			scheduleBytes.append(interleave(scheduleOut, fpp));
		}
		chainInput->setSource(originalSource);

		const QByteArray reference = readReference();
		printEvidence("reference", reference);
		printEvidence("plan-walk", planBytes);
		printEvidence("schedule-walk", scheduleBytes);

		QCOMPARE(scheduleBytes.size(), reference.size());
		QVERIFY2(scheduleBytes == reference,
			"the published-schedule walk is NOT byte-identical to the committed reference");
		QVERIFY2(planBytes == reference,
			"the plan-order walk is NOT byte-identical to the committed reference");
		QVERIFY2(scheduleBytes == planBytes,
			"the published schedule and the plan-order execution disagree byte for byte");
	}

	//! Slice 0 runtime half: AllocationProbe armed on a LIVE pool worker.
	void allocationProbeRunsOnAWorkerThread()
	{
		auto* chain = chainUnderTest();
		QVERIFY(chain->routesThroughGraph());
		const f_cnt_t fpp = Engine::audioEngine()->framesPerPeriod();
		AudioBuffer block(fpp, 2);

		WorkerProbeJob job{&chain->routingGraph(), &block};
		AudioEngineWorkerThread::addJob(&job);

		// The test thread never drains the queue: it wakes the pool and waits, so whoever runs
		// this job is a pool worker. (Workers used to re-check the queue every 100 ms on their own;
		// they sleep until woken since the wake became an atomic generation, BUGS_FOUND 11.22 a.)
		AudioEngineWorkerThread::wakeWorkers();
		QTRY_VERIFY_WITH_TIMEOUT(job.finished(), 5000);

		auto* worker = dynamic_cast<AudioEngineWorkerThread*>(job.runner());
		QVERIFY2(worker != nullptr, "the probe job did not run on a pool worker thread");
		QVERIFY2(job.runner() != QThread::currentThread(),
			"the probe job ran on the test thread - nothing would be proven about workers");
		QCOMPARE(job.graphAllocations(), std::uint64_t{0});
		QCOMPARE(job.controlAllocations(), job.graphAllocations() + 1);

		std::fprintf(stdout, "MC_SLICE0_WORKER_PROBE graph-allocations=%llu control-allocations=%llu\n",
			static_cast<unsigned long long>(job.graphAllocations()),
			static_cast<unsigned long long>(job.controlAllocations()));
		std::fflush(stdout);
	}

	//! Slice 0 runtime half: the queue-full branch refuses exactly one job and
	//! counts it instead of formatting on the audio thread's side of the call.
	void queueFullRefusalIsCounted()
	{
		static QueueFillerJob filler;

		AudioEngineWorkerThread::resetJobQueue();
		const std::uint64_t before = queueFullRefusals.load();
		for (std::size_t i = 0; i <= AudioEngineWorkerThread::JobQueue::JOB_QUEUE_SIZE; ++i)
		{
			AudioEngineWorkerThread::addJob(&filler);
		}
		const std::uint64_t after = queueFullRefusals.load();
		QCOMPARE(after - before, std::uint64_t{1});
		AudioEngineWorkerThread::resetJobQueue();

		std::fprintf(stdout, "MC_SLICE0_QUEUE_FULL refused=%llu\n",
			static_cast<unsigned long long>(after - before));
		std::fflush(stdout);
	}

private:
	EffectChain* chainUnderTest()
	{
		return &Engine::mixer()->mixerChannel(kChannel)->m_fxChain;
	}

	void buildGraph()
	{
		auto mixer = Engine::mixer();
		while (mixer->numChannels() < kChannels) { mixer->createChannel(); }

		// The channel's default send to master and its default unity fader both
		// stay as they are: the chain under test is the only thing varied.
		auto* chain = &mixer->mixerChannel(kChannel)->m_fxChain;
		chain->appendEffect(new ScheduleScaleEffect(chain, kFirstLeft, kFirstRight));
		chain->appendEffect(new ScheduleScaleEffect(chain, kSecondLeft, kSecondRight));
	}

	QByteArray readReference()
	{
		// QTest's macros that return early need a void function; this helper
		// reports through qVerify and yields an empty array the caller fails on.
		const QString refPath = QString::fromUtf8(ROUTING_GRAPH_LIVE_REFERENCE_FILE);
		if (!QFile::exists(refPath))
		{
			QTest::qVerify(false, "the committed reference render exists", "",
				__FILE__, __LINE__);
			return {};
		}
		QFile ref{refPath};
		if (!ref.open(QIODevice::ReadOnly))
		{
			QTest::qVerify(false, "the committed reference render opens", "",
				__FILE__, __LINE__);
			return {};
		}
		const QByteArray bytes = ref.readAll();
		ref.close();
		return bytes;
	}
};

QTEST_GUILESS_MAIN(RoutingGraphScheduleTest)
#include "RoutingGraphScheduleTest.moc"
