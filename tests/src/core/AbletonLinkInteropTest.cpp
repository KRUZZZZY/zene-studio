/*
 * AbletonLinkInteropTest.cpp - Zene's engine in a REAL Ableton Link session,
 *                              against a peer built from Link's own API.
 *
 * THE CLAIM: with sync on, Zene is an Ableton Link participant. A Link peer -
 * here an `ableton::Link` object constructed by this test from the vendored
 * library, which is what Ableton Live, Bitwig and every Link app embed - and
 * Zene share a session: the peer's tempo becomes the engine's tempo (and is not
 * an undo step), Zene's tempo becomes the peer's, Zene's play head locks to the
 * session's beat phase while playing, and with start/stop sync on both sides a
 * start or a stop on either side reaches the other.
 *
 * HOW. The test is the engine's audio thread: the dummy device's render thread
 * is stopped and every period is rendered here with Song::processNextBuffer(),
 * paced to the wall clock (Link's clock is the wall clock, so a period rendered
 * early or late would be measured as a phase error that the engine did not
 * make). The UI-thread half (the link.* service tick) runs from this thread's
 * event loop between periods - the same thread split the product has, collapsed
 * onto one thread.
 *
 * WHAT IT CANNOT PROVE: interoperation with a specific third-party application.
 * There is no Ableton Live on this box; the peer is the reference library those
 * applications embed. And both participants share one host clock, so Link's
 * cross-machine clock synchronisation is exercised only in its trivial case.
 *
 * MULTICAST. As in AbletonLinkSessionTest: a host that cannot deliver a multicast
 * datagram between two sockets on Link's group skips, naming the measurement; a
 * host that can and still forms no session fails.
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

#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>

#include <ableton/Link.hpp>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "LinkPeerTransport.h"
#include "LinkSync.h"
#include "ProjectJournal.h"
#include "Song.h"

using namespace lmms;

namespace
{

constexpr double Quantum = 4.0;
constexpr int DiscoveryTimeoutMs = 8000;
constexpr int PropagationTimeoutMs = 4000;
//! Zene's beat is a quarter note: DefaultTicksPerBar / 4 ticks.
constexpr double TicksPerBeat = DefaultTicksPerBar / 4.0;
/*! The phase tolerance, stated before it was measured: 0.02 beats, which is
 *  10 ms at 120 BPM - under one 256-frame period at 44.1 kHz plus the wall-clock
 *  jitter of a test thread that sleeps between periods. */
constexpr double PhaseToleranceBeats = 0.02;

ControlResult run(const QString& id, const QJsonObject& args = QJsonObject())
{
	return ControlRegistry::instance()->invoke(id, args);
}

QJsonObject linkState()
{
	return run(QStringLiteral("link.get_state")).result;
}

//! Link carries a tempo as whole microseconds per beat, so a tempo that crossed
//! the network is exact only to within bpm^2 * 0.5 / 60e6 (link/Tempo.hpp).
bool sameTempo(double a, double b)
{
	return std::fabs(a - b) <= b * b * 0.5 / 60e6 * 1.000001;
}

double wrapSigned(double value, double period)
{
	return value - period * std::floor(value / period + 0.5);
}

//! The engine's play head in beats, including the frames into the current tick.
double engineBeats()
{
	Song* song = Engine::getSong();
	const Timeline& timeline = song->getTimeline();
	const double framesPerTick = static_cast<double>(Engine::framesPerTick());
	return (static_cast<double>(timeline.ticks()) + timeline.frameOffset() / framesPerTick) / TicksPerBeat;
}

/*! Real-time-paced rendering: a period is rendered when the wall clock says it
 *  is due, and the event loop (the link.* service tick) runs in between, until
 *  \a done or \a timeoutMs. */
class Pump
{
public:
	Pump() { m_clock.start(); }

	bool until(const std::function<bool()>& done, int timeoutMs)
	{
		QElapsedTimer limit;
		limit.start();
		while (!done())
		{
			if (limit.elapsed() > timeoutMs) { return false; }
			step();
		}
		return true;
	}

