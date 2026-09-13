/*
 * ControlLinkCommandsTest.cpp - the link.* command group (SPEC-zene-studio.md
 *                                A11-A16): the session tempo/phase sync model.
 *
 * The engine half is include/LinkSync.h (the model), include/LinkPeerTransport.h
 * (the seam), src/core/LinkSync.cpp and src/core/LinkUdpTransport.cpp. What this
 * file holds to account is the SURFACE the release contract section 3.1 requires
 * of it - five registered commands with argument and result schemas, typed
 * refusals, and a reversibility class whose inverse actually works - plus the
 * MODEL's own arithmetic, which is where the two claims that could silently be
 * wrong live:
 *
 *   1. A NEWER DECLARATION IS ADOPTED, and an older one is not, including the
 *      same-revision case two simultaneous joiners produce. The tiebreak has to
 *      be a total order or two instances oscillate instead of converging.
 *   2. THE PHASE IS MEASURED, NOT GUESSED. A peer's announcement carries its
 *      beat at its send time, and the receiver advances it by the datagram's
 *      actual transit time on the shared monotone clock - so the session phase
 *      after adoption is a number this test can compute independently.
 *
 * The two-instance proof over real sockets is the registered ctest
 * `ControlLinkSync` (tests/control-link-sync.py), which starts two real
 * binaries. This file is the model and the surface; that one is the wire.
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
 */

#include <QtTest>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "Engine.h"
#include "LinkSync.h"

using namespace lmms;

namespace
{

//! Invoke a command through the registry, exactly as the socket does.
ControlResult run(const QString& id, const QJsonObject& args = QJsonObject())
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! link.get_state, whose payload every setter also returns.
QJsonObject state()
{
	return run(QStringLiteral("link.get_state")).result;
}

QJsonObject transportOf(const QJsonObject& report)
{
	return report.value(QStringLiteral("transport")).toObject();
}

//! Is `id` in the session this instance reports?
bool sessionHasPeer(const QJsonObject& report, const QString& id)
{
	for (const QJsonValue& value : report.value(QStringLiteral("peers")).toArray())
	{
		if (value.toObject().value(QStringLiteral("id")).toString() == id)
		{
			return true;
		}
	}
	return false;
}

QString peerIdOf(const QJsonObject& report)
{
	return report.value(QStringLiteral("peer_id")).toString();
}

quint32 revisionOf(const QJsonObject& report)
{
	return static_cast<quint32>(report.value(QStringLiteral("session_revision")).toInt());
}

//! An announcement from a peer, as it would arrive on the wire.
link::LinkPeerPacket packetFrom(const QString& id, quint32 revision, double tempo,
	const QString& owner = QString())
{
	link::LinkPeerPacket packet;
	packet.model = QString::fromLatin1(link::SyncModelName);
	packet.id = id;
	packet.name = id + QStringLiteral("-name");
	packet.revision = revision;
	packet.revisionOwner = owner.isEmpty() ? id : owner;
	packet.tempo = tempo;
	packet.beat = 0.0;
	packet.sentUs = LinkSyncEngine::monotonicMicros();
	return packet;
}

//! The engine's own tempo, read through the transport group's own command.
int engineTempo()
{
	return run(QStringLiteral("transport.get_state")).result
		.value(QStringLiteral("tempo")).toInt();
}

const control::ReversibilityEntry* contractRow(const QString& id)
{
	return control::ReversibilityTable::instance().lookup(id);
}

} // namespace

