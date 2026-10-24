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

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
#include <memory>
#include <vector>
#include <utility>
#include <vector>
#include <limits>

#include <QDir>
#include <QDomDocument>
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
#include "../../../thirdparty/mts-esp/libMTSClient.h"

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

//! A one-off .scl holding exactly \a body lines, for the scales this test
//! writes itself rather than shares.
QString writeScl(QTemporaryDir& dir, const QString& name, const QStringList& body)
{
	const QString path = dir.filePath(name);
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) { return QString(); }
	file.write(body.join(QLatin1Char('\n')).toUtf8());
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

void installNegativeDegreeFixture(Song* song, const std::vector<int>& degrees)
{
	std::vector<Interval> intervals;
	intervals.reserve(13);
	for (int degree = 0; degree <= 12; ++degree)
	{
		intervals.emplace_back(degree * 100.0f);
	}
	song->setScale(0, std::make_shared<Scale>(QStringLiteral("12-tone equal temperament"),
		std::move(intervals)));
	song->setKeymap(0, std::make_shared<Keymap>(QStringLiteral("negative degree fixture"), degrees,
		0, 127, 69, 69, 440.0f));
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
		// Relative paths: both error strings promise "must be absolute", so a
		// RELATIVE name is an argument error, not a missing-file one - even when
		// it happens to exist in the process's working directory.
		{QStringLiteral("mts.load_scale"),
			QJsonObject{{QStringLiteral("path"), QStringLiteral("ji3.scl")}},
			ControlErrorKind::InvalidArgs},
		{QStringLiteral("mts.load_keymap"),
			QJsonObject{{QStringLiteral("path"), QStringLiteral("ji3.kbm")}},
			ControlErrorKind::InvalidArgs},
		// Mixed shapes: a whole-table write never carries a note-shaped field, and
		// a half pair is not a pair.
		{QStringLiteral("mts.set_tuning"),
			QJsonObject{{QStringLiteral("frequencies"), frequenciesOf(128, 440.0)},
				{QStringLiteral("note"), 5}},
			ControlErrorKind::InvalidArgs},
		{QStringLiteral("mts.set_tuning"),
			QJsonObject{{QStringLiteral("frequencies"), frequenciesOf(128, 440.0)},
				{QStringLiteral("frequency"), 440.0}},
			ControlErrorKind::InvalidArgs},
		{QStringLiteral("mts.set_tuning"),
			QJsonObject{{QStringLiteral("frequency"), 440.0}},
			ControlErrorKind::InvalidArgs},
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

using MtsClientPtr = std::unique_ptr<MTSClient, decltype(&MTS_DeregisterClient)>;

std::vector<unsigned char> localMtsScaleOctaveDump()
{
	std::vector<unsigned char> message{0xF0, 0x7E, 0x7F, 0x08, 0x05, 0x00, 0x00};
	for (const char character : QByteArrayLiteral("Test scale"))
	{
		message.push_back(static_cast<unsigned char>(character));
	}
	message.insert(message.end(), 6, 0);
	for (int degree = 0; degree < 12; ++degree)
	{
		message.push_back(static_cast<unsigned char>(degree == 0 ? 65 : 64));
	}
	message.push_back(0);
	message.push_back(0xF7);
	return message;
}

void seedLocalMtsScale(MTSClient* client)
{
	const auto message = localMtsScaleOctaveDump();
	MTS_ParseMIDIDataU(client, message.data(), static_cast<int>(message.size()));
}

void sendLocalMtsTuningDumpRequest(MTSClient* client)
{
	constexpr unsigned char request[]{0xF0, 0x7E, 0x7F, 0x08, 0x00, 0x00, 0xF7};
	MTS_ParseMIDIDataU(client, request, static_cast<int>(sizeof(request)));
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

	//! Metadata children and malformed degree values must not enter the map.
	void keymapLoadingIgnoresNonDegreeChildrenAndInvalidValues()
	{
		QDomDocument document;
		QDomElement element = document.createElement(QStringLiteral("keymap"));
		document.appendChild(element);
		element.appendChild(document.createComment(QStringLiteral("annotation")));

		QDomElement metadata = document.createElement(QStringLiteral("metadata"));
		metadata.setAttribute(QStringLiteral("value"), 91);
		element.appendChild(metadata);

		QDomElement firstDegree = document.createElement(QStringLiteral("degree"));
		firstDegree.setAttribute(QStringLiteral("value"), 3);
		element.appendChild(firstDegree);

		QDomElement invalidDegree = document.createElement(QStringLiteral("degree"));
		invalidDegree.setAttribute(QStringLiteral("value"), QStringLiteral("not-an-integer"));
		element.appendChild(invalidDegree);

		QDomElement lastDegree = document.createElement(QStringLiteral("degree"));
		lastDegree.setAttribute(QStringLiteral("value"), 8);
		element.appendChild(lastDegree);

		Keymap keymap;
		keymap.loadSettings(element);

		QCOMPARE(keymap.getSize(), std::size_t{2});
		QCOMPARE(keymap.getMap().at(0), 3);
		QCOMPARE(keymap.getMap().at(1), 8);
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

	void negativeNoteDegreeUsesFloorOctaveDivision()
	{
		Song* song = Engine::getSong();
		song->clearProject();
		installNegativeDegreeFixture(song, {0, -2});
		float actual = 0.0f;
		{
			Microtuner microtuner;
			actual = microtuner.keyToFreq(70, 69);
		}
		song->clearProject();

		const double expected = 440.0 * std::exp2(-2.0 / 12.0);
		QVERIFY2(closeTo(actual, expected),
			qPrintable(QStringLiteral("degree -2 should be two scale steps below the base note: got %1 Hz, expected %2 Hz")
				.arg(actual).arg(expected)));
	}

	void negativeBaseDegreeUsesFloorOctaveDivision()
	{
		Song* song = Engine::getSong();
		song->clearProject();
		installNegativeDegreeFixture(song, {-2, 0});
		float actual = 0.0f;
		{
			Microtuner microtuner;
			actual = microtuner.keyToFreq(70, 69);
		}
		song->clearProject();

		const double expected = 440.0 * std::exp2(2.0 / 12.0);
		QVERIFY2(closeTo(actual, expected),
			qPrintable(QStringLiteral("base degree -2 should shift the reference up to its equivalent octave: got %1 Hz, expected %2 Hz")
				.arg(actual).arg(expected)));
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

	//! Luna 3530: MaxSourceLength is a stated cap (include/SessionTuning.h:88),
	//! and setState() already refuses an over-cap source. The OTHER mutating
	//! entry point did not: setNoteFrequency() handed the source straight to
	//! commit(), which silently kept source.left(MaxSourceLength) - so a caller
	//! asking for a 300-character label got a success and a 256-character one.
	//! Over-cap is REFUSED, typed, writing nothing (the same rule setState
	//! applies), and the exactly-at-the-cap source still succeeds.
	void aSourceOverTheCapIsRefusedRatherThanSilentlyTruncated()
	{
		SessionTuning* tuning = SessionTuning::instance();
		tuning->reset();
		QTemporaryDir dir;
		QVERIFY(dir.isValid());

		// A long .scl PATH: loadScaleFile() passes the path itself as the
		// source, so a file under a deep enough directory reaches commit() the
		// same way a long label does.
		// Segments stay well under the 255-byte NAME_MAX so the deep path really
		// is created; the whole PATH is what has to pass the source cap.
		QString deepPath = dir.path();
		while (deepPath.size() < SessionTuning::MaxSourceLength + 40)
		{
			const QString segment(60, QLatin1Char('d'));
			deepPath += QLatin1Char('/') + segment;
			if (!QDir().mkpath(deepPath)) { break; }
		}
		QVERIFY2(deepPath.size() > SessionTuning::MaxSourceLength,
			"the deep fixture path did not pass the source cap, so this half cannot run");
		const QString sclPath = deepPath + QStringLiteral("/ji3.scl");
		QFile scl(sclPath);
		QVERIFY(scl.open(QIODevice::WriteOnly | QIODevice::Text));
		scl.write(jiSclLines().join(QLatin1Char('\n')).toUtf8() + "\n");
		scl.close();

		const QJsonObject beforeLoad = snapshotState(run(QStringLiteral("mts.get_state")).result);
		const ControlResult loaded = run(QStringLiteral("mts.load_scale"),
			QJsonObject{{QStringLiteral("path"), sclPath}});
		QVERIFY2(!loaded.ok,
			"a scale whose path is over the source cap loaded successfully: the "
			"recorded source was silently truncated");
		QCOMPARE(loaded.errorKind, ControlErrorKind::Refused);
		int mismatch = 0;
		const QJsonObject afterLoad = snapshotState(run(QStringLiteral("mts.get_state")).result);
		QVERIFY2(sameTable(beforeLoad, afterLoad, &mismatch),
			qPrintable(QStringLiteral("the refused load still changed the table at key %1")
				.arg(mismatch)));

		// The note path: an over-cap source label is refused, and mts.set_note
		// does NOT push an undo step for the refusal.
		const int depthBefore = Engine::projectJournal()->undoDepth();
		const QString longSource(SessionTuning::MaxSourceLength + 1, QLatin1Char('s'));
		const ControlResult refused = run(QStringLiteral("mts.set_note"),
			QJsonObject{{QStringLiteral("note"), 5}, {QStringLiteral("frequency"), 500.0},
				{QStringLiteral("source"), longSource}});
		QVERIFY2(!refused.ok,
			"an over-cap source was accepted: m_source silently lost its tail");
		QCOMPARE(Engine::projectJournal()->undoDepth(), depthBefore);

		// Exactly at the cap is legal - the boundary is inclusive.
		const QString atCap(SessionTuning::MaxSourceLength, QLatin1Char('t'));
		QVERIFY2(run(QStringLiteral("mts.set_note"),
			QJsonObject{{QStringLiteral("note"), 5}, {QStringLiteral("frequency"), 500.0},
				{QStringLiteral("source"), atCap}}).ok,
			"a source of exactly MaxSourceLength characters was refused: the cap is inclusive");
		QCOMPARE(run(QStringLiteral("mts.get_state")).result
			.value(QStringLiteral("source")).toString().size(), qint64(SessionTuning::MaxSourceLength));
		tuning->reset();
	}

	//! GUI-fed scales use their description as the table source. An over-cap
	//! description must leave both the current table and its built-from scale.
	void anOverCapScaleFeedLeavesTheSessionTuningUnchanged()
	{
		Song* song = Engine::getSong();
		song->clearProject();
		SessionTuning* tuning = SessionTuning::instance();
		tuning->reset();
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString path = writeJiScl(dir);
		QVERIFY2(run(QStringLiteral("mts.load_scale"),
			QJsonObject{{QStringLiteral("path"), path}}).ok, "the JI fixture did not load");

		const QJsonObject before = tuning->stateJson();
		const std::shared_ptr<const Scale> beforeScale = tuning->scale();
		const QString longDescription(SessionTuning::MaxSourceLength + 1, QLatin1Char('s'));
		auto overCapScale = std::make_shared<Scale>(longDescription,
			beforeScale->getIntervals());
		tuning->feedScale(overCapScale, longDescription);

		const bool tableUnchanged = tuning->stateJson() == before;
		const bool scaleUnchanged = tuning->scale() == beforeScale;
		QVERIFY2(tableUnchanged && scaleUnchanged,
			"an over-cap GUI scale feed changed the table or its built-from scale");
		tuning->reset();
	}

	//! GUI-fed keymaps use their description as the table source. An over-cap
	//! description must leave both the current table and its built-from keymap.
	void anOverCapKeymapFeedLeavesTheSessionTuningUnchanged()
	{
		Song* song = Engine::getSong();
		song->clearProject();
		SessionTuning* tuning = SessionTuning::instance();
		tuning->reset();
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString path = writeJiScl(dir);
		QVERIFY2(run(QStringLiteral("mts.load_scale"),
			QJsonObject{{QStringLiteral("path"), path}}).ok, "the JI fixture did not load");

		const QJsonObject before = tuning->stateJson();
		const std::shared_ptr<const Keymap> beforeKeymap = tuning->keymap();
		const QString longDescription(SessionTuning::MaxSourceLength + 1, QLatin1Char('k'));
		auto overCapKeymap = copyOfKeymap(beforeKeymap);
		overCapKeymap->setDescription(longDescription);
		tuning->feedKeymap(overCapKeymap, longDescription);

		const bool tableUnchanged = tuning->stateJson() == before;
		const bool keymapUnchanged = tuning->keymap() == beforeKeymap;
		QVERIFY2(tableUnchanged && keymapUnchanged,
			"an over-cap GUI keymap feed changed the table or its built-from keymap");
		tuning->reset();
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

	//! Luna 4449: an .scl ratio line is n/d. The denominator parse ignored its
	//! own result and BOTH halves were clamped up to 1, so 3/nope loaded as 3/1
	//! and 0/2 as 1/2 - the table then held an interval the file never asked for.
	//! A line that is not a positive ratio is a bad LINE: it is refused, typed,
	//! and the table that was already loaded is left standing.
	void anInvalidRatioLineIsRefusedInsteadOfRewrittenToADifferentOne()
	{
		Song* song = Engine::getSong();
		song->clearProject();
		SessionTuning* tuning = SessionTuning::instance();
		tuning->reset();

		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString good = writeJiScl(dir);
		QVERIFY(!good.isEmpty());
		QVERIFY2(run(QStringLiteral("mts.load_scale"),
			QJsonObject{{QStringLiteral("path"), good}}).ok, "the JI fixture did not load");
		const QJsonObject before = snapshotState(run(QStringLiteral("mts.get_state")).result);

		// A .scl is name line, degree count, then that many interval lines.
		// Three interval lines for the declared three degrees - the point of
		// these bodies is the RATIO on one of them, so none of them may also be
		// a truncated scale.
		const QStringList badOne{QStringLiteral("bad ratio fixture"), QStringLiteral("3"),
			QStringLiteral("1/1"), QStringLiteral("3/nope"), QStringLiteral("3/2")};
		const QStringList badZeroDen{QStringLiteral("bad ratio fixture"), QStringLiteral("3"),
			QStringLiteral("1/1"), QStringLiteral("3/0"), QStringLiteral("3/2")};
		const QStringList badExtraSlash{QStringLiteral("bad ratio fixture"), QStringLiteral("3"),
			QStringLiteral("1/1"), QStringLiteral("3/2/7"), QStringLiteral("3/2")};
		const QStringList badZeroNum{QStringLiteral("bad ratio fixture"), QStringLiteral("3"),
			QStringLiteral("1/1"), QStringLiteral("0/2"), QStringLiteral("3/2")};
		const QStringList badInMiddle{QStringLiteral("bad ratio fixture"), QStringLiteral("3"),
			QStringLiteral("1/1"), QStringLiteral("3/nope"), QStringLiteral("3/2")};
		const QVector<QStringList> badBodies{badOne, badZeroDen, badExtraSlash, badZeroNum, badInMiddle};
		for (const QStringList& body : badBodies)
		{
			const QString path = writeScl(dir, QStringLiteral("bad-%1.scl").arg(qHash(body)), body);
			QVERIFY2(!path.isEmpty(), "the fixture .scl could not be written");
			const ControlResult loaded = run(QStringLiteral("mts.load_scale"),
				QJsonObject{{QStringLiteral("path"), path}});
			QVERIFY2(!loaded.ok, qPrintable(QStringLiteral("'%1' was accepted: the bad ratio "
				"line became a different, valid interval").arg(body.at(3))));
			QCOMPARE(loaded.errorKind, ControlErrorKind::Refused);

			int mismatch = 0;
			const QJsonObject after = snapshotState(run(QStringLiteral("mts.get_state")).result);
			QVERIFY2(sameTable(before, after, &mismatch),
				qPrintable(QStringLiteral("the refused scale still changed the table at key %1")
					.arg(mismatch)));
		}

		// Scala ignores description text after the interval value, including
		// slashes inside that text.
		const QStringList describedRatio{QStringLiteral("ratio with description"),
			QStringLiteral("3"), QStringLiteral("1/1"),
			QStringLiteral("3/2 description/variant"), QStringLiteral("3/2")};
		const QString describedPath = writeScl(dir, QStringLiteral("described-ratio.scl"),
			describedRatio);
		QVERIFY2(!describedPath.isEmpty(), "the described-ratio .scl could not be written");
		const ControlResult describedLoad = run(QStringLiteral("mts.load_scale"),
			QJsonObject{{QStringLiteral("path"), describedPath}});
		QVERIFY2(describedLoad.ok, qPrintable(QStringLiteral("valid trailing description was refused: %1")
			.arg(describedLoad.errorMessage)));
	}

	//! A .kbm with the standard 12-TET shape but \a baseFrequency in field 5 -
//! everything else exactly what a hand-written keymap carries.
QString writeKbm(QTemporaryDir& dir, const QString& name, const QString& baseFrequency)
{
	return writeScl(dir, name,
		// size, then first / last / middle / base key - the whole keyboard, so
		// every key (base included) is mapped by the lines below.
		{QStringLiteral("12"),
			QStringLiteral("0"),
			QStringLiteral("127"),
			QStringLiteral("0"),
			QStringLiteral("0"),
			baseFrequency,
			QStringLiteral("12"),
			// 12 mapping lines, one per scale degree (x = unmapped).
			QStringLiteral("0"), QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3"),
			QStringLiteral("4"), QStringLiteral("5"), QStringLiteral("6"), QStringLiteral("7"),
			QStringLiteral("8"), QStringLiteral("9"), QStringLiteral("10"), QStringLiteral("11")});
}

//! Luna 5317: noteToFreq() hands the table to the render path as a FLOAT, so a
	//! built entry has to be inside float's range before it is committed. A .kbm
	//! base frequency of 1e100 parses fine and builds fine as a double, and
	//! arrives at the audio thread as infinity - so such a table is refused whole,
	//! leaving the one already loaded standing.
	void aKeymapThatWouldOverflowFloatIsRefusedWithTheTableLeftStanding()
	{
		Song* song = Engine::getSong();
		song->clearProject();
		SessionTuning* tuning = SessionTuning::instance();
		tuning->reset();

		QTemporaryDir dir;
		QVERIFY(dir.isValid());

		// A keymap is CONTENT over whatever scale is loaded, so the test loads
		// the 12-degree scale its keymap maps (reset() leaves the session's
		// scale alone, and a mismatched pair is not what this case is about).
		QStringList twelveTetScale{QStringLiteral("12-tone scale for board card #712"),
			QStringLiteral("12")};
		for (int degree = 0; degree < 12; ++degree)
		{
			twelveTetScale.append(QStringLiteral("%1/1").arg(degree + 1));
		}
		const QString scalePath = writeScl(dir, QStringLiteral("twelve.scl"), twelveTetScale);
		QVERIFY2(!scalePath.isEmpty(), "the 12-tone .scl could not be written");
		QVERIFY2(run(QStringLiteral("mts.load_scale"),
			QJsonObject{{QStringLiteral("path"), scalePath}}).ok, "the 12-tone scale did not load");

		const QString good = writeKbm(dir, QStringLiteral("good.kbm"), QStringLiteral("440.0"));
		QVERIFY2(!good.isEmpty(), "the fixture .kbm could not be written");
		QVERIFY2(run(QStringLiteral("mts.load_keymap"),
			QJsonObject{{QStringLiteral("path"), good}}).ok, "the 440 Hz fixture did not load");
		const QJsonObject before = snapshotState(run(QStringLiteral("mts.get_state")).result);

		// Finite as a double, far past float's maximum.
		const QString huge = writeKbm(dir, QStringLiteral("huge.kbm"), QStringLiteral("1e100"));
		QVERIFY2(!huge.isEmpty(), "the huge .kbm could not be written");
		const ControlResult loaded = run(QStringLiteral("mts.load_keymap"),
			QJsonObject{{QStringLiteral("path"), huge}});
		QVERIFY2(!loaded.ok, "a keymap whose frequencies overflow float was accepted: the "
			"render path would read them as infinity");
		QCOMPARE(loaded.errorKind, ControlErrorKind::Refused);

		int mismatch = 0;
		const QJsonObject after = snapshotState(run(QStringLiteral("mts.get_state")).result);
		QVERIFY2(sameTable(before, after, &mismatch),
			qPrintable(QStringLiteral("the refused keymap still changed the table at key %1")
				.arg(mismatch)));

		// And nothing in the standing table is out of the render path's range.
		for (int key = 0; key < SessionTuning::TableSize; ++key)
		{
			const float frequency = tuning->noteToFreq(key);
			QVERIFY2(std::isfinite(frequency),
				qPrintable(QStringLiteral("key %1 reads %2 Hz through the render path")
					.arg(key).arg(frequency)));
		}
	}

	//! Luna 5318: a .scl's second non-comment line DECLARES how many degrees the
	//! scale has, so a file that ends early has not declared the scale it carries.
	//! readScaleLines() only checked that it read at least one interval, so a file
	//! declaring 5 degrees and holding 1 loaded happily as a 1-degree scale - a
	//! table built from a scale the file never described. The truncation is
	//! refused, typed, and the standing table is left alone.
	void aScaleFileShorterThanItsDeclaredDegreeCountIsRefused()
	{
		Song* song = Engine::getSong();
		song->clearProject();
		SessionTuning* tuning = SessionTuning::instance();
		tuning->reset();

		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString good = writeJiScl(dir);
		QVERIFY(!good.isEmpty());
		QVERIFY2(run(QStringLiteral("mts.load_scale"),
			QJsonObject{{QStringLiteral("path"), good}}).ok, "the JI fixture did not load");
		const QJsonObject before = snapshotState(run(QStringLiteral("mts.get_state")).result);

		// Declares five degrees, carries one interval: it would otherwise build a
		// one-degree scale out of the file's own 5-degree name.
		const QStringList truncated{QStringLiteral("a five degree scale, truncated"),
			QStringLiteral("5"), QStringLiteral("9/8")};
		const QString path = writeScl(dir, QStringLiteral("truncated.scl"), truncated);
		QVERIFY2(!path.isEmpty(), "the fixture .scl could not be written");
		const ControlResult loaded = run(QStringLiteral("mts.load_scale"),
			QJsonObject{{QStringLiteral("path"), path}});
		QVERIFY2(!loaded.ok, "a .scl with fewer interval lines than its declared degree "
			"count was accepted: it built a scale the file never described");
		QCOMPARE(loaded.errorKind, ControlErrorKind::Refused);

		int mismatch = 0;
		const QJsonObject after = snapshotState(run(QStringLiteral("mts.get_state")).result);
		QVERIFY2(sameTable(before, after, &mismatch),
			qPrintable(QStringLiteral("the refused scale still changed the table at key %1")
				.arg(mismatch)));
	}

	//! An .scl has to NAME the scale and DECLARE how many degrees it has before
	//! it can describe one. readScaleLines() left its degree count at zero when
	//! the stream carried neither header line, and its "the file carries as many
	//! interval lines as it declared" check compared zero against zero - so an
	//! empty .scl was accepted, and the scale built from it held the single 1/1
	//! interval that loadScaleFile() always starts with: a table in which every
	//! key has the same pitch. Such a file is refused, typed, and the standing
	//! table is left alone.
	void aScaleFileWithoutNameOrDegreeCountIsRefused()
	{
		Song* song = Engine::getSong();
		song->clearProject();
		SessionTuning* tuning = SessionTuning::instance();
		tuning->reset();

		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString good = writeJiScl(dir);
		QVERIFY(!good.isEmpty());
		QVERIFY2(run(QStringLiteral("mts.load_scale"),
			QJsonObject{{QStringLiteral("path"), good}}).ok, "the JI fixture did not load");
		const QJsonObject before = snapshotState(run(QStringLiteral("mts.get_state")).result);

		// A zero-byte file: no name and no degree count at all.
		const QString empty = dir.filePath(QStringLiteral("empty.scl"));
		QFile emptyFile(empty);
		QVERIFY(emptyFile.open(QIODevice::WriteOnly | QIODevice::Text));
		emptyFile.close();
		// A file that names the scale but never declares how many degrees it has.
		const QString nameOnly = writeScl(dir, QStringLiteral("name-only.scl"),
			{QStringLiteral("missing degree count")});
		QVERIFY(!nameOnly.isEmpty());

		const QStringList incompletePaths{empty, nameOnly};
		for (const QString& path : incompletePaths)
		{
			const ControlResult loaded = run(QStringLiteral("mts.load_scale"),
				QJsonObject{{QStringLiteral("path"), path}});
			QVERIFY2(!loaded.ok, qPrintable(QStringLiteral("incomplete .scl '%1' was accepted: "
				"it built a one-interval scale whose every key has the same pitch")
				.arg(path)));
			QCOMPARE(loaded.errorKind, ControlErrorKind::Refused);

			int mismatch = 0;
			const QJsonObject after = snapshotState(run(QStringLiteral("mts.get_state")).result);
			QVERIFY2(sameTable(before, after, &mismatch),
				qPrintable(QStringLiteral("the refused scale changed the table at key %1")
					.arg(mismatch)));
		}
	}

	//! A .kbm with the standard 12-TET shape but \a declaredSize in field 0 and
//! exactly \a mappingLines after the header, for the keymaps this test writes
//! itself.
QString writeKbmSized(QTemporaryDir& dir, const QString& name, const QString& declaredSize,
	const QStringList& headerFields, const QStringList& mappingLines)
{
	QStringList body{declaredSize};
	body += headerFields;
	body.append(QStringLiteral("12")); // octave degree, ignored
	body += mappingLines;
	return writeScl(dir, name, body);
}

	//! Luna 5320: a .kbm's first field is the SIZE of the mapping that follows -
	//! the number of mapping lines the file promises. Only its positivity was
	//! checked, so a file declaring a 12-entry map and carrying one line built a
	//! Keymap whose map period is 1, and getDegree() then answered with that one
	//! degree for every key on the keyboard. The mismatch is refused, typed, and
	//! the standing table is left alone.
	void aKeymapWhoseMappingLinesDoNotMatchItsDeclaredSizeIsRefused()
	{
		Song* song = Engine::getSong();
		song->clearProject();
		SessionTuning* tuning = SessionTuning::instance();
		tuning->reset();

		QTemporaryDir dir;
		QVERIFY(dir.isValid());

		// first / last / middle / base key, then the base frequency: the shape
		// writeKbm() hands the parser, so only the size and the lines below it move.
		const QStringList keys{QStringLiteral("0"), QStringLiteral("127"),
			QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("440.0")};
		QStringList twelveLines;
		for (int degree = 0; degree < 12; ++degree)
		{
			twelveLines.append(QString::number(degree));
		}

		QStringList scaleLines{QStringLiteral("12-tone scale for Luna 5320"),
			QStringLiteral("12")};
		for (int degree = 0; degree < 12; ++degree)
		{
			scaleLines.append(QStringLiteral("%1/1").arg(degree + 1));
		}
		const QString scalePath = writeScl(dir, QStringLiteral("twelve.scl"), scaleLines);
		QVERIFY2(!scalePath.isEmpty(), "the 12-tone .scl could not be written");
		QVERIFY2(run(QStringLiteral("mts.load_scale"),
			QJsonObject{{QStringLiteral("path"), scalePath}}).ok, "the 12-tone scale did not load");

		const QString good = writeKbmSized(dir, QStringLiteral("good.kbm"),
			QStringLiteral("12"), keys, twelveLines);
		QVERIFY2(!good.isEmpty(), "the fixture .kbm could not be written");
		QVERIFY2(run(QStringLiteral("mts.load_keymap"),
			QJsonObject{{QStringLiteral("path"), good}}).ok,
			"the matching-size keymap did not load");
		const QJsonObject before = snapshotState(run(QStringLiteral("mts.get_state")).result);

		// Declares 12, carries one: the map would repeat with a one-entry period.
		const QString shortMap = writeKbmSized(dir, QStringLiteral("short.kbm"),
			QStringLiteral("12"), keys, QStringList{QStringLiteral("0")});
		QVERIFY2(!shortMap.isEmpty(), "the short fixture .kbm could not be written");
		// And the other direction: declares 3, carries twelve.
		const QString longMap = writeKbmSized(dir, QStringLiteral("long.kbm"),
			QStringLiteral("3"), keys, twelveLines);
		QVERIFY2(!longMap.isEmpty(), "the long fixture .kbm could not be written");
		for (const QString& path : QStringList{shortMap, longMap})
		{
			const ControlResult loaded = run(QStringLiteral("mts.load_keymap"),
				QJsonObject{{QStringLiteral("path"), path}});
			QVERIFY2(!loaded.ok, qPrintable(QStringLiteral("'%1' was accepted: its mapping "
				"does not have the period it declares").arg(path)));
			QCOMPARE(loaded.errorKind, ControlErrorKind::Refused);
		}

		int mismatch = 0;
		const QJsonObject after = snapshotState(run(QStringLiteral("mts.get_state")).result);
		QVERIFY2(sameTable(before, after, &mismatch),
			qPrintable(QStringLiteral("the refused keymap still changed the table at key %1")
				.arg(mismatch)));
	}

	//! Luna 5321: the four key fields were read with toInt() and no ok check, so
	//! "oops" became 0 and was then clamped into range like a real key - a middle
	//! key of 0 instead of a refusal, and a table built from a keymap nobody
	//! wrote. Each key field's conversion is checked; an invalid one is refused,
	//! typed, and the standing table is left alone.
	void aKeymapWithAMalformedKeyFieldIsRefused()
	{
		Song* song = Engine::getSong();
		song->clearProject();
		SessionTuning* tuning = SessionTuning::instance();
		tuning->reset();

		QTemporaryDir dir;
		QVERIFY(dir.isValid());

		QStringList scaleLines{QStringLiteral("12-tone scale for Luna 5321"),
			QStringLiteral("12")};
		for (int degree = 0; degree < 12; ++degree)
		{
			scaleLines.append(QStringLiteral("%1/1").arg(degree + 1));
		}
		const QString scalePath = writeScl(dir, QStringLiteral("twelve.scl"), scaleLines);
		QVERIFY2(!scalePath.isEmpty(), "the 12-tone .scl could not be written");
		QVERIFY2(run(QStringLiteral("mts.load_scale"),
			QJsonObject{{QStringLiteral("path"), scalePath}}).ok, "the 12-tone scale did not load");

		const QStringList goodKeys{QStringLiteral("0"), QStringLiteral("127"),
			QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("440.0")};
		QStringList twelveLines;
		for (int degree = 0; degree < 12; ++degree)
		{
			twelveLines.append(QString::number(degree));
		}
		const QString good = writeKbmSized(dir, QStringLiteral("good.kbm"),
			QStringLiteral("12"), goodKeys, twelveLines);
		QVERIFY2(!good.isEmpty(), "the fixture .kbm could not be written");
		QVERIFY2(run(QStringLiteral("mts.load_keymap"),
			QJsonObject{{QStringLiteral("path"), good}}).ok, "the good keymap did not load");
		const QJsonObject before = snapshotState(run(QStringLiteral("mts.get_state")).result);

		// One field at a time, each in the position it occupies on disk: first,
		// last, middle and base key. The malformed text is the whole field, so
		// these cannot pass as something else.
		const QVector<QPair<QString, QStringList>> badKeys{
			{QStringLiteral("first"), {QStringLiteral("oops"), QStringLiteral("127"),
				QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("440.0")}},
			{QStringLiteral("last"), {QStringLiteral("0"), QStringLiteral("nope"),
				QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("440.0")}},
			{QStringLiteral("middle"), {QStringLiteral("0"), QStringLiteral("127"),
				QStringLiteral("oops"), QStringLiteral("0"), QStringLiteral("440.0")}},
			{QStringLiteral("base"), {QStringLiteral("0"), QStringLiteral("127"),
				QStringLiteral("0"), QStringLiteral("oops"), QStringLiteral("440.0")}},
			// A fractional key is not a MIDI key either, and it used to be
			// truncated into a plausible one.
			{QStringLiteral("fractional"), {QStringLiteral("0"), QStringLiteral("127"),
				QStringLiteral("0"), QStringLiteral("0.5"), QStringLiteral("440.0")}},
		};
		for (const auto& badKey : badKeys)
		{
			const QString path = writeKbmSized(dir,
				QStringLiteral("badkey-%1.kbm").arg(badKey.first), QStringLiteral("12"),
				badKey.second, twelveLines);
			QVERIFY2(!path.isEmpty(), "the fixture .kbm could not be written");
			const ControlResult loaded = run(QStringLiteral("mts.load_keymap"),
				QJsonObject{{QStringLiteral("path"), path}});
			QVERIFY2(!loaded.ok, qPrintable(QStringLiteral("a .kbm whose %1 key field is "
				"malformed was accepted: it loaded as a real key").arg(badKey.first)));
			QCOMPARE(loaded.errorKind, ControlErrorKind::Refused);
		}

		int mismatch = 0;
		const QJsonObject after = snapshotState(run(QStringLiteral("mts.get_state")).result);
		QVERIFY2(sameTable(before, after, &mismatch),
			qPrintable(QStringLiteral("the refused keymap still changed the table at key %1")
				.arg(mismatch)));
	}

	//! Positive doubles that narrow to float zero are not usable render
	//! frequencies. Mapped zero is different from the intentional unmapped marker.
	void aKeymapFrequencyThatUnderflowsTheRenderFloatIsRefused()
	{
		Song* song = Engine::getSong();
		song->clearProject();
		SessionTuning* tuning = SessionTuning::instance();
		tuning->reset();
		QTemporaryDir dir;
		QVERIFY(dir.isValid());

		QStringList scaleLines{QStringLiteral("12-tone scale"), QStringLiteral("12")};
		for (int degree = 0; degree < 12; ++degree)
		{
			scaleLines.append(QStringLiteral("%1/1").arg(degree + 1));
		}
		const QString scalePath = writeScl(dir, QStringLiteral("twelve.scl"), scaleLines);
		QVERIFY(!scalePath.isEmpty());
		QVERIFY2(run(QStringLiteral("mts.load_scale"),
			QJsonObject{{QStringLiteral("path"), scalePath}}).ok, "the scale did not load");
		const QJsonObject before = snapshotState(run(QStringLiteral("mts.get_state")).result);

		const QString tinyBase = writeKbm(dir, QStringLiteral("tiny-base.kbm"),
			QStringLiteral("1e-100"));
		QVERIFY(!tinyBase.isEmpty());
		const ControlResult tiny = run(QStringLiteral("mts.load_keymap"),
			QJsonObject{{QStringLiteral("path"), tinyBase}});
		QVERIFY2(!tiny.ok, "a positive .kbm base frequency that narrows to float zero was accepted");
		QCOMPARE(tiny.errorKind, ControlErrorKind::Refused);

		// This base is the smallest positive float. Key 0 is mapped one scale
		// octave below the reference, so its positive double narrows to float 0.
		const QStringList lowHeader{QStringLiteral("0"), QStringLiteral("127"),
			QStringLiteral("127"), QStringLiteral("127"), QStringLiteral("1.40129846e-45")};
		QStringList wideMap;
		for (int degree = 0; degree < 128; ++degree)
		{
			wideMap.append(degree == 2 ? QStringLiteral("x") : QStringLiteral("0"));
		}
		const QString lowPath = writeKbmSized(dir, QStringLiteral("low-render-frequency.kbm"),
			QStringLiteral("128"), lowHeader, wideMap);
		QVERIFY(!lowPath.isEmpty());
		const ControlResult low = run(QStringLiteral("mts.load_keymap"),
			QJsonObject{{QStringLiteral("path"), lowPath}});
		QVERIFY2(!low.ok, "a mapped positive frequency that narrows to float zero was accepted");
		QCOMPARE(low.errorKind, ControlErrorKind::Refused);

		int mismatch = 0;
		QVERIFY2(sameTable(before, snapshotState(run(QStringLiteral("mts.get_state")).result),
			&mismatch), qPrintable(QStringLiteral("a refused keymap changed the table at key %1")
				.arg(mismatch)));

		// An actual 'x' entry remains a valid zero in the table.
		QStringList sparseMap;
		for (int degree = 0; degree < 12; ++degree)
		{
			sparseMap.append(degree == 1 ? QStringLiteral("x") : QString::number(degree));
		}
		const QString sparsePath = writeKbmSized(dir, QStringLiteral("sparse.kbm"),
			QStringLiteral("12"), {QStringLiteral("0"), QStringLiteral("127"),
				QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("440.0")}, sparseMap);
		QVERIFY(!sparsePath.isEmpty());
		QVERIFY2(run(QStringLiteral("mts.load_keymap"),
			QJsonObject{{QStringLiteral("path"), sparsePath}}).ok,
			"a keymap's intentional unmapped entry was rejected");
		QCOMPARE(tuning->noteToFreq(1), 0.0f);
		QVERIFY(tuning->noteToFreq(0) > 0.0f);
	}

	//! Project keymap XML used to retain arbitrary integer key fields, so a
	//! minimum middle_key overflowed key - middle_key during pitch mapping. The
	//! XML loader applies the same MIDI bounds as the .kbm loader, and Keymap's
	//! public constructor still gets overflow-safe offset arithmetic.
	void aKeymapWithOutOfRangeLoadedMiddleKeyIsClampedBeforeMapping()
	{
		QDomDocument document(QStringLiteral("keymap-test"));
		QDomElement element = document.createElement(QStringLiteral("keymap"));
		document.appendChild(element);
		element.setAttribute(QStringLiteral("first_key"), 0);
		element.setAttribute(QStringLiteral("last_key"), 127);
		element.setAttribute(QStringLiteral("middle_key"),
			std::numeric_limits<int>::min());
		element.setAttribute(QStringLiteral("base_key"), 0);
		element.setAttribute(QStringLiteral("base_freq"), 440.0);
		std::vector<int> degrees;
		for (int degree = 0; degree < 12; ++degree)
		{
			degrees.push_back(degree);
			QDomElement degreeElement = document.createElement(QStringLiteral("degree"));
			degreeElement.setAttribute(QStringLiteral("value"), degree);
			element.appendChild(degreeElement);
		}

		Keymap loaded;
		loaded.loadSettings(element);
		QCOMPARE(loaded.getMiddleKey(), 0);
		QCOMPARE(loaded.getDegree(60), 0);
		QCOMPARE(loaded.getOctave(60), 5);

		Keymap directlyConstructed(QStringLiteral("extreme"), std::move(degrees),
			0, 127, std::numeric_limits<int>::min(), 0, 440.0f);
		QCOMPARE(directlyConstructed.getDegree(60), 8);
		QCOMPARE(directlyConstructed.getOctave(60), 178956975);
	}

	//! The recorded undo step restores the Scale/Keymap the table was BUILT from,
	//! not just the frequencies: stateJson() carries the table alone, so without
	//! this mts.get_state kept reporting the scale an undone load had installed.
	void undoOfALoadScaleAlsoUndoesTheScaleItInstalled()
	{
		Song* song = Engine::getSong();
		song->clearProject();
		SessionTuning* tuning = SessionTuning::instance();
		tuning->reset();

		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		// Its OWN scale file, so the description this slot installs cannot be
		// the one an earlier slot left behind: the undo must restore whatever was
		// reported BEFORE the load, whatever that was.
		const QString path = dir.filePath(QStringLiteral("theory-undo.scl"));
		QFile file(path);
		QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
		file.write("! theory-undo.scl\nthe scale this slot installs\n2\n1/1\n9/8\n");
		file.close();

		const QString reportedBefore = run(QStringLiteral("mts.get_state")).result
			.value(QStringLiteral("scale")).toString();
		const ControlResult loaded = run(QStringLiteral("mts.load_scale"),
			QJsonObject{{QStringLiteral("path"), path}});
		QVERIFY2(loaded.ok, qPrintable(loaded.errorMessage));
		const QString installed = run(QStringLiteral("mts.get_state")).result
			.value(QStringLiteral("scale")).toString();
		QCOMPARE(installed, QStringLiteral("the scale this slot installs"));

		REV_UNDO_OR_FAIL();
		QVERIFY2(run(QStringLiteral("mts.get_state")).result
				.value(QStringLiteral("scale")).toString() == reportedBefore,
			qPrintable(QStringLiteral("control.undo restored the table but mts.get_state "
				"still reports '%1' instead of the pre-load '%2'")
				.arg(installed, reportedBefore)));
		tuning->reset();
	}

	void localMtsTuningDumpRequestPreservesScaleName()
	{
		MtsClientPtr client(MTS_RegisterClient(), &MTS_DeregisterClient);
		QVERIFY(client != nullptr);
		seedLocalMtsScale(client.get());
		const QByteArray name(MTS_GetScaleName(client.get()));
		QCOMPARE(name, QByteArrayLiteral("Test scale"));

		sendLocalMtsTuningDumpRequest(client.get());

		QCOMPARE(QByteArray(MTS_GetScaleName(client.get())), name);
	}

	void localMtsFrequencyToNoteMapsNanToFallback()
	{
		MtsClientPtr client(MTS_RegisterClient(), &MTS_DeregisterClient);
		QVERIFY(client != nullptr);
		const double nan = std::numeric_limits<double>::quiet_NaN();
		const char expected = MTS_FrequencyToNote(nullptr, nan, -1);
		QCOMPARE(expected, static_cast<char>(60));

		QCOMPARE(MTS_FrequencyToNote(client.get(), nan, -1), expected);

		signed char channel = -1;
		QCOMPARE(MTS_FrequencyToNoteAndChannel(client.get(), nan, &channel), expected);
		QCOMPARE(channel, static_cast<signed char>(0));
	}

	void localMtsTuningDumpRequestPreservesMapMetadata()
	{
		MtsClientPtr client(MTS_RegisterClient(), &MTS_DeregisterClient);
		QVERIFY(client != nullptr);
		seedLocalMtsScale(client.get());
		QCOMPARE(MTS_GetMapSize(client.get()), static_cast<signed char>(12));
		QCOMPARE(MTS_GetMapStartKey(client.get()), static_cast<signed char>(60));
		const double frequency = MTS_NoteToFrequency(client.get(), 0, 0);
		const double expectedFrequency = 440.0 * std::pow(2.0, (0.01 - 69.0) / 12.0);
		QVERIFY(std::abs(frequency - expectedFrequency) < expectedFrequency * 1e-12);

		sendLocalMtsTuningDumpRequest(client.get());

		QCOMPARE(MTS_GetMapSize(client.get()), static_cast<signed char>(12));
		QCOMPARE(MTS_GetMapStartKey(client.get()), static_cast<signed char>(60));
		QCOMPARE(MTS_NoteToFrequency(client.get(), 0, 0), frequency);
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
			// A repeated disarm changes nothing and takes no undo step, so ONE undo
			// must reach the real disarm above and bring the master back.
			const ControlResult repeated = run(QStringLiteral("mts.master_set"),
				QJsonObject{{QStringLiteral("enabled"), false}});
			QVERIFY2(repeated.ok, qPrintable(repeated.errorMessage));
			REV_UNDO_OR_FAIL();
			QVERIFY(run(QStringLiteral("mts.get_state")).result
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

		// Disarming what was never armed is idempotent, and a value the flag already
		// has changes nothing, so it takes NO undo step - the refused arm above
		// recorded none either.
		const ControlResult disarmed = run(QStringLiteral("mts.master_set"),
			QJsonObject{{QStringLiteral("enabled"), false}});
		QVERIFY2(disarmed.ok, qPrintable(disarmed.errorMessage));
		QCOMPARE(Engine::projectJournal()->undoDepth(), depthBefore);
	}

	//! A repeated mts.master_set changes nothing and must not sit on the undo
	//! stack: the real table edit before it is the step control.undo unwinds.
	void repeatedMasterSetLeavesTheRealEditForUndo()
	{
		Song* song = Engine::getSong();
		song->clearProject();
		const ControlResult firstDisarm = run(QStringLiteral("mts.master_set"),
			QJsonObject{{QStringLiteral("enabled"), false}});
		QVERIFY2(firstDisarm.ok, qPrintable(firstDisarm.errorMessage));
		const int depthBefore = Engine::projectJournal()->undoDepth();
		const QJsonObject before = snapshotState(run(QStringLiteral("mts.get_state")).result);

		const ControlResult noted = run(QStringLiteral("mts.set_note"),
			QJsonObject{{QStringLiteral("note"), 60}, {QStringLiteral("frequency"), 432.0},
				{QStringLiteral("source"), QStringLiteral("repeated-master-set")}});
		QVERIFY2(noted.ok, qPrintable(noted.errorMessage));
		QCOMPARE(Engine::projectJournal()->undoDepth(), depthBefore + 1);

		const ControlResult repeated = run(QStringLiteral("mts.master_set"),
			QJsonObject{{QStringLiteral("enabled"), false}});
		QVERIFY2(repeated.ok, qPrintable(repeated.errorMessage));
		QCOMPARE(Engine::projectJournal()->undoDepth(), depthBefore + 1);

		REV_UNDO_OR_FAIL();
		int mismatchKey = 0;
		const QJsonObject undone = snapshotState(run(QStringLiteral("mts.get_state")).result);
		QVERIFY2(sameTable(before, undone, &mismatchKey),
			qPrintable(QStringLiteral("control.undo unwound the repeated mts.master_set "
				"instead of the set_note before it (first difference: %1)").arg(mismatchKey)));
		QCOMPARE(Engine::projectJournal()->undoDepth(), depthBefore);
	}

};

QTEST_GUILESS_MAIN(SessionTuningTest)
#include "SessionTuningTest.moc"
