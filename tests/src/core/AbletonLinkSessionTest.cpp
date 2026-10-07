/*
 * AbletonLinkSessionTest.cpp - the vendored Ableton Link library, compiled into
 *                              this tree and driven through its one wrapper
 *                              (include/AbletonLinkSession.h).
 *
 * THE CLAIM: this build carries the pinned Link release, and two Link
 * participants in one process - which share a session on the same host exactly
 * as two applications do - discover each other, agree on tempo, beat phase and
 * start/stop, and the audio-thread half of the wrapper does not allocate.
 *
 * WHAT IT DOES NOT CLAIM: anything about Zene's engine; that is
 * AbletonLinkInteropTest's job. This file proves the library and the wrapper
 * under it.
 *
 * MULTICAST. Link discovers peers over UDP multicast (224.76.78.75:20808). A host
 * that cannot deliver a multicast datagram between two sockets cannot run this
 * test at all, so discovery is gated on a MEASUREMENT: the zene-link-style
 * transport's own two-socket loopback probe on that same group. A probe that is
 * not delivered is a skip that names the measurement; a delivered probe with no
 * Link peer is a failure.
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

#include <cmath>
#include <functional>
#include <memory>

#include "AbletonLinkSession.h"
#include "LinkPeerTransport.h"
#include "LinkSync.h"
// Last: it defines the program-wide operator new replacements (see its header).
#include "LinkRealtimeProbe.h"

using namespace lmms;

namespace
{

constexpr double Quantum = 4.0;
constexpr int DiscoveryTimeoutMs = 8000;
constexpr int PropagationTimeoutMs = 4000;
/*! Link carries a tempo as whole microseconds per beat (link/Tempo.hpp), so a
 *  tempo that crossed the network equals the one set only to within half a
 *  microsecond per beat: |error| <= bpm^2 * 0.5 / 60e6 (1.5e-4 BPM at 133.5,
 *  8.3e-3 at 999). This is that bound, not a tolerance chosen to pass. */
bool sameTempo(double a, double b)
{
	const double bound = b * b * 0.5 / 60e6;
	return std::fabs(a - b) <= bound * 1.000001;
}

//! Poll \a done every 20 ms for at most \a timeoutMs.
bool waitFor(const std::function<bool()>& done, int timeoutMs)
{
	QElapsedTimer clock;
	clock.start();
	while (!done())
	{
		if (clock.elapsed() > timeoutMs) { return false; }
		QTest::qWait(20);
	}
	return true;
}

//! a - b wrapped into (-period/2, period/2].
double phaseDistance(double a, double b, double period)
{
	return a - b - period * std::floor((a - b) / period + 0.5);
}

} // namespace

class AbletonLinkSessionTest : public QObject
{
	Q_OBJECT
private slots:
	void initTestCase()
	{
		// The measured precondition for every discovery test below (file header).
		std::unique_ptr<LinkPeerTransport> probe(createLinkUdpTransport(nullptr));
		probe->start();
		const LinkPeerTransport::LoopbackProbe measured = probe->loopbackProbe();
		probe->stop();
		m_multicastReason = measured.delivered ? QString()
			: QStringLiteral("this host did not deliver a multicast datagram on 224.76.78.75:20808 "
				"between two sockets (attempted=%1, elapsed_ms=%2, bound_ms=%3): %4")
				.arg(measured.attempted).arg(measured.elapsedMs).arg(measured.boundMs).arg(probe->reason());
	}

	//! The pin is the one README.lmms records, and the library is in this build.
	void libraryIsCompiledInAtThePinnedRelease()
	{
		QVERIFY(AbletonLinkSession::compiledIn());
		QCOMPARE(QString::fromLatin1(AbletonLinkSession::libraryVersion()), QStringLiteral("4.1"));
		QCOMPARE(QString::fromLatin1(AbletonLinkSession::libraryCommit()),
			QStringLiteral("9c9091275e707ab09d09a5a608fcdb84bf0dec85"));
	}

	//! Two participants discover each other and form ONE session.
	void twoParticipantsFormOneSession()
	{
		joinBoth();
		// At least one: a Link application elsewhere on the network is a legitimate
		// member of the same session, so the count is a lower bound, never exact.
		QVERIFY(m_a->numPeers() >= 1);
		QVERIFY(m_b->numPeers() >= 1);
	}