class ControlLinkCommandsTest : public QObject
{
	Q_OBJECT
private slots:

	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
		// A known tempo, so "the follower adopted it" is a measurement and not
		// a coincidence: every adoption assertion below names a value the
		// engine could not have reached on its own.
		run(QStringLiteral("transport.set_tempo"), {{QStringLiteral("bpm"), 120}});
	}

	void cleanupTestCase()
	{
		LinkSyncEngine::instance()->setEnabled(false);
		LinkSyncEngine::destroy();
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! Every command of the group declares the contract's parts: a group.verb
	//! id, both schemas, a description and an empty `requires` (headless-safe).
	void requiredCommandsAreRegistered()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const QStringList mutating = {
			QStringLiteral("link.set_enabled"), QStringLiteral("link.set_quantum"),
			QStringLiteral("link.set_start_stop_sync"),
			QStringLiteral("link.set_session_tempo")};
		const QStringList all = QStringList{QStringLiteral("link.get_state")} + mutating;
		for (const QString& id : all)
		{
			QVERIFY2(registry->hasCommand(id), qPrintable(QStringLiteral("missing command %1").arg(id)));
			const ControlCommand* cmd = registry->command(id);
			QVERIFY(cmd != nullptr);
			QCOMPARE(cmd->group, QStringLiteral("link"));
			QVERIFY(!cmd->verb.isEmpty());
			QVERIFY(!cmd->description.isEmpty());
			QVERIFY(!cmd->argsSchema.isEmpty());
			QVERIFY(!cmd->resultSchema.isEmpty());
			// SPEC A13 headless parity: the declaration exists and is empty.
			QVERIFY2(cmd->requiresDecl.isEmpty(), qPrintable(id + " declares a requires excuse"));
			QCOMPARE(cmd->mutating, mutating.contains(id));
		}
	}

	/*! The read-only half answers with the MODEL it is speaking and the limits
	 *  it does not paper over: an empty session before anything is enabled, the
	 *  announcement cadence, and - the honesty row - that real Ableton Link
	 *  interoperability is NOT in this build, with the licence reference. */
	void stateNamesTheModelAndItsLimits()
	{
		LinkSyncEngine::instance()->setEnabled(false);
		const QJsonObject report = state();
		QCOMPARE(report.value(QStringLiteral("enabled")).toBool(), false);
		QCOMPARE(report.value(QStringLiteral("model")).toString(), QString::fromLatin1(link::SyncModelName));
		QCOMPARE(report.value(QStringLiteral("peer_count")).toInt(), 0);
		QCOMPARE(report.value(QStringLiteral("peers")).toArray().size(), 0);
		QCOMPARE(report.value(QStringLiteral("quantum")).toInt(), link::DefaultQuantum);
		QVERIFY(!peerIdOf(report).isEmpty());

		const QJsonObject transport = transportOf(report);
		QCOMPARE(transport.value(QStringLiteral("publish_interval_ms")).toInt(),
			link::DefaultPublishIntervalMs);
		QCOMPARE(transport.value(QStringLiteral("peer_timeout_ms")).toInt(), link::PeerTimeoutMs);
		QVERIFY(!transport.value(QStringLiteral("endpoint")).toString().isEmpty());
		if (!transport.value(QStringLiteral("available")).toBool())
		{
			// A gap must be STATED, never an empty session that looks idle.
			QVERIFY2(!transport.value(QStringLiteral("reason")).toString().isEmpty(),
				"the transport reports itself unavailable with no reason");
		}

		const QJsonObject interop = report.value(QStringLiteral("interop")).toObject();
		QCOMPARE(interop.value(QStringLiteral("ableton_link")).toBool(), false);
		QVERIFY(interop.value(QStringLiteral("reason")).toString()
			.contains(QStringLiteral("LICENSE.md")));
	}

	//! Enabling joins the session and starts announcing; disabling leaves it and
	//! forgets every peer - and both are visible in the state.
	void enablingJoinsAndDisablingLeaves()
	{
		const ControlResult on = run(QStringLiteral("link.set_enabled"),
			{{QStringLiteral("enabled"), true}});
		QVERIFY2(on.ok, qPrintable(on.errorMessage));
		QCOMPARE(on.result.value(QStringLiteral("enabled")).toBool(), true);
		QCOMPARE(on.result.value(QStringLiteral("previous")).toBool(), false);
		QVERIFY(!LinkSyncEngine::instance()->peerId().isEmpty());
		if (!transportOf(on.result).value(QStringLiteral("available")).toBool())
		{
			QSKIP("this platform cannot carry announcements; the model is still covered here");
		}
		// One announcement cycle: proof the transport is not merely configured.
		LinkSyncEngine::instance()->service();
		QVERIFY(state().value(QStringLiteral("published_count")).toDouble() >= 1.0);

		const ControlResult off = run(QStringLiteral("link.set_enabled"),
			{{QStringLiteral("enabled"), false}});
		QVERIFY2(off.ok, qPrintable(off.errorMessage));
		QCOMPARE(off.result.value(QStringLiteral("enabled")).toBool(), false);
		QCOMPARE(state().value(QStringLiteral("peer_count")).toInt(), 0);
	}

	/*! A newer declaration is adopted and an older one is NOT - including the
	 *  same-revision case two simultaneous joiners produce, where the owner id is
	 *  the tiebreak. Without a total order here two instances would swap tempos
	 *  forever instead of converging.
	 */
	void theNewestDeclarationWins()
	{
		setEnabledForTest(true);
		const quint32 mine = revisionOf(state());
		const QString other = QStringLiteral("peer-beta");

		// Newer: adopted, and the engine's own tempo moves with it.
		observe(packetFrom(other, mine + 1, 150.0));
		QJsonObject report = state();
		QCOMPARE(report.value(QStringLiteral("session_tempo")).toDouble(), 150.0);
		QCOMPARE(report.value(QStringLiteral("tempo_is_local")).toBool(), false);
		QCOMPARE(engineTempo(), 150);
		QCOMPARE(report.value(QStringLiteral("peer_count")).toInt(), 1);
		QVERIFY(sessionHasPeer(report, other));
		const quint32 adopted = revisionOf(report);
		QCOMPARE(adopted, mine + 1);

		// Older: remembered as a peer, but it does NOT drag the session back.
		observe(packetFrom(QStringLiteral("peer-gamma"), 1, 90.0));
		report = state();
		QCOMPARE(report.value(QStringLiteral("session_tempo")).toDouble(), 150.0);
		QCOMPARE(engineTempo(), 150);
		QVERIFY(sessionHasPeer(report, QStringLiteral("peer-gamma")));

		// Same revision, higher owner id: adopted (both sides pick this winner).
		observe(packetFrom(QStringLiteral("peer-zulu"), adopted, 111.0,
			QStringLiteral("zzz-owner")));
		report = state();
		QCOMPARE(report.value(QStringLiteral("session_tempo")).toDouble(), 111.0);
		QCOMPARE(report.value(QStringLiteral("session_revision_owner")).toString(),
			QStringLiteral("zzz-owner"));
		QCOMPARE(engineTempo(), 111);

		// Same revision, LOWER owner id: not adopted.
		observe(packetFrom(QStringLiteral("peer-alpha"), adopted, 77.0,
			QStringLiteral("aaa-owner")));
		QCOMPARE(state().value(QStringLiteral("session_tempo")).toDouble(), 111.0);
		QCOMPARE(engineTempo(), 111);
	}

	/*! THE PHASE CLAIM, computed independently. A peer announces beat 0 at a
	 *  send time one second in the past at 150 BPM, so the beat it is playing at
	 *  the moment this instance reads the datagram is 1.0 * 150 / 60 = 2.5 beats
	 *  - and after adoption that is where the session phase sits. A model that
	 *  copied `beat` without the transit term would report ~0.0 and fail here.
	 */
	void adoptionPlacesTheSharedBeatPhase()
	{
		setEnabledForTest(true);
		// A tempo the engine is not already at, so the phase arithmetic below is
		// the only thing that can produce the number.
		run(QStringLiteral("link.set_session_tempo"), {{QStringLiteral("bpm"), 150.0}});
		const quint32 mine = revisionOf(state());

		link::LinkPeerPacket packet = packetFrom(QStringLiteral("peer-phase"), mine + 1, 150.0);
		packet.beat = 0.0;
		packet.sentUs = LinkSyncEngine::monotonicMicros() - 1000000;  // one second ago
		observe(packet);

		const QJsonObject report = state();
		QCOMPARE(report.value(QStringLiteral("session_tempo")).toDouble(), 150.0);
		const double phase = report.value(QStringLiteral("phase_beats")).toDouble();
		// 2.5 beats expected; the bound allows the microseconds between the
		// stamp and the read, and nothing else.
		QVERIFY2(phase > 2.4 && phase < 2.8,
			qPrintable(QStringLiteral("session phase is %1 beats, expected ~2.5").arg(phase)));
		// The engine's own phase is measured from the play head, so it is a
		// different quantity - and the error between them is reported, wrapped.
		const double error = report.value(QStringLiteral("phase_error_beats")).toDouble();
		QVERIFY(error > -2.0 && error <= 2.0);
	}

	//! The peer table is bounded and quiet peers leave: a session cannot grow
	//! this instance's memory, and a peer that stopped announcing is not a peer.
	void thePeerTableIsBoundedAndQuietPeersLeave()
	{
		setEnabledForTest(true);
		const quint32 mine = revisionOf(state());
		for (int index = 0; index < link::MaxPeers + 9; ++index)
		{
			observe(packetFrom(QStringLiteral("peer-%1").arg(index), mine, 150.0));
		}
		QCOMPARE(state().value(QStringLiteral("peer_count")).toInt(), link::MaxPeers);

		// One announcement from a peer that went quiet long enough to expire.
		link::LinkPeerPacket stale = packetFrom(QStringLiteral("peer-quiet"), mine, 150.0);
		stale.sentUs = LinkSyncEngine::monotonicMicros()
			- static_cast<quint64>(link::PeerTimeoutMs * 4) * 1000;
		observe(stale, stale.sentUs);
		QVERIFY(sessionHasPeer(state(), QStringLiteral("peer-quiet")));
		LinkSyncEngine::instance()->service();
		QVERIFY(!sessionHasPeer(state(), QStringLiteral("peer-quiet")));
	}

	//! The announcement codec: our own packets round trip, and anything this
	//! model cannot read is IGNORED rather than misread - a foreign producer (a
	//! future real-Link transport) must not be parsed as one of ours.
	void packetsRoundTripAndForeignOnesAreIgnored()
	{
		const link::LinkPeerPacket original = packetFrom(QStringLiteral("peer-codec"), 7, 128.5);
		link::LinkPeerPacket decoded;
		QVERIFY(LinkSyncEngine::decodePacket(LinkSyncEngine::encodePacket(original), &decoded));
		QCOMPARE(decoded.id, original.id);
		QCOMPARE(decoded.revision, 7u);
		QCOMPARE(decoded.tempo, 128.5);
		QCOMPARE(decoded.sentUs, original.sentUs);

		const QByteArray ours = LinkSyncEngine::encodePacket(original);
		QVERIFY(!LinkSyncEngine::decodePacket(QByteArray("not json at all"), &decoded));
		QVERIFY(!LinkSyncEngine::decodePacket(QByteArray(), &decoded));
		QVERIFY(!LinkSyncEngine::decodePacket(ours, nullptr));
		QJsonObject foreign = QJsonDocument::fromJson(ours).object();
		foreign.insert(QStringLiteral("model"), QStringLiteral("ableton-link"));
		QVERIFY2(!LinkSyncEngine::decodePacket(QJsonDocument(foreign).toJson(QJsonDocument::Compact),
			&decoded), "an announcement from another producer was read as one of ours");
		foreign.insert(QStringLiteral("model"), QString::fromLatin1(link::SyncModelName));
		foreign.insert(QStringLiteral("proto"), link::ProtoVersion + 1);
		QVERIFY2(!LinkSyncEngine::decodePacket(QJsonDocument(foreign).toJson(QJsonDocument::Compact),
			&decoded), "an announcement from a future proto was read as this one");
		foreign.insert(QStringLiteral("proto"), link::ProtoVersion);
		foreign.remove(QStringLiteral("id"));
		QVERIFY(!LinkSyncEngine::decodePacket(QJsonDocument(foreign).toJson(QJsonDocument::Compact),
			&decoded));
	}

	//! Every refusal is typed and changes nothing: the quantum range, the tempo
	//! range, an unparseable bpm, and the one state refusal (declaring a session
	//! tempo with no session joined, which would reach nobody).
	void refusalsAreTypedAndChangeNothing()
	{
		setEnabledForTest(true);
		const QJsonObject before = state();

		const ControlResult low = run(QStringLiteral("link.set_quantum"),
			{{QStringLiteral("quantum"), 0}});
		QCOMPARE(low.ok, false);
		QCOMPARE(low.errorKind, ControlErrorKind::InvalidArgs);
		const ControlResult high = run(QStringLiteral("link.set_quantum"),
			{{QStringLiteral("quantum"), link::MaxQuantum + 1}});
		QCOMPARE(high.errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(state().value(QStringLiteral("quantum")).toInt(),
			before.value(QStringLiteral("quantum")).toInt());

		const ControlResult slow = run(QStringLiteral("link.set_session_tempo"),
			{{QStringLiteral("bpm"), 1.0}});
		QCOMPARE(slow.errorKind, ControlErrorKind::InvalidArgs);
		const ControlResult fast = run(QStringLiteral("link.set_session_tempo"),
			{{QStringLiteral("bpm"), 5000.0}});
		QCOMPARE(fast.errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(state().value(QStringLiteral("session_tempo")).toDouble(),
			before.value(QStringLiteral("session_tempo")).toDouble());

		// No arguments at all: the schema refuses before the handler runs.
		for (const QString& id : {QStringLiteral("link.set_enabled"),
				QStringLiteral("link.set_quantum"), QStringLiteral("link.set_start_stop_sync"),
				QStringLiteral("link.set_session_tempo")})
		{
			const ControlResult missing = run(id);
			QCOMPARE(missing.ok, false);
			QCOMPARE(missing.errorKind, ControlErrorKind::InvalidArgs);
			QVERIFY(!missing.errorMessage.isEmpty());
		}

		LinkSyncEngine::instance()->setEnabled(false);
		const ControlResult noSession = run(QStringLiteral("link.set_session_tempo"),
			{{QStringLiteral("bpm"), 140.0}});
		QCOMPARE(noSession.ok, false);
		QCOMPARE(noSession.errorKind, ControlErrorKind::Refused);
		QVERIFY(noSession.errorMessage.contains(QStringLiteral("transport.set_tempo")));
	}

	/*! The A16 proof: the recorded action step is what puts the previous value
	 *  back, and control.undo - the same path a user's Ctrl+Z takes - is what
	 *  runs it. Three commands, each undone and read back.
	 */
	void theGroupIsReversibleThroughTheRecordedStep()
	{
		LinkSyncEngine::instance()->setEnabled(false);
		QVERIFY(run(QStringLiteral("link.set_enabled"), {{QStringLiteral("enabled"), true}}).ok);
		QVERIFY(run(QStringLiteral("link.set_quantum"),
			{{QStringLiteral("quantum"), 8}}).ok);
		QCOMPARE(state().value(QStringLiteral("quantum")).toInt(), 8);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(state().value(QStringLiteral("quantum")).toInt(), link::DefaultQuantum);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(state().value(QStringLiteral("enabled")).toBool(), false);
	}

	//! The class the handlers claim is the class the contract stamps, and the
	//! contract rows exist for all five ids.
	void contractRowsClassifyTheGroup()
	{
		for (const QString& id : {QStringLiteral("link.set_enabled"),
				QStringLiteral("link.set_quantum"), QStringLiteral("link.set_start_stop_sync"),
				QStringLiteral("link.set_session_tempo")})
		{
			const control::ReversibilityEntry* row = contractRow(id);
			QVERIFY2(row != nullptr, qPrintable(id + " has no contract row"));
			QCOMPARE(control::reversibilityClassName(row->cls), QStringLiteral("true_inverse"));
			QVERIFY(row->reversible);
			QVERIFY(!row->reason.isEmpty());
			QVERIFY(!row->mechanism.isEmpty());
		}
		const control::ReversibilityEntry* listed = contractRow(QStringLiteral("link.get_state"));
		QVERIFY(listed != nullptr);
		QCOMPARE(control::reversibilityClassName(listed->cls), QStringLiteral("not_mutating"));

		// Driven THROUGH THE REGISTRY, because the transaction this asserts on
		// is the registry's own record of the command - not the model's state.
		QVERIFY(run(QStringLiteral("link.set_enabled"), {{QStringLiteral("enabled"), false}}).ok);
		const ControlResult on = run(QStringLiteral("link.set_enabled"),
			{{QStringLiteral("enabled"), true}});
		QVERIFY2(on.ok, qPrintable(on.errorMessage));
		const ControlRegistry::Transaction* tx = ControlRegistry::instance()->lastTransaction();
		QVERIFY(tx != nullptr);
		QCOMPARE(tx->command, QStringLiteral("link.set_enabled"));
		QCOMPARE(tx->cls, QStringLiteral("true_inverse"));
		QCOMPARE(tx->reversible, true);
		QVERIFY2(tx->mechanism.contains(QStringLiteral("action checkpoint")),
			qPrintable(tx->mechanism));
		QCOMPARE(tx->inverse.value(QStringLiteral("op")).toString(),
			QStringLiteral("link.set_enabled"));
		QCOMPARE(tx->inverse.value(QStringLiteral("args")).toObject()
			.value(QStringLiteral("enabled")).toBool(), false);
		LinkSyncEngine::instance()->setEnabled(false);
	}

private:
	//! Enabled for the length of one model test, so the peer table is this
	//! test's own business and not another fixture's.
	void setEnabledForTest(bool enabled)
	{
		LinkSyncEngine::instance()->setEnabled(enabled);
	}

	//! One announcement, stamped the way the transport stamps it.
	void observe(const link::LinkPeerPacket& packet, quint64 receivedUs = 0)
	{
		LinkSyncEngine::instance()->observePacket(packet,
			receivedUs == 0 ? LinkSyncEngine::monotonicMicros() : receivedUs);
	}
};

QTEST_GUILESS_MAIN(ControlLinkCommandsTest)
#include "ControlLinkCommandsTest.moc"
