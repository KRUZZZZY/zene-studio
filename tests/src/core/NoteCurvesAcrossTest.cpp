/*
 * NoteCurvesAcrossTest.cpp - a note's pitch curve and one note edit over several clips
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

/*! ED-122 / ED-078 (0.5.0 L07): note.pitch_curve_set / get (the note's DetuningHelper curve: points,
 *  validation, one undo step, the note's element round trip, a copied note not sharing the new curve)
 *  and note.across (one command over several clips, one undo step for all, a refused command or an
 *  unknown clip changing nothing, a clip with nothing to do reported). */

#include <QtTest>

#include <limits>
#include <algorithm>

#include <QDomDocument>
#include <QJsonArray>

#include "BasicFilters.h"
#include "Note.h"
#include "PlayHandle.h"
#include "Track.h"
#define private public
#include "NotePlayHandle.h"
#undef private

#include "AutomationClip.h"
#include "AudioEngine.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "DetuningHelper.h"
#include "Engine.h"
#include "InstrumentFunctions.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "Song.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

QList<int> keysOf(const MidiClip* clip)
{
	QList<int> out;
	for (const Note* note : clip->notes()) { out << note->key(); }
	return out;
}

} // namespace

class NoteCurvesAcrossTest : public QObject
{
	Q_OBJECT

private:
	InstrumentTrack* m_track = nullptr;