	//! A tempo set by either side reaches the other: the application-thread path
	//! one way, the audio-thread commit the other.
	void tempoTravelsBothWays()
	{
		joinBoth();
		m_a->appSetTempo(133.5);
		QVERIFY2(waitFor([this] { return sameTempo(m_b->appCapture(Quantum).tempo, 133.5); }, PropagationTimeoutMs),
			qPrintable(QStringLiteral("B still reads %1 BPM").arg(m_b->appCapture(Quantum).tempo)));

		LinkAudioChange change;
		change.tempo = 97.25;
		m_b->audioPeriod(m_b->clockMicros(), Quantum, change);
		QVERIFY2(waitFor([this] { return sameTempo(m_a->appCapture(Quantum).tempo, 97.25); }, PropagationTimeoutMs),
			qPrintable(QStringLiteral("A still reads %1 BPM").arg(m_a->appCapture(Quantum).tempo)));
	}

	//! Both sides read the same phase at the same host time: the shared beat grid.
	void phaseIsSharedAtTheSameHostTime()
	{
		joinBoth();
		// Let the clock offset estimate settle after the tempo changes above.
		QTest::qWait(300);
		const std::int64_t at = m_a->clockMicros() + 50000;
		const LinkTimelinePoint a = m_a->audioPeriod(at, Quantum, LinkAudioChange{});
		const LinkTimelinePoint b = m_b->audioPeriod(at, Quantum, LinkAudioChange{});
		const double error = phaseDistance(a.phase, b.phase, Quantum);
		qInfo("phase A=%.6f B=%.6f error=%.6f beats", a.phase, b.phase, error);
		QVERIFY2(std::fabs(error) < 0.005, qPrintable(QStringLiteral("phase error %1 beats").arg(error)));
		QVERIFY2(sameTempo(a.tempo, b.tempo), qPrintable(QStringLiteral("A %1 BPM, B %2 BPM").arg(a.tempo).arg(b.tempo)));
	}

	//! With start/stop sync on both sides, a start committed on the audio thread
	//! of one reaches the other, and so does the stop.
	void startStopTravelsWhenShared()
	{
		joinBoth();
		m_a->enableStartStopSync(true);
		m_b->enableStartStopSync(true);
		LinkAudioChange start;
		start.playing = 1;
		m_a->audioPeriod(m_a->clockMicros(), Quantum, start);
		QVERIFY2(waitFor([this] { return m_b->appCapture(Quantum).playing; }, PropagationTimeoutMs),
			"the start never reached B");
		LinkAudioChange stop;
		stop.playing = 0;
		m_b->audioPeriod(m_b->clockMicros(), Quantum, stop);
		QVERIFY2(waitFor([this] { return !m_a->appCapture(Quantum).playing; }, PropagationTimeoutMs),
			"the stop never reached A");
	}

	/*! The audio-thread half adds nothing Link does not promise: 64 periods of
	 *  capture + commit + host-time filtering, with a change committed every
	 *  eighth period, allocate nothing on this thread. */
	void audioPeriodDoesNotAllocate()
	{
		joinBoth();
		m_b->resetHostTimeFilter();
		const std::int64_t base = m_b->clockMicros();
		double beats = 0.0;
		const std::uint64_t allocations = lmms::test::link::allocationsDuring([&] {
			for (int period = 0; period < 64; ++period)
			{
				LinkAudioChange change;
				if (period % 8 == 0) { change.tempo = 120.0 + period; }
				const std::int64_t host = m_b->hostTimeAtSample(256.0 * period);
				beats += m_b->audioPeriod(host > 0 ? host : base, Quantum, change).beat;
			}
		});
		qInfo("allocations=%llu over 64 audio periods (beat sum %.3f)",
			static_cast<unsigned long long>(allocations), beats);
		QCOMPARE(allocations, std::uint64_t{0});
	}

	void cleanupTestCase()
	{
		m_a.reset();
		m_b.reset();
	}

private:
	//! Both participants enabled and in one session, or a skip that names the
	//! measurement that made discovery impossible on this host.
	void joinBoth()
	{
		if (!m_multicastReason.isEmpty()) { QSKIP(qPrintable(m_multicastReason)); }
		if (!m_a)
		{
			m_a = std::make_unique<AbletonLinkSession>(120.0);
			m_b = std::make_unique<AbletonLinkSession>(90.0);
			m_a->enable(true);
			m_b->enable(true);
		}
		QVERIFY2(waitFor([this] { return m_a->numPeers() >= 1 && m_b->numPeers() >= 1; }, DiscoveryTimeoutMs),
			qPrintable(QStringLiteral("the multicast probe was delivered, yet the two Link participants "
				"did not find each other in %1 ms (A sees %2, B sees %3)")
				.arg(DiscoveryTimeoutMs).arg(m_a->numPeers()).arg(m_b->numPeers())));
	}

	QString m_multicastReason;
	std::unique_ptr<AbletonLinkSession> m_a;
	std::unique_ptr<AbletonLinkSession> m_b;
};

QTEST_GUILESS_MAIN(AbletonLinkSessionTest)
#include "AbletonLinkSessionTest.moc"
