/*
 * McuSurfaceFeedbackTest.cpp - a Mackie Control surface's buttons, pots, transport and motor feedback
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

/*! McuSurfaceTest covers the protocol tables and the faders, mute, bank and master. This drives the rest of
 *  McuSurface on real tracks and the real song: the V-pot turns a track's pan, solo / select / rec run the
 *  same commands a click does, the transport buttons play, stop, record and seek (a bar for rewind and
 *  forward, a beat for a jog detent), events arriving on the MIDI client's thread reach the song through the
 *  lock-free inbox (and a burst past its 256 entries is counted as dropped), and the feedback timer sends
 *  the motor faders and LEDs - but never a fader the user is touching. */

#include <QtTest>

#include "AudioEngine.h"
#include "AutomatableModel.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "McuProtocol.h"
#include "McuSurface.h"
#include "MidiEvent.h"
#include "MultiTrackRecorder.h"
#include "Song.h"
#include "Track.h"
#include "TrackMixModels.h"

using namespace lmms;

namespace
{

mcu::Input press(mcu::Kind kind, int strip = -1, int value = 0) { return mcu::Input{kind, strip, value, true}; }

int eventsOut(const McuSurface* surface) { return surface->state().value(QStringLiteral("events_out")).toInt(); }
int eventsIn(const McuSurface* surface) { return surface->state().value(QStringLiteral("events_in")).toInt(); }

} // namespace

class McuSurfaceFeedbackTest : public QObject
{
	Q_OBJECT

	Song* m_song = nullptr;
	QList<Track*> m_tracks;
	McuSurface* m_surface = nullptr;

private slots:
	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
		m_song = Engine::getSong();
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void init()
	{
		m_tracks.clear();
		for (int i = 0; i < 3; ++i) { m_tracks.append(Track::create(Track::Type::Instrument, m_song)); }
		m_surface = McuSurface::connect(QString(), QString());
		QVERIFY(m_surface != nullptr);
	}

	void cleanup()
	{
		McuSurface::disconnect();
		qDeleteAll(m_tracks);
		m_tracks.clear();
	}

	void theVPotTurnsTheStripsPan()
	{
		FloatModel* pan = trackPanningModel(m_tracks.at(1));
		QVERIFY(pan != nullptr);
		const float before = pan->value();
		m_surface->apply(mcu::Input{mcu::Kind::VPot, 1, 3, false});
		QCOMPARE(pan->value(), before + 6.f);
		m_surface->apply(mcu::Input{mcu::Kind::VPot, 1, -2, false});
		QCOMPARE(pan->value(), before + 2.f);
		// Another strip's pot leaves this one alone.
		const float other = trackPanningModel(m_tracks.at(0))->value();
		m_surface->apply(mcu::Input{mcu::Kind::VPot, 1, 1, false});
		QCOMPARE(trackPanningModel(m_tracks.at(0))->value(), other);
		// A pot over a strip with no track does nothing and does not crash.
		m_surface->apply(mcu::Input{mcu::Kind::VPot, 6, 4, false});
	}

