/*
 * SessionTuningTest.cpp - the registered proof for board card #712 (lane 3):
 *                         the session-wide dynamic-tuning table and mts.*
 *
 * Slots: (1) INACTIVE table == the pre-#712 branch; (2) THE PROOF - transport
 * PLAYING, two sounding notes, one .scl, BOTH handles on the ONE table,
 * control.undo brings the old sound back, transport never stopping; (3) junk
 * paths are typed errors with no table write and no undo step; (4) table ==
 * Microtuner over 128 keys from the SAME Scale/Keymap; (5) mts.master_set.
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
 * License along with this program (see COPYING); if not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301 USA.
 *
 */

#include <QtTest>

#include <cmath>
#include <cstdio>

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QVector>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "Keymap.h"
#include "Microtuner.h"
#include "MidiEvent.h"
#include "Note.h"
#include "NotePlayHandle.h"
#include "ProjectJournal.h"
#include "Scale.h"
#include "SessionTuning.h"
#include "Song.h"
#include "Track.h"

#include "ReversibilityTestSupport.h"

using namespace lmms;
using namespace revtest;

namespace
{

//! The fixture .scl (module DATA): a 3-degree JI scale whose keyboard
//! mapping is nothing like 12-TET, so "the frequency moved" cannot be luck.
const QStringList& jiSclLines()
{
	static const QStringList lines{
		QStringLiteral("! lane712-parity.scl"),
		QStringLiteral("3-note just scale for board card #712"),
		QStringLiteral("3"),
		QStringLiteral("1/1"),
		QStringLiteral("5/4"),
		QStringLiteral("3/2")};
	return lines;
}

QString writeJiScl(QTemporaryDir& dir)
{
	const QString path = dir.filePath(QStringLiteral("ji3.scl"));
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) { return QString(); }
	file.write(jiSclLines().join(QLatin1Char('\n')).toUtf8());
	file.write("\n");
	file.close();
	return path;
}

bool closeTo(double actual, double expected)
{
	return std::fabs(actual - expected) <= 1e-4 * std::fmax(1.0, std::fabs(expected));
}

//! Exactly the three keys mts.set_tuning's schema carries - the stated cap,
//! and what the undo assertions compare.
QJsonObject snapshotState(const QJsonObject& fullState)
{
	QJsonObject state;
	state.insert(QStringLiteral("active"), fullState.value(QStringLiteral("active")));
	state.insert(QStringLiteral("source"), fullState.value(QStringLiteral("source")));
	state.insert(QStringLiteral("frequencies"), fullState.value(QStringLiteral("frequencies")));
	return state;
}

//! \a mismatchKey names the first difference (-1 = flag/source, -2 = length).
bool sameTable(const QJsonObject& a, const QJsonObject& b, int* mismatchKey)
{
	if (a.value(QStringLiteral("active")) != b.value(QStringLiteral("active"))
		|| a.value(QStringLiteral("source")) != b.value(QStringLiteral("source")))
	{
		*mismatchKey = -1;
		return false;
	}
	const QJsonArray left = a.value(QStringLiteral("frequencies")).toArray();
	const QJsonArray right = b.value(QStringLiteral("frequencies")).toArray();
	if (left.size() != right.size()) { *mismatchKey = -2; return false; }
	for (int key = 0; key < left.size(); ++key)
	{
		if (!closeTo(left.at(key).toDouble(), right.at(key).toDouble()))
		{
			*mismatchKey = key;
			return false;
		}
	}
	return true;
}

QJsonArray frequenciesOf(int size, double value)
{
	QJsonArray frequencies;
	for (int key = 0; key < size; ++key) { frequencies.append(value); }
	return frequencies;
}

//! The 12-TET value the pre-#712 branch computes - from the MODELS: base
//! note, master pitch, pitch knob (no detune automation, no slide).
double baseline12Tet(int key, int baseNote, int masterPitch, double pitchCents)
{
	const double semitones = (key - baseNote + masterPitch) / 12.0 + pitchCents / 1200.0;
	return DefaultBaseFreq * std::exp2(semitones);
}

//! Render until the pending retune mark is consumed (bounded periods). The
//! dummy device is stopped (AutomationModesTest pattern): the test drives
//! AudioEngine::renderNextPeriod() itself - the REAL period pipeline
//! (Song::processNextBuffer inside stage 0, the play-handle render in stage
//! 1 - that is where NotePlayHandle::play() consumes the retune mark) - and
//! the transport stays PLAYING through every one of them.
float renderedFrequency(NotePlayHandle* note, double expected, int periods = 64)
{
	float seen = note->frequency();
	for (int period = 0; period < periods && !closeTo(seen, expected); ++period)
	{
		Engine::audioEngine()->renderNextPeriod();
		seen = note->frequency();
	}
	return seen;
}