	void forMs(int ms) { until([] { return false; }, ms); }

	//! Render every period that is due, then let the event loop run.
	void step()
	{
		renderDue();
		QCoreApplication::processEvents();
		QThread::usleep(300);
	}

	//! Wait for the next period to be due WITHOUT rendering it.
	void waitForNextPeriod()
	{
		while (m_rendered + periodFrames() > dueFrames())
		{
			QCoreApplication::processEvents();
			QThread::usleep(100);
		}
	}

	void renderOne()
	{
		Engine::getSong()->processNextBuffer();
		m_rendered += periodFrames();
	}

private:
	static qint64 periodFrames() { return Engine::audioEngine()->framesPerPeriod(); }
	qint64 dueFrames() const
	{
		return m_clock.nsecsElapsed() * static_cast<qint64>(Engine::audioEngine()->outputSampleRate())
			/ 1000000000;
	}
	void renderDue()
	{
		while (m_rendered + periodFrames() <= dueFrames()) { renderOne(); }
	}

	QElapsedTimer m_clock;
	qint64 m_rendered = 0;
};

} // namespace

class AbletonLinkInteropTest : public QObject
{
	Q_OBJECT
private slots:
	void initTestCase()
	{
		std::unique_ptr<LinkPeerTransport> probe(createLinkUdpTransport(nullptr));
		probe->start();
		const LinkPeerTransport::LoopbackProbe measured = probe->loopbackProbe();
		probe->stop();
		if (!measured.delivered)
		{
			m_multicastReason = QStringLiteral("this host did not deliver a multicast datagram on "
				"224.76.78.75:20808 between two sockets (elapsed_ms=%1): %2")
				.arg(measured.elapsedMs).arg(probe->reason());
		}
		Engine::init(true);
		// This test IS the audio thread (file header): the dummy device must not
		// render the same song concurrently.
		Engine::audioEngine()->audioDev()->stopProcessing();
		ControlRegistry::setReady(true);
		QVERIFY(run(QStringLiteral("transport.set_tempo"), {{QStringLiteral("bpm"), 120}}).ok);
		m_peer = std::make_unique<ableton::Link>(120.0);
		m_peer->enable(true);
	}