	MidiClip* clipWith(int position, std::initializer_list<int> keys)
	{
		auto* clip = dynamic_cast<MidiClip*>(m_track->createClip(TimePos(position)));
		int at = 0;
		for (const int key : keys) { clip->addNote(Note(TimePos(24), TimePos(at += 24), key), false); }
		clip->rearrangeAllNotes();
		return clip;
	}

private slots:
	void initTestCase()
	{
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

	void init()
	{
		Engine::getSong()->clearProject();
		m_track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
	}

	void aNoteCarriesItsOwnPitchCurve()
	{
		MidiClip* clip = clipWith(0, {60});
		const QString id = control::clipIdOf(clip);
		const QString note = control::noteIdOf(clip->notes().front());
		const QJsonArray bend{QJsonObject{{QStringLiteral("tick"), 0}, {QStringLiteral("semitones"), 0.0}},
			QJsonObject{{QStringLiteral("tick"), 12}, {QStringLiteral("semitones"), 2.0}}};
		const ControlResult set = run(QStringLiteral("note.pitch_curve_set"), {{QStringLiteral("clip"), id}, {QStringLiteral("note"), note},
			{QStringLiteral("points"), bend}});
		QVERIFY2(set.ok, qPrintable(set.errorMessage));
		const Note* bent = clip->notes().front();
		QVERIFY(bent->detuning() != nullptr && bent->detuning()->hasAutomation());
		QCOMPARE(bent->detuning()->automationClip()->valueAt(TimePos(6)), 1.f);  // linear between
		QCOMPARE(run(QStringLiteral("note.pitch_curve_get"), {{QStringLiteral("clip"), id}, {QStringLiteral("note"), note}})
			.result.value(QStringLiteral("points")).toArray().size(), 2);

		// The note's element carries it: a copy restored from it has the same curve.
		QDomDocument doc;
		QDomElement root = doc.createElement(QStringLiteral("root"));
		const QDomElement saved = clip->notes().front()->saveState(doc, root);
		Note loaded;
		loaded.restoreState(saved);
		QVERIFY(loaded.detuning() != nullptr && loaded.detuning()->hasAutomation());

		QVERIFY(run(QStringLiteral("control.undo")).ok);
		const Note* restored = clip->notes().front();
		QVERIFY(restored->detuning() == nullptr || !restored->detuning()->hasAutomation());
		QCOMPARE(run(QStringLiteral("note.pitch_curve_set"), {{QStringLiteral("clip"), id}, {QStringLiteral("note"), note},
			{QStringLiteral("points"), QJsonArray{QJsonObject{{QStringLiteral("tick"), 0}, {QStringLiteral("semitones"), 61.0}}}}}).errorKind,
			ControlErrorKind::InvalidArgs);
	}

	void aCopiedNoteDoesNotShareTheNewCurve()
	{
		MidiClip* clip = clipWith(0, {60});
		Note copy = *clip->notes().front();  // shares the note's (absent) helper
		clip->notes().front()->createDetuning();
		Note shared = *clip->notes().front();  // shares the helper now
		const ControlResult set = run(QStringLiteral("note.pitch_curve_set"), {{QStringLiteral("clip"), control::clipIdOf(clip)},
			{QStringLiteral("note"), control::noteIdOf(clip->notes().front())},
			{QStringLiteral("points"), QJsonArray{QJsonObject{{QStringLiteral("tick"), 0}, {QStringLiteral("semitones"), 5.0}}}}});
		QVERIFY(set.ok);
		QVERIFY2(shared.detuning() == nullptr || !shared.detuning()->hasAutomation(), "the copy was given the new curve");
		QVERIFY(copy.detuning() == nullptr);
	}

	void aRelativeShapeFitsEveryNoteInOneStep()
	{
		MidiClip* clip = dynamic_cast<MidiClip*>(m_track->createClip(TimePos(0)));
		clip->addNote(Note(TimePos(48), TimePos(0), 60), false);
		clip->addNote(Note(TimePos(12), TimePos(96), 62), false);
		clip->rearrangeAllNotes();
		const QJsonArray notes{control::noteIdOf(clip->notes()[0]), control::noteIdOf(clip->notes()[1])};
		const ControlResult set = run(QStringLiteral("note.pitch_curve_set"), {{QStringLiteral("clip"), control::clipIdOf(clip)},
			{QStringLiteral("notes"), notes}, {QStringLiteral("relative"), true},
			{QStringLiteral("points"), QJsonArray{QJsonObject{{QStringLiteral("tick"), 0.0}, {QStringLiteral("semitones"), 0.0}},
				QJsonObject{{QStringLiteral("tick"), 1.0}, {QStringLiteral("semitones"), 2.0}}}}});
		QVERIFY2(set.ok, qPrintable(set.errorMessage));
		// The end point sits at each note's own end: tick 48 for the long note, 12 for the short one.
		QVERIFY(clip->notes()[0]->detuning()->automationClip()->getTimeMap().contains(48));
		QVERIFY(clip->notes()[1]->detuning()->automationClip()->getTimeMap().contains(12));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		for (const Note* note : clip->notes()) { QVERIFY(note->detuning() == nullptr || !note->detuning()->hasAutomation()); }
		QCOMPARE(run(QStringLiteral("note.pitch_curve_set"), {{QStringLiteral("clip"), control::clipIdOf(clip)}, {QStringLiteral("notes"), notes},
			{QStringLiteral("relative"), true}, {QStringLiteral("points"), QJsonArray{QJsonObject{{QStringLiteral("tick"), 1.5},
				{QStringLiteral("semitones"), 0.0}}}}}).errorKind, ControlErrorKind::InvalidArgs);  // past the note's end
	}

	void oneEditOverAWholeTrack()
	{
		MidiClip* a = clipWith(0, {60});
		MidiClip* b = clipWith(192, {62});
		QVERIFY(run(QStringLiteral("note.across"), {{QStringLiteral("tracks"), QJsonArray{control::trackIdOf(m_track)}},
			{QStringLiteral("command"), QStringLiteral("note.transpose")}, {QStringLiteral("args"), QJsonObject{{QStringLiteral("semitones"), -12}}}}).ok);
		QCOMPARE(keysOf(a), (QList<int>{48}));
		QCOMPARE(keysOf(b), (QList<int>{50}));
	}

	void oneEditAcrossSeveralClipsIsOneUndoStep()
	{
		MidiClip* a = clipWith(0, {60, 62});
		MidiClip* b = clipWith(192, {48});
		const QJsonArray clips{control::clipIdOf(a), control::clipIdOf(b), control::clipIdOf(a)};  // a twice: edited once
		const ControlResult done = run(QStringLiteral("note.across"), {{QStringLiteral("clips"), clips},
			{QStringLiteral("command"), QStringLiteral("note.transpose")}, {QStringLiteral("args"), QJsonObject{{QStringLiteral("semitones"), 12}}}});
		QVERIFY2(done.ok, qPrintable(done.errorMessage));
		QCOMPARE(done.result.value(QStringLiteral("edited")).toInt(), 2);
		QCOMPARE(keysOf(a), (QList<int>{72, 74}));
		QCOMPARE(keysOf(b), (QList<int>{60}));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(keysOf(a), (QList<int>{60, 62}));
		QCOMPARE(keysOf(b), (QList<int>{48}));
	}

	//! note.across counted every successful handler as an edit, so a handler that succeeded while
	//! changing nothing made the batch report clips as edited and hand back a control.undo
	//! transaction no undo step backs - replaying it would undo an unrelated prior action.
	//! note.restore_timing over notes that keep no original timing succeeds with 'restored: 0',
	//! adds no journal checkpoint and returns no __transaction.
	void anEditThatChangedNothingIsNotAnUndoStep()
	{
		MidiClip* a = clipWith(0, {60});
		MidiClip* b = clipWith(192, {62});
		const QJsonArray clips{control::clipIdOf(a), control::clipIdOf(b)};
		const int recorded = run(QStringLiteral("control.transactions")).result.value(QStringLiteral("transactions")).toArray().size();
		const ControlResult done = run(QStringLiteral("note.across"), {{QStringLiteral("clips"), clips},
			{QStringLiteral("command"), QStringLiteral("note.restore_timing")}, {QStringLiteral("args"), QJsonObject{}}});
		QCOMPARE(done.errorKind, ControlErrorKind::NotFound);  // it changed no clip
		QCOMPARE(keysOf(a), (QList<int>{60}));
		QCOMPARE(keysOf(b), (QList<int>{62}));
		QCOMPARE(run(QStringLiteral("control.transactions")).result.value(QStringLiteral("transactions")).toArray().size(), recorded);
	}

	//! Cluster 2857 probe: every comparison against NaN is false, so a NaN tick would slip past
	//! readCurve's range check and reach std::lround. Whether a nonfinite double can be stored in
	//! QJsonValue at all decides whether this is reachable; this case measures it.
	void aNonFiniteCurvePointIsRefused()
	{
		MidiClip* clip = clipWith(0, {60});
		const QString id = control::clipIdOf(clip);
		const QString note = control::noteIdOf(clip->notes().front());
		const QJsonArray bend{QJsonObject{{QStringLiteral("tick"), std::numeric_limits<double>::quiet_NaN()},
			{QStringLiteral("semitones"), 1.0}}};
		const ControlResult set = run(QStringLiteral("note.pitch_curve_set"), {{QStringLiteral("clip"), id},
			{QStringLiteral("note"), note}, {QStringLiteral("points"), bend}});
		QVERIFY2(set.errorKind == ControlErrorKind::InvalidArgs, qPrintable(set.ok ? "accepted a NaN tick" : set.errorMessage));
	}

	void aBadCallChangesNothing()
	{
		MidiClip* a = clipWith(0, {60});
		QCOMPARE(run(QStringLiteral("note.across"), {{QStringLiteral("clips"), QJsonArray{control::clipIdOf(a), QStringLiteral("clip-99999")}},
			{QStringLiteral("command"), QStringLiteral("note.transpose")}, {QStringLiteral("args"), QJsonObject{{QStringLiteral("semitones"), 1}}}}).errorKind,
			ControlErrorKind::NotFound);
		QCOMPARE(keysOf(a), (QList<int>{60}));
		QCOMPARE(run(QStringLiteral("note.across"), {{QStringLiteral("clips"), QJsonArray{control::clipIdOf(a)}},
			{QStringLiteral("command"), QStringLiteral("track.remove")}}).errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(run(QStringLiteral("note.across"), {{QStringLiteral("clips"), QJsonArray{control::clipIdOf(a)}},
			{QStringLiteral("command"), QStringLiteral("note.transpose")}, {QStringLiteral("args"), QJsonObject{}}}).errorKind,
			ControlErrorKind::InvalidArgs);  // 'semitones' is required
		QCOMPARE(keysOf(a), (QList<int>{60}));
	}

	void chordStackingRejectsTheFirstKeyPastTheRange()
	{
		const auto& chords = InstrumentFunctionNoteStacking::ChordTable::getInstance().chords();
		const auto chromatic = std::find_if(chords.begin(), chords.end(), [](const auto& chord)
		{
			return chord.getName() == QStringLiteral("Chromatic");
		});
		QVERIFY(chromatic != chords.end());
		const int chromaticIndex = static_cast<int>(chromatic - chords.begin());

		InstrumentFunctionNoteStacking stacking(nullptr);
		QDomDocument document;
		QDomElement settings = document.createElement(QStringLiteral("chordcreator"));
		settings.setAttribute(QStringLiteral("chord-enabled"), 1);
		settings.setAttribute(QStringLiteral("chord"), chromaticIndex);
		settings.setAttribute(QStringLiteral("chordrange"), 1);
		stacking.loadSettings(settings);

		NotePlayHandle* source = NotePlayHandleManager::acquire(m_track, 0,
			Engine::audioEngine()->framesPerPeriod(), Note(TimePos(96), TimePos(0), NumKeys - 1));
		stacking.processNote(source);

		QList<int> stackedKeys;
		const NotePlayHandleList children = source->m_subNotes;
		for (const NotePlayHandle* child : children) { stackedKeys << child->key(); }

		for (NotePlayHandle* child : children)
		{
			child->audioBusHandle()->removePlayHandle(child);
			NotePlayHandleManager::release(child);
		}
		Engine::audioEngine()->clearNewPlayHandles();
		NotePlayHandleManager::release(source);

		QCOMPARE(stackedKeys, (QList<int>{NumKeys - 1}));
	}
};

QTEST_MAIN(NoteCurvesAcrossTest)
#include "NoteCurvesAcrossTest.moc"