QVector<InstrumentTrack*> instrumentTracks()
{
	QVector<InstrumentTrack*> tracks;
	for (Track* track : Engine::getSong()->tracks())
	{
		if (auto* instrumentTrack = dynamic_cast<InstrumentTrack*>(track))
		{
			tracks.append(instrumentTrack);
		}
	}
	return tracks;
}

//! The SAME mapping as \a source (getters -> the value constructor): the Song
//! takes a non-const shared_ptr, the session's is shared as const.
std::shared_ptr<Keymap> copyOfKeymap(const std::shared_ptr<const Keymap>& source)
{
	return std::make_shared<Keymap>(source->getDescription(), source->getMap(),
		source->getFirstKey(), source->getLastKey(), source->getMiddleKey(),
		source->getBaseKey(), source->getBaseFreq());
}

struct RefusalCase
{
	QString id;
	QJsonObject args;
	ControlErrorKind kind;
};

const QVector<RefusalCase>& refusalCases(const QString& missingPath)
{
	static const QVector<RefusalCase> cases{
		{QStringLiteral("mts.load_scale"),
			QJsonObject{{QStringLiteral("path"), QString()}},
			ControlErrorKind::InvalidArgs},
		{QStringLiteral("mts.load_scale"),
			QJsonObject{{QStringLiteral("path"), missingPath}},
			ControlErrorKind::NotFound},
		{QStringLiteral("mts.load_keymap"),
			QJsonObject{{QStringLiteral("path"), QString()}},
			ControlErrorKind::InvalidArgs},
		{QStringLiteral("mts.load_keymap"),
			QJsonObject{{QStringLiteral("path"), missingPath}},
			ControlErrorKind::NotFound},
		{QStringLiteral("mts.set_tuning"), QJsonObject{}, ControlErrorKind::InvalidArgs},
		{QStringLiteral("mts.set_tuning"),
			QJsonObject{{QStringLiteral("note"), 5}},
			ControlErrorKind::InvalidArgs},
		// Wrong length: the cap is 128 exactly, refused whole-cloth.
		{QStringLiteral("mts.set_tuning"),
			QJsonObject{{QStringLiteral("frequencies"), frequenciesOf(127, 440.0)}},
			ControlErrorKind::Refused},
		// Right length, illegal content: a snapshot replays exactly or not at all
		// (0 is the unmapped marker, negatives are not frequencies).
		{QStringLiteral("mts.set_tuning"),
			QJsonObject{{QStringLiteral("frequencies"), frequenciesOf(128, -5.0)}},
			ControlErrorKind::Refused},
		{QStringLiteral("mts.set_tuning"),
			QJsonObject{{QStringLiteral("note"), 0}, {QStringLiteral("frequency"), 0.0}},
			ControlErrorKind::InvalidArgs},
		{QStringLiteral("mts.set_note"),
			QJsonObject{{QStringLiteral("note"), 5}, {QStringLiteral("frequency"), -3.0}},
			ControlErrorKind::InvalidArgs},
		{QStringLiteral("mts.master_set"), QJsonObject{}, ControlErrorKind::InvalidArgs}};
	return cases;
}

} // namespace

class SessionTuningTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
#endif
		// MEASURED first-run requirements: the note pool's own init (else the
		// first note-on segfaults), setReady(true) (else engine_starting),
		// device stopped - THIS test renders (AutomationModesTest pattern).
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

	//! INACTIVE table -> the pre-#712 branch, computed from the models.
	void baselineInactiveKeepsTheOldRenderPath()
	{
		Song* song = Engine::getSong();
		song->clearProject();
		SessionTuning* tuning = SessionTuning::instance();
		tuning->reset();
		QVERIFY2(!tuning->isActive(), "the table must start inactive (mts.reset)");

		auto* track = dynamic_cast<InstrumentTrack*>(
			Track::create(Track::Type::Instrument, song));
		QVERIFY(track != nullptr);
		track->processInEvent(MidiEvent(MidiNoteOn, 1, 69, 100));
		NotePlayHandle* note = track->playingNote(69);
		QVERIFY2(note != nullptr, "the note-on did not start a note handle");

		const double expected = baseline12Tet(69, track->baseNoteModel()->value(),
			song->masterPitch(), track->pitchModel()->value());
		const double seen = note->frequency();
		std::fprintf(stdout, "MTS712_EVIDENCE baseline key69=%.4f expected=%.4f active=%d\n",
			seen, expected, tuning->isActive() ? 1 : 0);
		QVERIFY2(closeTo(seen, expected),
			qPrintable(QStringLiteral("inactive table changed the baseline: got %1, the "
				"pre-#712 branch says %2").arg(seen).arg(expected)));

		track->processInEvent(MidiEvent(MidiNoteOff, 1, 69));
	}

	//! THE PROOF: transport playing, two sounding notes, one .scl, both tracks
	//! on the ONE table, undo brings the old sound back - transport never stops.
	void loadScaleWhileTransportPlaysRetunesSoundingNotes()
	{
		Song* song = Engine::getSong();
		song->clearProject();
		SessionTuning* tuning = SessionTuning::instance();
		tuning->reset();

		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString path = writeJiScl(dir);
		QVERIFY2(!path.isEmpty(), "the fixture .scl could not be written");

		const ControlResult played = run(QStringLiteral("transport.play"));
		QVERIFY2(played.ok, qPrintable(QStringLiteral("transport.play refused: %1")
			.arg(played.errorMessage)));
		QVERIFY2(song->isPlaying(), "the transport is not playing after transport.play");

		const QString trackAId = addInstrumentTrack();
		const QString trackBId = addInstrumentTrack();
#ifdef Q_OS_WIN
		if (trackAId.isEmpty() || trackBId.isEmpty())
		{
			song->stop();
			QSKIP("no instrument could be loaded: on Windows the plugin modules link the zene executable, "
				"so a test host cannot load them (the same limit ControlAutomationModesTest skips for)");
		}
#endif
		QVERIFY2(!trackAId.isEmpty() && !trackBId.isEmpty(),
			"two instrument tracks with a real instrument could not be created");
		QVector<InstrumentTrack*> tracks = instrumentTracks();
		QVERIFY2(tracks.size() == 2,
			"clearProject did not leave exactly the two tracks this slot created");
		InstrumentTrack* first = tracks.at(0);
		InstrumentTrack* second = tracks.at(1);

		first->processInEvent(MidiEvent(MidiNoteOn, 1, 60, 100));
		second->processInEvent(MidiEvent(MidiNoteOn, 1, 60, 100));

		// The manual render loop below must be the ONLY renderer. transport.play
		// started the device's render thread, which races the direct
		// renderNextPeriod() calls on m_processHandles (one side deletes a
		// finished NotePlayHandle while the other's fillJobQueue still iterates
		// it -> use-after-free in isFinished, the abort this test hit at the
		// merged tip). Stopping processing keeps the transport PLAYING (the
		// song state is separate) while making the renders single-threaded -
		// the AutomationModesTest pattern the helper's comment already states.
		Engine::audioEngine()->audioDev()->stopProcessing();
		NotePlayHandle* noteA = first->playingNote(60);
		NotePlayHandle* noteB = second->playingNote(60);
		QVERIFY(noteA != nullptr && noteB != nullptr);
		const double bornFrequency = baseline12Tet(60, first->baseNoteModel()->value(),
			song->masterPitch(), first->pitchModel()->value());
		QVERIFY2(closeTo(noteA->frequency(), bornFrequency),
			"the first note was not born on the 12-TET baseline");

		const QJsonObject before = snapshotState(run(QStringLiteral("mts.get_state")).result);
		const ControlResult loaded = run(QStringLiteral("mts.load_scale"),
			QJsonObject{{QStringLiteral("path"), path}});
		QVERIFY2(loaded.ok, qPrintable(QStringLiteral("mts.load_scale failed: %1")
			.arg(loaded.errorMessage)));
		QVERIFY(tuning->isActive());

		// The ONE table both tracks read AT RENDER - through the engine's own
		// render function: that is "while the transport plays" (playSong has
		// been playing since transport.play).
		const double tableFrequency = tuning->noteToFreq(60);
		QVERIFY2(!closeTo(tableFrequency, bornFrequency),
			"the JI fixture maps key 60 to the same Hz as 12-TET - fix the fixture, "
			"not the test: it would prove nothing");
		const double seenA = renderedFrequency(noteA, tableFrequency);
		const double seenB = renderedFrequency(noteB, tableFrequency);
		std::fprintf(stdout,
			"MTS712_EVIDENCE after load_scale: table=%.4f noteA=%.4f noteB=%.4f "
			"(born %.4f) playing=%d\n",
			tableFrequency, seenA, seenB, bornFrequency, song->isPlaying() ? 1 : 0);
		QVERIFY2(closeTo(seenA, tableFrequency),
			qPrintable(QStringLiteral("first SOUNDING track off the table: handle %1, "
				"table %2").arg(seenA).arg(tableFrequency)));
		QVERIFY2(closeTo(seenB, tableFrequency),
			qPrintable(QStringLiteral("second SOUNDING track off the same table: handle "
				"%1, table %2").arg(seenB).arg(tableFrequency)));
		QVERIFY2(song->isPlaying(), "the transport stopped during the table change");

		// Undo: the table, and through it the SOUND, comes back.
		REV_UNDO_OR_FAIL();
		int mismatchKey = 0;
		const QJsonObject undone = snapshotState(
			run(QStringLiteral("mts.get_state")).result);
		QVERIFY2(sameTable(before, undone, &mismatchKey),
			qPrintable(QStringLiteral("control.undo did not restore the table "
				"(first difference: %1)").arg(mismatchKey)));
		const double seenBackA = renderedFrequency(noteA, bornFrequency);
		const double seenBackB = renderedFrequency(noteB, bornFrequency);
		std::fprintf(stdout,
			"MTS712_EVIDENCE after control.undo: noteA=%.4f noteB=%.4f playing=%d\n",
			seenBackA, seenBackB, song->isPlaying() ? 1 : 0);
		QVERIFY2(closeTo(seenBackA, bornFrequency),
			"undo restored the table but the sounding first track kept the new tuning");
		QVERIFY2(closeTo(seenBackB, bornFrequency),
			"undo restored the table but the sounding second track kept the new tuning");
		QVERIFY2(song->isPlaying(), "the transport stopped during the undo");

		first->processInEvent(MidiEvent(MidiNoteOff, 1, 60));
		second->processInEvent(MidiEvent(MidiNoteOff, 1, 60));
		QVERIFY(run(QStringLiteral("transport.stop")).ok);
		tuning->reset();
	}

	//! Typed error, NO table write, NO undo step - every junk path.
	void typedRefusalsWriteNothingAndPushNoUndoStep()
	{
		SessionTuning* tuning = SessionTuning::instance();
		tuning->reset();
		ProjectJournal* journal = Engine::projectJournal();
		QVERIFY(journal != nullptr);
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString missing = dir.filePath(QStringLiteral("absent.scl"));
		const QJsonObject before = snapshotState(run(QStringLiteral("mts.get_state")).result);
		const int depthBefore = journal->undoDepth();

		for (const RefusalCase& refusals : refusalCases(missing))
		{
			const ControlResult result = run(refusals.id, refusals.args);
			QVERIFY2(!result.ok, qPrintable(QStringLiteral("%1 with junk arguments SUCCEEDED "
				"- it must be a typed refusal").arg(refusals.id)));
			QCOMPARE(result.errorKind, refusals.kind);
			QVERIFY2(!result.errorMessage.isEmpty(),
				qPrintable(refusals.id + " failed without a message"));
			QCOMPARE(journal->undoDepth(), depthBefore);
			int mismatchKey = 0;
			const QJsonObject now = snapshotState(run(QStringLiteral("mts.get_state")).result);
			QVERIFY2(sameTable(before, now, &mismatchKey),
				qPrintable(QStringLiteral("%1 refused yet wrote the table; first "
					"difference at entry %2").arg(refusals.id).arg(mismatchKey)));
		}
	}

	//! The anti-reimplementation anchor: same Scale, same Keymap, 128 keys.
	void sessionTableMatchesTheMicrotunerOverAll128Keys()
	{
		Song* song = Engine::getSong();
		song->clearProject();
		SessionTuning* tuning = SessionTuning::instance();
		tuning->reset();
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString path = writeJiScl(dir);
		QVERIFY(!path.isEmpty());
		const ControlResult loaded = run(QStringLiteral("mts.load_scale"),
			QJsonObject{{QStringLiteral("path"), path}});
		QVERIFY2(loaded.ok, qPrintable(loaded.errorMessage));
		QVERIFY(tuning->isActive());
		const std::shared_ptr<const Scale> sessionScale = tuning->scale();
		const std::shared_ptr<const Keymap> sessionKeymap = tuning->keymap();
		QVERIFY(sessionScale != nullptr && sessionKeymap != nullptr);

		// The Microtuner reads the SAME objects through the Song (slots 0/0,
		// keyRangeImport on - the default): Interval/Scale stay ITS contract.
		song->setScale(0, std::make_shared<Scale>(sessionScale->getDescription(),
			sessionScale->getIntervals()));
		song->setKeymap(0, copyOfKeymap(sessionKeymap));
		Microtuner microtuner;
		const int baseNoteForKeyRange = sessionKeymap->getBaseKey();
		for (int key = 0; key < SessionTuning::TableSize; ++key)
		{
			const double expected = microtuner.keyToFreq(key, baseNoteForKeyRange);
			const double actual = tuning->noteToFreq(key);
			if (!closeTo(actual, expected))
			{
				QFAIL(qPrintable(QStringLiteral("key %1: the session table says %2 Hz, "
					"the Microtuner reading the SAME Scale/Keymap says %3 Hz - table "
					"construction and the Microtuner have diverged")
					.arg(key).arg(actual).arg(expected)));
			}
		}
		std::fprintf(stdout, "MTS712_EVIDENCE parity: 128 keys table==Microtuner, "
			"scale='%s' keymap='%s'\n",
			qPrintable(sessionScale->getDescription()),
			qPrintable(sessionKeymap->getDescription()));
		tuning->reset();
	}

	//! This box has no libMTS.so: arming is a typed REFUSAL that writes nothing
	//! and names the library; the installed-library branch stays runnable.
	void masterSetReportsItsOwnMtsEspState()
	{
		Song* song = Engine::getSong();
		song->clearProject();
		const int depthBefore = Engine::projectJournal()->undoDepth();
		if (SessionTuning::mtsEspLibraryPresent())
		{
			const ControlResult armed = run(QStringLiteral("mts.master_set"),
				QJsonObject{{QStringLiteral("enabled"), true}});
			QVERIFY2(armed.ok, qPrintable(armed.errorMessage));
			QCOMPARE(run(QStringLiteral("mts.get_state")).result
				.value(QStringLiteral("mts_library")).toString(), QStringLiteral("present"));
			QVERIFY(run(QStringLiteral("mts.get_state")).result
				.value(QStringLiteral("mts_master")).toBool());
			const ControlResult disarmed = run(QStringLiteral("mts.master_set"),
				QJsonObject{{QStringLiteral("enabled"), false}});
			QVERIFY(disarmed.ok);
			QVERIFY(!run(QStringLiteral("mts.get_state")).result
				.value(QStringLiteral("mts_master")).toBool());
			return;
		}

		const ControlResult armed = run(QStringLiteral("mts.master_set"),
			QJsonObject{{QStringLiteral("enabled"), true}});
		QVERIFY2(!armed.ok, "arming the MTS-ESP master succeeded with no MTS-ESP "
			"library installed - the wrapper's calls would be inert no-ops");
		QCOMPARE(armed.errorKind, ControlErrorKind::Refused);
		QVERIFY2(armed.errorMessage.contains(QStringLiteral("libMTS.so")),
			qPrintable(QStringLiteral("the refusal must name the library: %1")
				.arg(armed.errorMessage)));
		QCOMPARE(Engine::projectJournal()->undoDepth(), depthBefore);
		const QJsonObject state = run(QStringLiteral("mts.get_state")).result;
		QCOMPARE(state.value(QStringLiteral("mts_library")).toString(),
			QStringLiteral("absent"));
		QVERIFY(!state.value(QStringLiteral("mts_master")).toBool());
		std::fprintf(stdout, "MTS712_EVIDENCE mts master arm refused: %s\n",
			qPrintable(armed.errorMessage));

		// Disarming what was never armed is idempotent - and as a SUCCESSFUL
		// mutator it records exactly ONE undo step (A16), while the refused
		// arm above recorded none.
		const ControlResult disarmed = run(QStringLiteral("mts.master_set"),
			QJsonObject{{QStringLiteral("enabled"), false}});
		QVERIFY2(disarmed.ok, qPrintable(disarmed.errorMessage));
		QCOMPARE(Engine::projectJournal()->undoDepth(), depthBefore + 1);
	}
};

QTEST_GUILESS_MAIN(SessionTuningTest)
#include "SessionTuningTest.moc"