	void cleanupTestCase()
	{
		run(QStringLiteral("link.set_enabled"), {{QStringLiteral("enabled"), false}});
		if (m_peer) { m_peer->enable(false); }
		m_peer.reset();
		LinkSyncEngine::destroy();
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! Each test starts and ends with the transport stopped, so a failure in one
	//! cannot leave the next one playing.
	void cleanup()
	{
		Engine::getSong()->stop();
		Pump().forMs(100);
	}

	//! Sync on: Zene is a member of the peer's Link session, and says so.
	void zeneJoinsTheLinkSession()
	{
		joinSession();
		const QJsonObject state = linkState();
		QCOMPARE(state.value(QStringLiteral("model")).toString(), QStringLiteral("ableton-link"));
		QVERIFY(state.value(QStringLiteral("peer_count")).toInt() >= 1);
		const QJsonObject interop = state.value(QStringLiteral("interop")).toObject();
		QCOMPARE(interop.value(QStringLiteral("ableton_link")).toBool(), true);
		QCOMPARE(interop.value(QStringLiteral("version")).toString(), QStringLiteral("4.1"));
	}

	//! A peer's tempo becomes the engine's tempo - and is NOT an undo step,
	//! while a local transport.set_tempo still is (the control that shows the
	//! journal check can see a step at all).
	void aPeersTempoReachesTheEngineAndIsNotAnUndoStep()
	{
		joinSession();
		Engine::projectJournal()->clearJournal();
		setPeerTempo(133.0);
		Pump pump;
		QVERIFY2(pump.until([] { return Engine::getSong()->getTempo() == 133; }, PropagationTimeoutMs),
			qPrintable(QStringLiteral("the engine still plays %1 BPM").arg(Engine::getSong()->getTempo())));
		const QJsonObject state = linkState();
		QVERIFY(sameTempo(state.value(QStringLiteral("session_tempo")).toDouble(), 133.0));
		QCOMPARE(state.value(QStringLiteral("tempo_is_local")).toBool(), false);
		QVERIFY2(!Engine::projectJournal()->canUndo(), "a peer's tempo was recorded as an undo step");

		QVERIFY(run(QStringLiteral("transport.set_tempo"), {{QStringLiteral("bpm"), 130}}).ok);
		QVERIFY2(Engine::projectJournal()->canUndo(), "control: a local tempo change records no step either");
	}

	//! Zene's tempo becomes the peer's: an ordinary transport.set_tempo, and an
	//! explicit fractional link.set_session_tempo (rounded in the engine, exact
	//! in the session).
	void zenesTempoReachesThePeer()
	{
		joinSession();
		QVERIFY(run(QStringLiteral("transport.set_tempo"), {{QStringLiteral("bpm"), 97}}).ok);
		Pump pump;
		QVERIFY2(pump.until([this] { return sameTempo(peerTempo(), 97.0); }, PropagationTimeoutMs),
			qPrintable(QStringLiteral("the peer still reads %1 BPM").arg(peerTempo())));

		QVERIFY(run(QStringLiteral("link.set_session_tempo"), {{QStringLiteral("bpm"), 101.5}}).ok);
		QVERIFY2(pump.until([this] { return sameTempo(peerTempo(), 101.5); }, PropagationTimeoutMs),
			qPrintable(QStringLiteral("the peer still reads %1 BPM").arg(peerTempo())));
		QCOMPARE(static_cast<int>(Engine::getSong()->getTempo()), 102);
	}

	/*! THE PHASE PROOF. At a fractional session tempo (120.5 BPM - the engine's
	 *  own tempo is an integer, so this cannot pass by the two simply running at
	 *  the same rate), Zene plays and its play head is compared, period by
	 *  period, with the PEER's own phase at the same host time. */
	void thePlayingEngineLocksToTheSessionPhase()
	{
		joinSession();
		setPeerTempo(120.5);
		Pump pump;
		QVERIFY(pump.until([] { return Engine::getSong()->getTempo() == 121; }, PropagationTimeoutMs));
		Engine::getSong()->playSong();
		pump.forMs(3000);   // the count-in to the session's phase, then the lock
		QVERIFY2(Engine::getSong()->isPlaying(), "the transport stopped on its own");

		const double latencyUs = static_cast<double>(Engine::audioEngine()->framesPerPeriod()
			+ Engine::audioEngine()->audioDev()->outputLatencyFrames())
			* 1e6 / Engine::audioEngine()->outputSampleRate();
		const double startBeats = engineBeats();
		QElapsedTimer window;
		window.start();
		double worst = 0.0;
		for (int period = 0; period < 200; ++period)
		{
			pump.waitForNextPeriod();
			const auto host = m_peer->clock().micros() + std::chrono::microseconds(std::llround(latencyUs));
			const double sessionPhase = m_peer->captureAppSessionState().phaseAtTime(host, Quantum);
			const double error = wrapSigned(sessionPhase - std::fmod(engineBeats(), Quantum), Quantum);
			worst = std::max(worst, std::fabs(error));
			pump.renderOne();
		}
		const double advanced = engineBeats() - startBeats;
		const double expected = window.nsecsElapsed() / 1e9 * 120.5 / 60.0;
		qInfo("worst phase error %.5f beats over 200 periods; advanced %.4f beats, wall clock %.4f",
			worst, advanced, expected);
		QVERIFY2(worst < PhaseToleranceBeats, qPrintable(QStringLiteral("worst phase error %1 beats").arg(worst)));
		QVERIFY2(std::fabs(advanced - expected) < 0.02 * expected,
			qPrintable(QStringLiteral("the play head advanced %1 beats where the session advanced %2")
				.arg(advanced).arg(expected)));
		Engine::getSong()->stop();
		pump.forMs(200);
	}

	//! With start/stop sync on BOTH sides, a start and a stop travel each way.
	void startAndStopTravelWhenShared()
	{
		joinSession();
		QVERIFY(run(QStringLiteral("link.set_start_stop_sync"), {{QStringLiteral("start_stop_sync"), true}}).ok);
		m_peer->enableStartStopSync(true);
		Song* song = Engine::getSong();
		Pump pump;
		pump.forMs(300);
		QVERIFY(!song->isPlaying());

		setPeerPlaying(true);
		QVERIFY2(pump.until([song] { return song->isPlaying(); }, PropagationTimeoutMs), "the peer's start never started Zene");
		setPeerPlaying(false);
		QVERIFY2(pump.until([song] { return !song->isPlaying(); }, PropagationTimeoutMs), "the peer's stop never stopped Zene");

		QVERIFY(run(QStringLiteral("transport.play")).ok);
		QVERIFY2(pump.until([this] { return peerPlaying(); }, PropagationTimeoutMs), "Zene's start never reached the peer");
		QVERIFY(run(QStringLiteral("transport.stop")).ok);
		QVERIFY2(pump.until([this] { return !peerPlaying(); }, PropagationTimeoutMs), "Zene's stop never reached the peer");
	}

	//! With start/stop sync OFF on Zene's side, a peer's start stays the peer's.
	void startStaysLocalWhenNotShared()
	{
		joinSession();
		QVERIFY(run(QStringLiteral("link.set_start_stop_sync"), {{QStringLiteral("start_stop_sync"), false}}).ok);
		m_peer->enableStartStopSync(true);
		Pump pump;
		setPeerPlaying(true);
		pump.forMs(1500);
		QVERIFY2(!Engine::getSong()->isPlaying(), "Zene started although it does not share start/stop");
		setPeerPlaying(false);
		pump.forMs(200);
	}

	//! Leaving the session keeps the tempo the engine was playing.
	void leavingKeepsTheTempo()
	{
		joinSession();
		const int tempo = Engine::getSong()->getTempo();
		QVERIFY(run(QStringLiteral("link.set_enabled"), {{QStringLiteral("enabled"), false}}).ok);
		Pump pump;
		QVERIFY2(pump.until([this] { return m_peer->numPeers() == 0; }, DiscoveryTimeoutMs),
			"the peer still counts Zene after it left");
		QCOMPARE(static_cast<int>(Engine::getSong()->getTempo()), tempo);
	}

private:
	void joinSession()
	{
		if (!m_multicastReason.isEmpty()) { QSKIP(qPrintable(m_multicastReason)); }
		QVERIFY(run(QStringLiteral("link.set_enabled"), {{QStringLiteral("enabled"), true}}).ok);
		Pump pump;
		QVERIFY2(pump.until([this] {
			return m_peer->numPeers() >= 1 && linkState().value(QStringLiteral("peer_count")).toInt() >= 1;
		}, DiscoveryTimeoutMs), qPrintable(QStringLiteral("no Link session formed in %1 ms (the peer counts %2)")
			.arg(DiscoveryTimeoutMs).arg(m_peer->numPeers())));
	}

	void setPeerTempo(double bpm)
	{
		auto state = m_peer->captureAppSessionState();
		state.setTempo(bpm, m_peer->clock().micros());
		m_peer->commitAppSessionState(state);
	}

	void setPeerPlaying(bool playing)
	{
		auto state = m_peer->captureAppSessionState();
		state.setIsPlayingAndRequestBeatAtTime(playing, m_peer->clock().micros(), 0.0, Quantum);
		m_peer->commitAppSessionState(state);
	}

	double peerTempo() const { return m_peer->captureAppSessionState().tempo(); }
	bool peerPlaying() const { return m_peer->captureAppSessionState().isPlaying(); }

	QString m_multicastReason;
	std::unique_ptr<ableton::Link> m_peer;
};

QTEST_GUILESS_MAIN(AbletonLinkInteropTest)
#include "AbletonLinkInteropTest.moc"
