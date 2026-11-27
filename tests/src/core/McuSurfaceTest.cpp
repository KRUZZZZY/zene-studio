/*
 * McuSurfaceTest.cpp - the Mackie Control protocol and a surface driving real tracks
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

/*! WF-126 (0.5.0 L13): the Mackie Control protocol tables (McuProtocol) - every input kind it
 *  decodes and the feedback it encodes - and a surface (McuSurface) on the engine's MIDI client
 *  driving real tracks: a fader sets a track's volume, mute runs track.set_mute (one undo
 *  takes it back), the bank buttons page, and disconnecting leaves nothing behind. */

#include <QtTest>

#include <QScopeGuard>
#include <QTemporaryDir>

#include "AudioEngine.h"
#include "AutomatableModel.h"
#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "McuProtocol.h"
#include "McuSurface.h"
#include "MultiTrackRecorder.h"
#include "Song.h"
#include "Track.h"
#include "TrackMixModels.h"

using namespace lmms;

class McuSurfaceTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void theProtocolDecodesEveryKind()
	{
		using mcu::Kind;
		const mcu::Input fader = mcu::decode(MidiEvent(MidiPitchBend, 2, 8192));
		QCOMPARE(fader.kind, Kind::Fader);
		QCOMPARE(fader.strip, 2);
		QCOMPARE(fader.value, 8192);
		QCOMPARE(mcu::decode(MidiEvent(MidiPitchBend, 8, 100)).strip, mcu::MasterStrip);
		QCOMPARE(mcu::decode(MidiEvent(MidiPitchBend, 9, 100)).kind, Kind::None);
		const mcu::Input mute = mcu::decode(MidiEvent(MidiNoteOn, 0, 17, 127));
		QCOMPARE(mute.kind, Kind::Mute);
		QCOMPARE(mute.strip, 1);
		QCOMPARE(mcu::decode(MidiEvent(MidiNoteOn, 0, 17, 0)).kind, Kind::None);  // a release
		QCOMPARE(mcu::decode(MidiEvent(MidiNoteOn, 0, 3, 127)).kind, Kind::Rec);
		QCOMPARE(mcu::decode(MidiEvent(MidiNoteOn, 0, 9, 127)).kind, Kind::Solo);
		QCOMPARE(mcu::decode(MidiEvent(MidiNoteOn, 0, 31, 127)).kind, Kind::Select);
		const mcu::Input released = mcu::decode(MidiEvent(MidiNoteOn, 0, 104, 0));
		QCOMPARE(released.kind, Kind::FaderTouch);
		QVERIFY(!released.pressed);
		const mcu::Input pot = mcu::decode(MidiEvent(MidiControlChange, 0, 16, 0x43));
		QCOMPARE(pot.kind, Kind::VPot);
		QCOMPARE(pot.value, -3);
		QCOMPARE(mcu::decode(MidiEvent(MidiControlChange, 0, 60, 0x05)).value, 5);
		QCOMPARE(mcu::decode(MidiEvent(MidiNoteOn, 0, 94, 127)).kind, Kind::Play);
		QCOMPARE(mcu::decode(MidiEvent(MidiNoteOn, 0, 47, 127)).kind, Kind::BankRight);
		QCOMPARE(mcu::decode(MidiEvent(MidiNoteOn, 0, 60, 127)).kind, Kind::None);
	}

	void theFeedbackIsTheSameNotesAndTheFaders()
	{
		QCOMPARE(mcu::faderPosition(1.f), mcu::FaderMax);
		QCOMPARE(mcu::faderPosition(0.f), 0);
		QCOMPARE(mcu::faderLevel(mcu::faderPosition(0.5f)), mcu::faderLevel(8192));
		const MidiEvent led = mcu::ledEvent(mcu::Kind::Mute, 3, true);
		QCOMPARE(led.type(), MidiNoteOn);
		QCOMPARE(led.key(), 19);
		QCOMPARE(static_cast<int>(led.velocity()), 127);
		QCOMPARE(static_cast<int>(mcu::ledEvent(mcu::Kind::Play, -1, false).velocity()), 0);
		QCOMPARE(mcu::ledEvent(mcu::Kind::Play, -1, false).key(), 94);
		const MidiEvent motor = mcu::faderEvent(mcu::MasterStrip, 100);
		QCOMPARE(motor.type(), MidiPitchBend);
		QCOMPARE(static_cast<int>(motor.channel()), mcu::MasterStrip);
		QCOMPARE(static_cast<int>(motor.pitchBend()), 100);
	}

	void aSurfaceDrivesTheSongsTracks()
	{
		Song* song = Engine::getSong();
		QList<Track*> made;
		for (int i = 0; i < 3; ++i) { made.append(Track::create(Track::Type::Instrument, song)); }
		McuSurface* surface = McuSurface::connect(QString(), QString());
		QVERIFY(surface != nullptr);
		QCOMPARE(McuSurface::instance(), surface);
		Track* first = surface->stripTrack(0);
		QVERIFY(first != nullptr);

		FloatModel* volume = trackVolumeModel(first);
		surface->apply({mcu::Kind::Fader, 0, mcu::FaderMax, false});
		QCOMPARE(volume->value(), volume->maxValue());
		surface->apply({mcu::Kind::Fader, 0, 0, false});
		QCOMPARE(volume->value(), volume->minValue());

		QVERIFY(!first->isMuted());
		surface->apply({mcu::Kind::Mute, 0, 0, true});
		QVERIFY(first->isMuted());
		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("control.undo")).ok);
		QVERIFY2(!first->isMuted(), "the surface's mute was not one undo step");

		const int tracks = static_cast<int>(surface->state().value(QStringLiteral("strips")).toArray().size());
		QCOMPARE(tracks, mcu::Strips);
		surface->apply({mcu::Kind::ChannelRight, -1, 0, true});
		QCOMPARE(surface->bank(), 1);
		QVERIFY(surface->stripTrack(0) != first);
		surface->apply({mcu::Kind::BankLeft, -1, 0, true});
		QCOMPARE(surface->bank(), 0);
		// surface.mcu_bank pages as the buttons do.
		const ControlResult paged = ControlRegistry::instance()->invoke(QStringLiteral("surface.mcu_bank"), {{QStringLiteral("bank"), 1}});
		QVERIFY2(paged.ok, qPrintable(paged.errorMessage));
		QCOMPARE(paged.result.value(QStringLiteral("bank")).toInt(), 1);
		QCOMPARE(surface->bank(), 1);
		QVERIFY(surface->stripTrack(0) != first);

		McuSurface::disconnect();
		QCOMPARE(ControlRegistry::instance()->invoke(QStringLiteral("surface.mcu_bank"), {{QStringLiteral("bank"), 0}}).errorKind,
			ControlErrorKind::NotFound);
		QVERIFY(McuSurface::instance() == nullptr);
		qDeleteAll(made);
	}

	//! Review W2a-1 #4/#7: with nine tracks and more, strip 8 is still the master fader.
	void theMasterFaderIsTheMasterWhateverTheBank()
	{
		Song* song = Engine::getSong();
		QList<Track*> made;
		for (int i = 0; i < 10; ++i) { made.append(Track::create(Track::Type::Instrument, song)); }
		McuSurface* surface = McuSurface::connect(QString(), QString());
		QVERIFY(surface->stripTrack(mcu::MasterStrip) == nullptr);
		FloatModel* ninth = trackVolumeModel(made.at(8));
		const float before = ninth->value();
		surface->apply({mcu::Kind::Fader, mcu::MasterStrip, 0, false});
		QCOMPARE(song->masterVolumeModel().value(), song->masterVolumeModel().minValue());
		QCOMPARE(ninth->value(), before);
		surface->apply({mcu::Kind::Fader, mcu::MasterStrip, mcu::FaderMax, false});
		QCOMPARE(song->masterVolumeModel().value(), song->masterVolumeModel().maxValue());
		McuSurface::disconnect();
		qDeleteAll(made);
	}

	//! The bank follows the track list: deleting tracks past it shows tracks again, not eight blanks.
	void theBankFollowsTheTrackList()
	{
		Song* song = Engine::getSong();
		QList<Track*> made;
		for (int i = 0; i < 3; ++i) { made.append(Track::create(Track::Type::Instrument, song)); }
		McuSurface* surface = McuSurface::connect(QString(), QString());
		QVERIFY(surface != nullptr);
		surface->setBank(2);
		QCOMPARE(surface->bank(), 2);
		QCOMPARE(surface->stripTrack(0), made.at(2));
		delete made.takeLast();  // the bank does not move by itself
		QCOMPARE(static_cast<int>(song->tracks().size()), 2);
		QVERIFY2(surface->stripTrack(0) != nullptr, "every strip went blank after a track was deleted");
		// as setBank clamps, the bank falls back to the last track the song still has
		QCOMPARE(surface->stripTrack(0), made.at(1));
		QCOMPARE(surface->state().value(QStringLiteral("bank")).toInt(), 1);
		McuSurface::disconnect();
		qDeleteAll(made);
	}

	void invalidFaderTouchDoesNotCorruptTheBank()
	{
		McuSurface* surface = McuSurface::connect(QString(), QString());
		surface->apply({mcu::Kind::FaderTouch, -1, 0, true});
		const int bank = surface->bank();
		McuSurface::disconnect();
		QCOMPARE(bank, 0);
	}

	void invalidFaderTouchDoesNotCorruptFeedbackState()
	{
		Song* song = Engine::getSong();
		Track* track = Track::create(Track::Type::Instrument, song);
		McuSurface* surface = McuSurface::connect(QString(), QString());
		FloatModel* volume = trackVolumeModel(track);
		volume->setValue(volume->maxValue());
		QTRY_VERIFY(surface->state().value(QStringLiteral("events_out")).toInt() > 0);
		QTest::qWait(80);
		const int sent = surface->state().value(QStringLiteral("events_out")).toInt();

		surface->apply({mcu::Kind::FaderTouch, 12, 0, true});
		QTest::qWait(80);
		const int afterInvalidInput = surface->state().value(QStringLiteral("events_out")).toInt();
		McuSurface::disconnect();
		qDeleteAll(QList<Track*>{track});
		QCOMPARE(afterInvalidInput, sent);
	}

	void bankButtonsStayOnEightTrackPageBoundaries()
	{
		Song* song = Engine::getSong();
		QList<Track*> made;
		for (int i = 0; i < 10; ++i) { made.append(Track::create(Track::Type::Instrument, song)); }
		McuSurface* surface = McuSurface::connect(QString(), QString());
		for (int i = 0; i < mcu::Strips; ++i) { surface->apply({mcu::Kind::ChannelRight, -1, 0, true}); }
		const int beforeBankRight = surface->bank();
		surface->apply({mcu::Kind::BankRight, -1, 0, true});
		const int afterBankRight = surface->bank();
		surface->apply({mcu::Kind::BankLeft, -1, 0, true});
		const int afterBankLeft = surface->bank();
		for (int i = 0; i < 9; ++i) { surface->apply({mcu::Kind::ChannelRight, -1, 0, true}); }
		const int afterChannelRight = surface->bank();
		McuSurface::disconnect();
		qDeleteAll(made);

		QCOMPARE(beforeBankRight, mcu::Strips);
		QCOMPARE(afterBankRight, mcu::Strips);
		QCOMPARE(afterBankLeft, 0);
		QCOMPARE(afterChannelRight, 9);
	}

	/*! Cluster 4736: the surface's REC button follows the TRACK, not the row it stands on. A track
	 *  that moved onto another track's armed row must not claim (and therefore stop, un-arm or
	 *  re-arm) that capture, and the track whose row moved must still stop its OWN capture. The
	 *  surface asks the same query the window's arm button does. */
	void recFollowsTheTrackWhenAnotherTrackMovesOntoItsRow()
	{
		QTemporaryDir takes;
		QVERIFY(takes.isValid());
		const auto restoreDir = qScopeGuard([previous = ConfigManager::inst()->workingDir()] { ConfigManager::inst()->setWorkingDir(previous); });
		ConfigManager::inst()->setWorkingDir(takes.path());
		Song* song = Engine::getSong();
		QList<Track*> made;
		for (int i = 0; i < 2; ++i) { made.append(Track::create(Track::Type::Instrument, song)); }
		const auto dropTracks = qScopeGuard([&made] { qDeleteAll(made); made.clear(); });
		McuSurface* surface = McuSurface::connect(QString(), QString());
		QVERIFY(surface != nullptr);
		const auto disconnect = qScopeGuard([] { McuSurface::disconnect(); });
		MultiTrackRecorder& recorder = Engine::audioEngine()->recorder();
		const auto stopCaptures = qScopeGuard([&recorder] { recorder.disarmAll(); });

		Track* mover = made.at(0);
		Track* owner = made.at(1);
		const auto rowOf = [song](const Track* track) {
			const TrackContainer::TrackList& list = song->tracks();
			return static_cast<int>(std::find(list.begin(), list.end(), track) - list.begin());
		};
		QCOMPARE(rowOf(owner), rowOf(mover) + 1);
		const ControlResult armed = ControlRegistry::instance()->invoke(QStringLiteral("track.set_arm"),
			{{QStringLiteral("track"), control::trackIdOf(owner)}, {QStringLiteral("armed"), true}});
		QVERIFY2(armed.ok, qPrintable(armed.errorMessage));
		const int route = armed.result.value(QStringLiteral("route")).toInt();
		QVERIFY(route >= 0 && route < recorder.trackCount());
		QVERIFY(recorder.track(route).isArmed());

		// The track above moves onto the armed row; the armed track sits one row lower.
		const ControlResult moved = ControlRegistry::instance()->invoke(QStringLiteral("track.move"),
			{{QStringLiteral("track"), control::trackIdOf(mover)}, {QStringLiteral("index"), route}});
		QVERIFY2(moved.ok, qPrintable(moved.errorMessage));
		QCOMPARE(rowOf(mover), route);
		QCOMPARE(rowOf(owner), route - 1);
		QVERIFY(recorder.track(route).isArmed());

		int moverStrip = -1;
		int ownerStrip = -1;
		for (int strip = 0; strip < mcu::Strips; ++strip)
		{
			if (surface->stripTrack(strip) == mover) { moverStrip = strip; }
			if (surface->stripTrack(strip) == owner) { ownerStrip = strip; }
		}
		QVERIFY(moverStrip >= 0 && ownerStrip >= 0);

		// The row is not the mover's capture: its REC press must neither stop nor re-arm it.
		surface->apply({mcu::Kind::Rec, moverStrip, 0, true});
		QVERIFY2(recorder.track(route).isArmed(), "the mover's REC press stopped another track's capture");

		// The owner's row moved, but the capture is still its own: REC stops it.
		surface->apply({mcu::Kind::Rec, ownerStrip, 0, true});
		QVERIFY2(!recorder.track(route).isArmed(),
			"the surface read the owner's new row and never stopped its capture");
	}
};

QTEST_MAIN(McuSurfaceTest)
#include "McuSurfaceTest.moc"
