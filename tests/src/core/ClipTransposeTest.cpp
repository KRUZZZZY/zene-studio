/*
 * ClipTransposeTest.cpp - a clip's non-destructive transpose: the command, the file, the played pitch
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

/*! ED-038 (0.5.0 L04): clip.set_transpose on a MIDI clip - stored, saved as an attribute only
 *  when non-zero, one undo step - and PLAYED: the song renders through the real period pipeline
 *  (the device stopped, SessionTuningTest's pattern) and the note play handle the clip starts is
 *  an octave up while the note in the clip keeps its key. A sample clip is pitch-shifted through
 *  Rubber Band: the command sets that stretch mode, or is refused, typed, in a build without it. */

#include <QtTest>

#include <cmath>

#include <QDomDocument>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "ClipEdits.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "NotePlayHandle.h"
#include "RubberBandStretch.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "Song.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

NotePlayHandle* noteOf(const InstrumentTrack* track)
{
	for (PlayHandle* handle : Engine::audioEngine()->playHandles())
	{
		auto* note = dynamic_cast<NotePlayHandle*>(handle);
		if (note != nullptr && note->instrumentTrack() == track) { return note; }
	}
	return nullptr;
}

} // namespace

class ClipTransposeTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);  // a real instrument, so notes start (SessionTuningTest's pattern)
#endif
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();
		NotePlayHandleManager::init();
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::getSong()->clearProject();
		Engine::destroy();
	}

	void aTransposedMidiClipPlaysItsNotesTransposed()
	{
		Song* song = Engine::getSong();
		song->clearProject();
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, song));
		QVERIFY(track->loadInstrument(QStringLiteral("tripleoscillator")) != nullptr);
		auto* clip = dynamic_cast<MidiClip*>(track->createClip(TimePos(0)));
		clip->addNote(Note(TimePos(192), TimePos(0), 69), false);
		const QString id = control::clipIdOf(clip);

		const ControlResult set = run(QStringLiteral("clip.set_transpose"), {{QStringLiteral("clip"), id}, {QStringLiteral("semitones"), 12}});
		QVERIFY2(set.ok, qPrintable(set.errorMessage));
		QCOMPARE(clip->clipEdits().transpose, 12);

		song->playSong();
		NotePlayHandle* note = nullptr;
		for (int period = 0; period < 64 && note == nullptr; ++period)
		{
			Engine::audioEngine()->renderNextPeriod();
			note = noteOf(track);
		}
		QVERIFY2(note != nullptr, "the clip's note never started");
		QCOMPARE(note->key(), 81);
		QCOMPARE(clip->notes().front()->key(), 69);
		song->stop();
		Engine::audioEngine()->renderNextPeriod();

		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(clip->clipEdits().transpose, 0);
		QCOMPARE(run(QStringLiteral("clip.set_transpose"), {{QStringLiteral("clip"), id}, {QStringLiteral("semitones"), 60}}).errorKind,
			ControlErrorKind::InvalidArgs);
	}

	void theTransposeIsSavedOnlyWhenSet()
	{
		Song* song = Engine::getSong();
		song->clearProject();
		auto* track = Track::create(Track::Type::Instrument, song);
		Clip* clip = track->createClip(TimePos(0));
		QDomDocument doc;
		QDomElement root = doc.createElement(QStringLiteral("root"));
		QVERIFY(!clip->saveState(doc, root).hasAttribute(QStringLiteral("transpose")));
		ClipEdits edits;
		edits.transpose = -5;
		clip->setClipEdits(edits);
		const QDomElement saved = clip->saveState(doc, root);
		QCOMPARE(saved.attribute(QStringLiteral("transpose")).toInt(), -5);
		Clip* loaded = track->createClip(TimePos(0));
		loaded->restoreState(saved);
		QCOMPARE(loaded->clipEdits().transpose, -5);
	}

	void aClipKeepsItsSnapPoint()
	{
		Song* song = Engine::getSong();
		song->clearProject();
		auto* track = Track::create(Track::Type::Instrument, song);
		Clip* clip = track->createClip(TimePos(0));
		clip->changeLength(TimePos(192));
		const QString id = control::clipIdOf(clip);
		QVERIFY(run(QStringLiteral("clip.set_snap_point"), {{QStringLiteral("clip"), id}, {QStringLiteral("ticks"), 48}}).ok);
		QCOMPARE(clip->clipEdits().snapTicks, 48);
		QDomDocument doc;
		QDomElement root = doc.createElement(QStringLiteral("root"));
		const QDomElement saved = clip->saveState(doc, root);
		QCOMPARE(saved.attribute(QStringLiteral("snappoint")).toInt(), 48);
		Clip* loaded = track->createClip(TimePos(0));
		loaded->restoreState(saved);
		QCOMPARE(loaded->clipEdits().snapTicks, 48);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(clip->clipEdits().snapTicks, 0);
		QVERIFY(!clip->saveState(doc, root).hasAttribute(QStringLiteral("snappoint")));
		QCOMPARE(run(QStringLiteral("clip.set_snap_point"), {{QStringLiteral("clip"), id}, {QStringLiteral("ticks"), 192}}).errorKind,
			ControlErrorKind::InvalidArgs);
		QCOMPARE(run(QStringLiteral("clip.set_snap_point"),
			{{QStringLiteral("clip"), id}, {QStringLiteral("ticks"), 2147483648.0}}).errorKind,
			ControlErrorKind::InvalidArgs);
		QCOMPARE(clip->clipEdits().snapTicks, 0);
	}

	void anAudioClipChangesSpeed()
	{
		ClipEdits edits;
		QCOMPARE(clipSpeedAt(edits, 50, 100, 0, 0), 1.f);  // untouched: natural speed everywhere
		edits.speedFrom = 50;
		edits.speedTo = 150;
		QCOMPARE(clipSpeedAt(edits, 50, 100, 0, 0), 1.f);  // the ramp's middle
		edits = ClipEdits{};
		QCOMPARE(clipSpeedAt(edits, 90, 100, 0, 20), 0.5f);  // half-way down a tape stop
		QCOMPARE(clipSpeedAt(edits, 5, 100, 10, 0), 0.5f);  // half-way up a tape start
		QCOMPARE(clipSpeedAt(edits, 100, 100, 0, 20), 1.f / 32.f);  // never stopped dead
		Song* song = Engine::getSong();
		song->clearProject();
		auto* track = new SampleTrack(song);
		auto* clip = dynamic_cast<SampleClip*>(track->createClip(TimePos(0)));
		clip->changeLength(TimePos(192));
		const QString id = control::clipIdOf(clip);
		QVERIFY(run(QStringLiteral("clip.speed"), {{QStringLiteral("clip"), id}, {QStringLiteral("from"), 80}, {QStringLiteral("fade_out"), 48}}).ok);
		QCOMPARE(clip->clipEdits().speedTo, 80);  // 'to' follows 'from'
		QDomDocument doc;
		QDomElement root = doc.createElement(QStringLiteral("root"));
		const QDomElement saved = clip->saveState(doc, root);
		QCOMPARE(saved.attribute(QStringLiteral("speedout")).toInt(), 48);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(!clip->clipEdits().hasSpeed());
		QVERIFY(!clip->saveState(doc, root).hasAttribute(QStringLiteral("speedfrom")));
	}

	void aSampleClipIsShiftedThroughRubberBandOrRefused()
	{
		Song* song = Engine::getSong();
		song->clearProject();
		auto* track = new SampleTrack(song);
		auto* clip = dynamic_cast<SampleClip*>(track->createClip(TimePos(0)));
		clip->setWarpStretchMode(WarpStretchMode::PreservePitch);
		const ControlResult set = run(QStringLiteral("clip.set_transpose"),
			{{QStringLiteral("clip"), control::clipIdOf(clip)}, {QStringLiteral("semitones"), 3}});
		if (!RubberBandPool::available())
		{
			QCOMPARE(set.errorKind, ControlErrorKind::Requires);
			QCOMPARE(clip->warpStretchMode(), WarpStretchMode::PreservePitch);
			return;
		}
		QVERIFY2(set.ok, qPrintable(set.errorMessage));
		QCOMPARE(clip->warpStretchMode(), WarpStretchMode::RubberBand);
		QCOMPARE(clip->clipEdits().transpose, 3);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(clip->warpStretchMode(), WarpStretchMode::PreservePitch);
		QCOMPARE(clip->clipEdits().transpose, 0);
	}
};

QTEST_MAIN(ClipTransposeTest)
#include "ClipTransposeTest.moc"