	void soloTogglesAndOneUndoTakesItBack()
	{
		Track* track = m_tracks.at(2);
		QVERIFY(!track->isSolo());
		m_surface->apply(press(mcu::Kind::Solo, 2));
		QVERIFY(track->isSolo());
		m_surface->apply(press(mcu::Kind::Solo, 2));
		QVERIFY(!track->isSolo());
		m_surface->apply(press(mcu::Kind::Solo, 2));
		QVERIFY(track->isSolo());
		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("control.undo")).ok);
		QVERIFY2(!track->isSolo(), "the surface's solo was not one undo step");
	}

	void recArmsTheTracksRecordRoute()
	{
		const MultiTrackRecorder& recorder = Engine::audioEngine()->recorder();
		const auto route = static_cast<int>(std::find(m_song->tracks().begin(), m_song->tracks().end(), m_tracks.at(0))
			- m_song->tracks().begin());
		QVERIFY2(route < recorder.trackCount(), "the recorder has no route for the first track");
		QVERIFY(!recorder.track(route).isArmed());
		m_surface->apply(press(mcu::Kind::Rec, 0));
		QVERIFY(recorder.track(route).isArmed());
		m_surface->apply(press(mcu::Kind::Rec, 0));
		QVERIFY2(!recorder.track(route).isArmed(), "a second press did not disarm");
	}

	void selectRunsWindowSelect()
	{
		// window.select needs a window; offscreen it answers a typed refusal rather than throwing, and the press
		// must neither crash nor change the song's tracks.
		const int count = static_cast<int>(m_song->tracks().size());
		m_surface->apply(press(mcu::Kind::Select, 1));
		QCOMPARE(static_cast<int>(m_song->tracks().size()), count);
	}

	void theTransportButtonsDriveTheSong()
	{
		m_surface->apply(press(mcu::Kind::Stop));
		QVERIFY(!m_song->isPlaying());
		m_song->setPlayPos(TimePos(0), Song::PlayMode::Song);
		const int bar = m_song->ticksPerBar();
		QVERIFY(bar > 0);

		m_surface->apply(press(mcu::Kind::Forward));
		QCOMPARE(static_cast<int>(m_song->getPlayPos().getTicks()), bar);
		m_surface->apply(press(mcu::Kind::Forward));
		QCOMPARE(static_cast<int>(m_song->getPlayPos().getTicks()), 2 * bar);
		m_surface->apply(press(mcu::Kind::Rewind));
		QCOMPARE(static_cast<int>(m_song->getPlayPos().getTicks()), bar);
		// A jog detent is a beat: a bar over the time signature's numerator.
		const int beat = bar / m_song->getTimeSigModel().getNumerator();
		m_surface->apply(press(mcu::Kind::Jog, -1, 2));
		QCOMPARE(static_cast<int>(m_song->getPlayPos().getTicks()), bar + 2 * beat);
		// Rewinding past the start stops at 0.
		m_surface->apply(press(mcu::Kind::Rewind));
		m_surface->apply(press(mcu::Kind::Rewind));
		m_surface->apply(press(mcu::Kind::Rewind));
		QCOMPARE(static_cast<int>(m_song->getPlayPos().getTicks()), 0);

		m_surface->apply(press(mcu::Kind::Play));
		QVERIFY2(m_song->isPlaying(), "the play button did not start the song");
		m_surface->apply(press(mcu::Kind::Stop));
		QVERIFY2(!m_song->isPlaying(), "the stop button did not stop the song");
	}

	void aRecordPressRunsTransportRecord()
	{
		m_surface->apply(press(mcu::Kind::Stop));
		m_surface->apply(press(mcu::Kind::Record));
		// transport.record starts the song recording or, with nothing armed, refuses typed; either way the
		// press leaves a consistent transport: not recording unless playing.
		QVERIFY(!m_song->isRecording() || m_song->isPlaying());
		m_surface->apply(press(mcu::Kind::Stop));
		QVERIFY(!m_song->isPlaying());
	}

	void anEventOnTheMidiThreadReachesTheSongThroughTheInbox()
	{
		Track* track = m_tracks.at(0);
		QVERIFY(!track->isMuted());
		const int before = eventsIn(m_surface);
		// Mute button of strip 0 (note 16, velocity 127 = pressed), as the MIDI client hands it over.
		m_surface->processInEvent(MidiEvent(MidiNoteOn, 0, 16, 127));
		QCOMPARE(eventsIn(m_surface), before + 1);
		// Not applied yet: the UI thread's timer drains it.
		QVERIFY(!track->isMuted());
		QTRY_VERIFY_WITH_TIMEOUT(track->isMuted(), 2000);
		// Something the protocol does not decode is not counted at all.
		m_surface->processInEvent(MidiEvent(MidiNoteOn, 0, 60, 127));
		QCOMPARE(eventsIn(m_surface), before + 1);
		// The surface's own output port ignores what it is handed.
		m_surface->processOutEvent(MidiEvent(MidiNoteOn, 0, 16, 127));
		QCOMPARE(eventsIn(m_surface), before + 1);
	}

	void aBurstPastTheQueueIsCountedAsDropped()
	{
		const int dropped0 = m_surface->state().value(QStringLiteral("events_dropped")).toInt();
		for (int i = 0; i < 300; ++i) { m_surface->processInEvent(MidiEvent(MidiPitchBend, 0, 100 + i, 0)); }
		QCOMPARE(eventsIn(m_surface), 300);
		const int dropped = m_surface->state().value(QStringLiteral("events_dropped")).toInt() - dropped0;
		QVERIFY2(dropped >= 300 - 256 && dropped < 300, qPrintable(QStringLiteral("dropped %1 of 300").arg(dropped)));
	}

	void theFeedbackSendsChangesAndNeverAFaderBeingTouched()
	{
		FloatModel* volume = trackVolumeModel(m_tracks.at(0));
		QVERIFY(volume != nullptr);
		// First pass sends every strip's state once; wait until it is quiet.
		QTRY_VERIFY_WITH_TIMEOUT(eventsOut(m_surface) > 0, 2000);
		QTest::qWait(150);
		const int settled = eventsOut(m_surface);
		QTest::qWait(150);
		QCOMPARE(eventsOut(m_surface), settled);  // nothing changed, nothing sent

		volume->setValue(volume->minValue());
		QTRY_VERIFY_WITH_TIMEOUT(eventsOut(m_surface) > settled, 2000);
		const int moved = eventsOut(m_surface);

		// A touched fader is the user's: the motor does not fight them.
		m_surface->apply(mcu::Input{mcu::Kind::FaderTouch, 0, 0, true});
		volume->setValue(volume->maxValue());
		QTest::qWait(200);
		QCOMPARE(eventsOut(m_surface), moved);
		// Released, the fader catches up.
		m_surface->apply(mcu::Input{mcu::Kind::FaderTouch, 0, 0, false});
		QTRY_VERIFY_WITH_TIMEOUT(eventsOut(m_surface) > moved, 2000);
	}

	void theMasterFaderAndTheLedsFollowTheSong()
	{
		QTRY_VERIFY_WITH_TIMEOUT(eventsOut(m_surface) > 0, 2000);
		QTest::qWait(150);
		const int settled = eventsOut(m_surface);
		IntModel& master = m_song->masterVolumeModel();
		master.setValue(master.minValue());
		QTRY_VERIFY_WITH_TIMEOUT(eventsOut(m_surface) > settled, 2000);
		const int afterMaster = eventsOut(m_surface);
		QTest::qWait(150);
		QCOMPARE(eventsOut(m_surface), afterMaster);
		master.setValue(master.maxValue());
		QTRY_VERIFY_WITH_TIMEOUT(eventsOut(m_surface) > afterMaster, 2000);

		// A mute lights the strip's LED: one more event, and no more while it stays muted.
		QTest::qWait(150);
		const int beforeLed = eventsOut(m_surface);
		m_tracks.at(1)->setMuted(true);
		QTRY_VERIFY_WITH_TIMEOUT(eventsOut(m_surface) > beforeLed, 2000);
		const int afterLed = eventsOut(m_surface);
		QTest::qWait(150);
		QCOMPARE(eventsOut(m_surface), afterLed);
	}

	void theStateNamesTheConnectedPortsAndStrips()
	{
		McuSurface::disconnect();
		m_surface = McuSurface::connect(QStringLiteral("in-port"), QStringLiteral("out-port"));
		const QJsonObject state = m_surface->state();
		QVERIFY(state.value(QStringLiteral("connected")).toBool());
		QCOMPARE(state.value(QStringLiteral("input")).toString(), QStringLiteral("in-port"));
		QCOMPARE(state.value(QStringLiteral("output")).toString(), QStringLiteral("out-port"));
		QCOMPARE(state.value(QStringLiteral("strips")).toArray().size(), mcu::Strips);
		QCOMPARE(state.value(QStringLiteral("bank")).toInt(), 0);
	}
};

QTEST_MAIN(McuSurfaceFeedbackTest)
#include "McuSurfaceFeedbackTest.moc"
