/*
 * PatcherCommandsTest.cpp - the `patcher.*` group (feature row 69): the node
 * graph read as a patch, and the EDIT row 69 asked for.
 *
 * WHAT IS PROVEN HERE, each check a number:
 *   * both ids are registered with schemas and A16 rows;
 *   * patcher.get_state reports the DERIVED wiring of a chain that renders
 *     through its graph - roles, edges, output node, cached plan - editable;
 *   * patcher.set_wiring RE-WIRES that chain and the render changes: the
 *     channel is rendered through the mixer before and after, and the re-wired
 *     render is exactly the linear render without the bypassed effect's gain
 *     (the RoutingGraphLiveTest shape: the arithmetic of the missing stage, not
 *     merely "the bytes differ");
 *   * the AUTHORED wiring SURVIVES EffectChain::rebuildRoutingGraph() - what
 *     plugin.load / plugin.unload / moveUp / moveDown call - and the graph is
 *     still ON the path afterwards: this is row 28's DERIVED-graph constraint
 *     resolved, not documented away;
 *   * control.undo puts the previous wiring back, and a chain that was on its
 *     DERIVED wiring comes back to it AS the derivation (an empty edge list),
 *     not as a linear-looking authored patch; an empty wiring does the same;
 *   * every refusal (unknown reference, cycle, an unreachable output, a chain
 *     with no graph) is TYPED and writes nothing;
 *   * one block through the patched graph allocates nothing on the audio thread.
 *
 * The engine half is include/PatchWiring.h + EffectChain::setPatchWiring();
 * the honest bounds of the feature are docs/PATCHER-GRAPH.md.
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
 * You should have received a copy of the GNU General Public License along
 * with this program (see COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <QtTest>

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <cstdint>
#include <vector>

#include "AllocationProbe.h"
#include "AudioBuffer.h"
#include "AudioBus.h"
#include "AudioDevice.h"
#include "AudioEngine.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "Mixer.h"
#include "PatchWiring.h"
#include "Plugin.h"
#include "RoutingGraph.h"
#include "SampleFrame.h"

namespace lmms
{

namespace
{

//! Deterministic per-channel gain - a legacy Effect (no audio-ports model), so
//! the chain really builds and renders through its RoutingGraph.
class PatcherScaleEffect : public Effect
{
public:
	PatcherScaleEffect(Model* parent, float left, float right) :
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

const Plugin::Descriptor PatcherScaleEffect::s_descriptor
{
	"patcherscaletest",
	"Patcher wiring scale effect",
	"Deterministic per-channel gain used by PatcherCommandsTest",
	"LMMS",
	0x0100,
	Plugin::Type::Effect,
	nullptr,
	nullptr,
	nullptr
};

//! 8 * 256 frames - enough for the chain to have produced audio.
constexpr int kPeriods = 8;
constexpr int kChannels = 3;   // master, the chain under test, an empty chain
constexpr int kChannel = 1;
constexpr int kEmptyChannel = 2;
//! The bypassed effect's gain, which is what the re-directed render loses.
constexpr float kBypassedGain = 2.0f;

//! Deterministic signal in [-0.5, 0.5), integer arithmetic only.
inline float patcherSignal(int period, int frame, int side)
{
	const int n = period * 7919 + frame * 131 + side * 4093;
	return static_cast<float>((n % 2048) - 1024) * (1.0f / 2048.0f);
}

QByteArray sha256Hex(const QByteArray& data)
{
	return QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
}

//! True when every sample of @a scaled is @a base scaled by @a factor: the
//! re-directed render is expected to differ by exactly the work of the effect
//! the wiring took out of the path, which is a stronger claim than "they differ".
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

//! One edge of a wiring, in the JSON shape the command takes.
QJsonObject inputTo(const QString& to, int toPort = 0)
{
	QJsonObject edge;
	edge.insert(QStringLiteral("from"), QStringLiteral("input"));
	edge.insert(QStringLiteral("from_port"), 0);
	edge.insert(QStringLiteral("to"), to);
	edge.insert(QStringLiteral("to_port"), toPort);
	return edge;
}

//! The same shape between two named nodes, for an edge the chain's input does
//! not start (the only way to express a feedback path among the effects).
QJsonObject wire(const QString& from, const QString& to, int fromPort = 0, int toPort = 0)
{
	QJsonObject edge;
	edge.insert(QStringLiteral("from"), from);
	edge.insert(QStringLiteral("from_port"), fromPort);
	edge.insert(QStringLiteral("to"), to);
	edge.insert(QStringLiteral("to_port"), toPort);
	return edge;
}

} // namespace

} // namespace lmms

using namespace lmms;
using namespace lmms::control;

class PatcherCommandsTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		// The registry's own readiness gate: ControlRegistry::isReady() is
		// s_ready && Song && Mixer, and s_ready is false until main() flips it
		// when the engine is up (src/core/ControlRegistry.cpp:47/173). A test
		// binary has no main() worth speaking of, so every invoke below would be
		// refused with `engine_starting` - "the engine is not addressable yet
		// [engine_starting]" is what this file measured before the two calls.
		ControlRegistry::setReady(true);
		// The dummy device thread renders in the background; this test drives
		// the mixer synchronously, so stop it to keep the buffers stable.
		Engine::audioEngine()->audioDev()->stopProcessing();
		QCOMPARE(Engine::audioEngine()->framesPerPeriod(), static_cast<f_cnt_t>(256));

		auto mixer = Engine::mixer();
		while (mixer->numChannels() < kChannels) { mixer->createChannel(); }

		EffectChain* chain = chainUnderTest();
		chain->appendEffect(new PatcherScaleEffect(chain, kBypassedGain, kBypassedGain));
		chain->appendEffect(new PatcherScaleEffect(chain, 0.5f, 0.25f));
		QVERIFY2(chain->routesThroughGraph(),
			"the chain does not render through its graph - every wiring check below would be vacuous");

		// The fixture's own wiring: the id every command below is addressed with
		// must name THIS channel - the one initTestCase just filled - and the
		// empty channel must be a different one. Without this the ids could
		// drift back to a literal `ch-<index>` and every slot below would
		// measure a sibling channel (the master's empty chain) instead.
		QCOMPARE(channel(), control::channelId(Engine::mixer()->mixerChannel(kChannel)->id()));
		QCOMPARE(emptyChannel(), control::channelId(Engine::mixer()->mixerChannel(kEmptyChannel)->id()));
		QVERIFY2(channel() != emptyChannel(), "the two channels the test drives must be different");
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! The ids, the schemas and the contract rows: a group is in only when all
	//! three exist, which is what the release contract's 3.1 says.
	void theGroupIsRegisteredWithSchemasAndRows()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const QStringList ids = registry->commandIds();
		QVERIFY(ids.contains(QStringLiteral("patcher.get_state")));
		QVERIFY(ids.contains(QStringLiteral("patcher.set_wiring")));

		const control::ReversibilityTable& table = control::ReversibilityTable::instance();
		const control::ReversibilityEntry* read = table.lookup(QStringLiteral("patcher.get_state"));
		const control::ReversibilityEntry* write = table.lookup(QStringLiteral("patcher.set_wiring"));
		QVERIFY(read != nullptr && write != nullptr);
		QCOMPARE(read->cls, control::ReversibilityClass::NotMutating);
		QCOMPARE(write->cls, control::ReversibilityClass::Snapshot);
		QVERIFY(write->reversible && !write->reason.isEmpty() && !write->mechanism.isEmpty());
	}

	//! The read, on the chain the test built: three nodes, the derived linear
	//! wiring, and an edit that can land.
	void theReadReportsTheDerivedWiring()
	{
		const QJsonObject state = readState();
		QCOMPARE(state.value(QStringLiteral("wiring")).toString(), QStringLiteral("derived"));
		QVERIFY(state.value(QStringLiteral("routes_through_graph")).toBool());
		QCOMPARE(state.value(QStringLiteral("effect_count")).toInt(), 2);
		QCOMPARE(state.value(QStringLiteral("patch_dropped")).toBool(), false);

		const QJsonObject graph = state.value(QStringLiteral("graph")).toObject();
		QCOMPARE(graph.value(QStringLiteral("node_count")).toInt(), 3);
		QCOMPARE(graph.value(QStringLiteral("connection_count")).toInt(), 2);
		QCOMPARE(graph.value(QStringLiteral("output_node")).toInt(), 2);

		const QJsonArray nodes = graph.value(QStringLiteral("nodes")).toArray();
		QCOMPARE(nodes.at(0).toObject().value(QStringLiteral("ref")).toString(), QStringLiteral("input"));
		QCOMPARE(nodes.at(1).toObject().value(QStringLiteral("ref")).toString(), QStringLiteral("effect:0"));
		QCOMPARE(nodes.at(2).toObject().value(QStringLiteral("ref")).toString(), QStringLiteral("effect:1"));

		const QJsonObject wiring = state.value(QStringLiteral("effective_wiring")).toObject();
		QCOMPARE(wiring.value(QStringLiteral("edge_count")).toInt(), 2);
		QCOMPARE(wiring.value(QStringLiteral("output")).toString(), QStringLiteral("effect:1"));
		QVERIFY(state.value(QStringLiteral("editable")).toObject()
			.value(QStringLiteral("editable")).toBool());
	}

	//! The EDIT, measured on the audio path: bypass effect 0 by wiring the input
	//! straight to effect 1, and the channel's render loses exactly effect 0's
	//! gain.
	void editingTheWiringChangesTheRender()
	{
		const QByteArray linear = render();
		QVERIFY2(linear != QByteArray(linear.size(), '\0'), "the render is silent");

		const ControlResult applied = invoke(QStringLiteral("patcher.set_wiring"),
			{{QStringLiteral("target"), channel()},
			 {QStringLiteral("edges"), QJsonArray{inputTo(QStringLiteral("effect:1"))}},
			 {QStringLiteral("output"), QStringLiteral("effect:1")}});
		QVERIFY2(applied.ok, qPrintable(applied.errorMessage));

		QVERIFY(chainUnderTest()->patchActive());
		QVERIFY2(chainUnderTest()->routesThroughGraph(),
			"the patched chain fell off the graph - the render below would not be the patch's");

		const QByteArray patched = render();
		QVERIFY2(patched != linear, "the re-wired graph did not change the render");
		QVERIFY2(isScaledBy(patched, linear, 1.0f / kBypassedGain),
			"the re-wired render is not the linear render without the bypassed effect's gain");
		qInfo("PATCHER_EVIDENCE render-linear  sha256=%s", sha256Hex(linear).constData());
		qInfo("PATCHER_EVIDENCE render-patched sha256=%s", sha256Hex(patched).constData());
	}

	//! THE DERIVED-GRAPH CONSTRAINT, resolved: a rebuild is what plugin.load
	//! calls, and the authored wiring is still in force after it.
	void theAuthoredWiringSurvivesADerivedRebuild()
	{
		EffectChain* chain = chainUnderTest();
		QVERIFY(chain->patchActive());
		const QByteArray patched = render();

		chain->rebuildRoutingGraph();  // exactly what plugin.load/unload call

		QVERIFY2(chain->patchActive(), "the derived rebuild discarded the authored wiring");
		QCOMPARE(chain->patchDropped(), false);
		QVERIFY(chain->routesThroughGraph());
		QCOMPARE(chain->routingGraph().connections().size(), std::size_t{1});
		QCOMPARE(render(), patched);
	}

	//! A16: control.undo restores the wiring that was in force, and a DERIVED
	//! wiring comes back as the derivation rather than as a linear patch.
	void controlUndoRestoresTheWiring()
	{
		const QJsonObject before = readState();
		QCOMPARE(before.value(QStringLiteral("wiring")).toString(), QStringLiteral("authored"));
		const QByteArray patched = render();

		const ControlResult undone = invoke(QStringLiteral("control.undo"));
		QVERIFY2(undone.ok, qPrintable(undone.errorMessage));

		EffectChain* chain = chainUnderTest();
		QVERIFY2(!chain->patchActive(), "control.undo left the authored patch in force");
		QCOMPARE(chain->routingGraph().connections().size(), std::size_t{2});
		const QJsonObject after = readState();
		QCOMPARE(after.value(QStringLiteral("wiring")).toString(), QStringLiteral("derived"));
		QVERIFY2(render() != patched, "the render did not come back with the wiring");

		// SPEC A16: the record this command left, and the shape that makes
		// control.undo dispatch it (the assertion above is that it did).
		QJsonObject record;
		for (const QJsonValue& value : invoke(QStringLiteral("control.transactions")).result
			.value(QStringLiteral("transactions")).toArray())
		{
			const QJsonObject candidate = value.toObject();
			if (candidate.value(QStringLiteral("command")).toString() == QLatin1String("patcher.set_wiring"))
			{
				record = candidate;
			}
		}
		QVERIFY2(!record.isEmpty(), "patcher.set_wiring left no A16 record");
		QCOMPARE(record.value(QStringLiteral("class")).toString(), QStringLiteral("snapshot"));
		QCOMPARE(record.value(QStringLiteral("reversible")).toBool(), true);
		QCOMPARE(record.value(QStringLiteral("inverse")).toObject()
			.value(QStringLiteral("op")).toString(), QStringLiteral("patcher.set_wiring"));
	}

	//! An empty wiring is the documented way back to the derived route, and it
	//! is what the read calls "derived" again.
	void anEmptyWiringRestoresTheDerivedRoute()
	{
		appliedWiring(QJsonArray{inputTo(QStringLiteral("effect:1"))}, QStringLiteral("effect:1"));
		QCOMPARE(readState().value(QStringLiteral("wiring")).toString(), QStringLiteral("authored"));

		appliedWiring(QJsonArray(), QString());
		const QJsonObject state = readState();
		QCOMPARE(state.value(QStringLiteral("wiring")).toString(), QStringLiteral("derived"));
		QCOMPARE(state.value(QStringLiteral("effective_wiring")).toObject()
			.value(QStringLiteral("edge_count")).toInt(), 2);
		QCOMPARE(chainUnderTest()->routingGraph().connections().size(), std::size_t{2});
		QCOMPARE(chainUnderTest()->routingGraph().outputNodeId(), 2);
	}

	//! Every refusal is typed and writes nothing - the state is read back after
	//! each one and compared with the state before it.
	void refusalsWriteNothing()
	{
		const QJsonObject before = readState();
		const ControlResult missing = invoke(QStringLiteral("patcher.set_wiring"),
			{{QStringLiteral("target"), channel()},
			 {QStringLiteral("edges"), QJsonArray{inputTo(QStringLiteral("effect:7"))}}});
		QVERIFY(!missing.ok);
		QCOMPARE(missing.errorKind, ControlErrorKind::InvalidArgs);

		// A feedback path among the effects: effect:0 -> effect:1 -> effect:0.
		// NOT `effect:0 -> input`: the graph's input node has no input ports
		// (it is the source of the chain), so that edge is refused by the
		// port-arity check before the cycle check is ever reached - measured
		// ("input has 0 input port(s), so to_port 0 is out of range"), and the
		// order is the documented one in ControlCommandsPatcherShared.h: an
		// unknown reference, a repeated or self edge, a port outside a node's
		// arity, a cycle, then an unreachable output. The loop below is a
		// genuine cycle: refs exist, ports fit, and only the DAG check refuses.
		const ControlResult cycle = invoke(QStringLiteral("patcher.set_wiring"),
			{{QStringLiteral("target"), channel()},
			 {QStringLiteral("edges"), QJsonArray{inputTo(QStringLiteral("effect:0")),
				wire(QStringLiteral("effect:0"), QStringLiteral("effect:1")),
				wire(QStringLiteral("effect:1"), QStringLiteral("effect:0"))}}});
		QVERIFY(!cycle.ok);
		QVERIFY2(cycle.errorMessage.contains(QStringLiteral("cycle")),
			qPrintable(cycle.errorMessage));

		// a wiring whose output cannot be reached renders silence: refused
		const ControlResult unreachable = invoke(QStringLiteral("patcher.set_wiring"),
			{{QStringLiteral("target"), channel()},
			 {QStringLiteral("edges"), QJsonArray{inputTo(QStringLiteral("effect:0"))}},
			 {QStringLiteral("output"), QStringLiteral("effect:1")}});
		QVERIFY(!unreachable.ok);
		QCOMPARE(unreachable.errorKind, ControlErrorKind::InvalidArgs);

		// junk arguments, which the agent-surface sweep sends
		const ControlResult junk = invoke(QStringLiteral("patcher.set_wiring"),
			{{QStringLiteral("target"), channel()}, {QStringLiteral("edges"), QStringLiteral("nonsense")}});
		QCOMPARE(junk.errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(invoke(QStringLiteral("patcher.get_state"), {}).errorKind,
			ControlErrorKind::InvalidArgs);
		QCOMPARE(readState(), before);
	}

	//! The measured normal case for a chain of real devices, stated rather than
	//! hidden: a chain with nothing to route has no graph and reports it
	//! (`editable.editable` false with the reason), and an edit against it is
	//! REFUSED, typed, instead of silently stored. The kind is InvalidArgs, not
	//! Refused: the edit path validates the edge list against the CURRENT node
	//! set before it touches the chain (ControlCommandsPatcherShared.h's
	//! documented order), and this chain's node set is the input node alone, so
	//! any edge naming an effect is an unknown reference. The Refused kind is
	//! for a wiring that IS valid for the node set but cannot be taken by the
	//! graph (EffectChain::setPatchWiring's m_graphActive/m_patchDropped path),
	//! which a zero-effect chain cannot reach: its only valid wiring is the
	//! empty one, and that always means "the derived route".
	void aChainWithNoGraphRefusesAnEdit()
	{
		const QJsonObject state = invoke(QStringLiteral("patcher.get_state"),
			{{QStringLiteral("target"), emptyChannel()}}).result;
		QVERIFY(!state.value(QStringLiteral("routes_through_graph")).toBool());
		QCOMPARE(state.value(QStringLiteral("editable")).toObject()
			.value(QStringLiteral("editable")).toBool(), false);
		QVERIFY(!state.value(QStringLiteral("editable")).toObject()
			.value(QStringLiteral("reason")).toString().isEmpty());

		// (a) An edge list with no explicit `output`: refused before anything is
		// validated, because a chain with no effects has none for the default
		// output (the last effect) to name.
		const ControlResult refused = invoke(QStringLiteral("patcher.set_wiring"),
			{{QStringLiteral("target"), emptyChannel()},
			 {QStringLiteral("edges"), QJsonArray{inputTo(QStringLiteral("effect:1"))}}});
		QVERIFY(!refused.ok);
		QCOMPARE(refused.errorKind, ControlErrorKind::InvalidArgs);
		QVERIFY2(refused.errorMessage.contains(QStringLiteral("this empty")),
			qPrintable(refused.errorMessage));

		// (b) The same edit with the output NAMED: now the edge's reference is
		// checked against the node set, which is the input node alone, so
		// effect:1 is an unknown reference - typed, and still nothing written.
		const ControlResult named = invoke(QStringLiteral("patcher.set_wiring"),
			{{QStringLiteral("target"), emptyChannel()},
			 {QStringLiteral("edges"), QJsonArray{inputTo(QStringLiteral("effect:1"))}},
			 {QStringLiteral("output"), QStringLiteral("effect:1")}});
		QVERIFY(!named.ok);
		QCOMPARE(named.errorKind, ControlErrorKind::InvalidArgs);
		QVERIFY2(named.errorMessage.contains(QStringLiteral("0 effect")),
			qPrintable(named.errorMessage));

		// Both refusals left the chain exactly as the read above found it.
		QCOMPARE(invoke(QStringLiteral("patcher.get_state"),
			{{QStringLiteral("target"), emptyChannel()}}).result, state);
	}

	//! Realtime: the audio thread's block through a PATCHED graph allocates
	//! nothing - the patch is control-thread work, not per-block work.
	void aPatchedBlockAllocatesNothing()
	{
		appliedWiring(QJsonArray{inputTo(QStringLiteral("effect:1"))}, QStringLiteral("effect:1"));

		EffectChain* chain = chainUnderTest();
		QVERIFY(chain->routesThroughGraph());
		const f_cnt_t fpp = Engine::audioEngine()->framesPerPeriod();
		std::vector<SampleFrame> block(fpp);
		SampleFrame* busData[1] = {block.data()};
		AudioBus bus{busData, 1, fpp};

		chain->processAudioBuffer(bus);  // warm-up: the first block may touch lazy internals

		test::tlCountAllocations = true;
		test::resetAllocationCount();
		chain->processAudioBuffer(bus);
		const std::uint64_t allocations = test::tlAllocationCount;
		test::tlCountAllocations = false;
		QCOMPARE(allocations, std::uint64_t{0});
	}

private:
	EffectChain* chainUnderTest() { return &Engine::mixer()->mixerChannel(kChannel)->m_fxChain; }

	/*! The `ch-<n>` id the ENGINE gives the mixer channel at \a index.
	 *
	 *  Asked for, never built from the index. `channelId(<n>)` (the vocabulary
	 *  formatter, lmms::control::channelId) writes the literal `ch-<n>`, and
	 *  the number in a real channel id is the channel OBJECT's own ProjectIds
	 *  id - one project-wide counter shared with tracks, clips, notes and
	 *  effects - so `ch-<index>` names whatever object was allocated when that
	 *  number came up. Measured here with gdb: the mixer has three channels
	 *  with ids 1 (master), 3 and 2, so the fixture's `channel()` used to be
	 *  `ch-1` and every wiring slot below drove the MASTER's empty fx chain
	 *  ("effect:1 names no node of this chain: it has 0 effect(s)") while
	 *  initTestCase appended its two effects to mixerChannel(1) - the chain
	 *  under test, whose id is ch-3. mixer.get_state publishes each channel's
	 *  own id in index order, which is the same order mixerChannel() uses.
	 */
	QString channelIdAt(int index) const
	{
		const QJsonArray channels = ControlRegistry::instance()
			->invoke(QStringLiteral("mixer.get_state")).result
			.value(QStringLiteral("channels")).toArray();
		if (index < 0 || index >= channels.size()) { return QString(); }
		return channels.at(index).toObject().value(QStringLiteral("id")).toString();
	}

	QString channel() const { return channelIdAt(kChannel); }
	QString emptyChannel() const { return channelIdAt(kEmptyChannel); }

	ControlResult invoke(const QString& id, const QJsonObject& args = QJsonObject())
	{
		return ControlRegistry::instance()->invoke(id, args);
	}

	QJsonObject readState()
	{
		const ControlResult read = invoke(QStringLiteral("patcher.get_state"),
			{{QStringLiteral("target"), channel()}});
		if (!read.ok)
		{
			// qFail rather than QVERIFY2: this helper RETURNS a value, and
			// QVERIFY's `return;` would not compile here.
			QTest::qFail(qPrintable(read.errorMessage), __FILE__, __LINE__);
			return QJsonObject();
		}
		return read.result;
	}

	//! Applies a wiring through the command and asserts it landed.
	void appliedWiring(const QJsonArray& edges, const QString& output)
	{
		QJsonObject args;
		args.insert(QStringLiteral("target"), channel());
		args.insert(QStringLiteral("edges"), edges);
		if (!output.isEmpty()) { args.insert(QStringLiteral("output"), output); }
		const ControlResult applied = invoke(QStringLiteral("patcher.set_wiring"), args);
		QVERIFY2(applied.ok, qPrintable(applied.errorMessage));
	}

	//! The channel's block through the mixer for kPeriods periods.
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
				input[f][0] = patcherSignal(p, f, 0);
				input[f][1] = patcherSignal(p, f, 1);
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

QTEST_GUILESS_MAIN(PatcherCommandsTest)
#include "PatcherCommandsTest.moc"
