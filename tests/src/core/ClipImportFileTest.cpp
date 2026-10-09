/*
 * ClipImportFileTest.cpp - clip.import_file and the MIDI note reader behind it
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
/*! 0.5.0 L04 (Q050-031): a MIDI or audio file becomes a clip at a tick - on a named track or on a new one - as one
 *  undo step, and a file that cannot be read changes NOTHING: no clip, no track, no undo step. The reader behind
 *  it is also fed damaged bytes (every cut of a valid file, flipped bytes, noise) and must answer, never crash. */

#include <QtTest>

#include <cmath>

#include <QJsonObject>
#include <QTemporaryDir>

#include "Clip.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "EditLocks.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "ProjectJournal.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "SmfNoteRead.h"
#include "Song.h"
#include "Track.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = QJsonObject())
{
	return ControlRegistry::instance()->invoke(id, args);
}

void be16(QByteArray& out, int value)
{
	out.append(static_cast<char>((value >> 8) & 0xFF));
	out.append(static_cast<char>(value & 0xFF));
}

void be32(QByteArray& out, quint32 value)
{
	be16(out, static_cast<int>(value >> 16));
	be16(out, static_cast<int>(value & 0xFFFF));
}

QByteArray smf(const QByteArray& trackBody, int division = 96, int format = 0, int tracks = 1)
{
	QByteArray out("MThd", 4);
	be32(out, 6);
	be16(out, format);
	be16(out, tracks);
	be16(out, division);
	out.append("MTrk", 4);
	be32(out, static_cast<quint32>(trackBody.size()));
	out.append(trackBody);
	return out;
}

//! Three notes at division 96 (one quarter = 96 file ticks): C4 at 0 for a quarter, E4 at one quarter for a quarter -
//! written with RUNNING STATUS and a note-on of velocity 0 as its note-off - and G4 at two quarters for a half.
QByteArray chordTrack()
{
	QByteArray body;
	body.append(char(0x00)); body.append(char(0xFF)); body.append(char(0x51)); body.append(char(0x03));  // tempo meta
	body.append(char(0x07)); body.append(char(0xA1)); body.append(char(0x20));
	body.append(char(0x00)); body.append(char(0x90)); body.append(char(60)); body.append(char(100));        // C4 on
	body.append(char(96));   body.append(char(0x80)); body.append(char(60)); body.append(char(0));          // C4 off
	body.append(char(0x00)); body.append(char(0x90)); body.append(char(64)); body.append(char(80));        // E4 on
	body.append(char(96));   body.append(char(64)); body.append(char(0));                                 // running status, vel 0
	body.append(char(0x00)); body.append(char(67)); body.append(char(127));                               // running status, G4 on
	body.append(char(0x81)); body.append(char(0x40)); body.append(char(0x80)); body.append(char(67)); body.append(char(0));  // 192 ticks, off
	body.append(char(0x00)); body.append(char(0xFF)); body.append(char(0x2F)); body.append(char(0x00));
	return smf(body);
}

QByteArray silentTrack() { return smf(QByteArray("\x00\xFF\x2F\x00", 4)); }

bool write(const QString& path, const QByteArray& bytes)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

//! A PCM16 mono WAV of \a seconds of a sine at \a rate.
QByteArray wav(int seconds, int rate = 44100)
{
	const int frames = seconds * rate;
	QByteArray data;
	for (int i = 0; i < frames; ++i)
	{
		const auto sample = static_cast<qint16>(12000.0 * std::sin(2.0 * 3.14159265358979323846 * 440.0 * i / rate));
		data.append(static_cast<char>(sample & 0xFF));
		data.append(static_cast<char>((sample >> 8) & 0xFF));
	}
	QByteArray out("RIFF", 4);
	auto le32 = [&out](quint32 v) { for (int i = 0; i < 4; ++i) { out.append(static_cast<char>((v >> (8 * i)) & 0xFF)); } };
	auto le16 = [&out](quint16 v) { out.append(static_cast<char>(v & 0xFF)); out.append(static_cast<char>(v >> 8)); };
	le32(36 + static_cast<quint32>(data.size()));
	out.append("WAVEfmt ", 8);
	le32(16); le16(1); le16(1); le32(static_cast<quint32>(rate)); le32(static_cast<quint32>(rate * 2)); le16(2); le16(16);
	out.append("data", 4);
	le32(static_cast<quint32>(data.size()));
	out.append(data);
	return out;
}

//! Everything a refused import must leave alone: tracks, each one's clips and notes, the undo and redo stacks.
QString stateOf()
{
	QString state;
	for (Track* track : Engine::getSong()->tracks())
	{
		state += QStringLiteral("%1:%2:").arg(track->id()).arg(track->numOfClips());
		for (int i = 0; i < track->numOfClips(); ++i)
		{
			Clip* clip = track->getClip(i);
			const auto* midi = dynamic_cast<MidiClip*>(clip);
			state += QStringLiteral("[%1+%2/%3]").arg(clip->startPosition().getTicks()).arg(clip->length().getTicks())
				.arg(midi != nullptr ? static_cast<int>(midi->notes().size()) : -1);
		}
		state += QLatin1Char(';');
	}
	return state + QStringLiteral("undo%1/redo%2").arg(Engine::projectJournal()->undoDepth()).arg(Engine::projectJournal()->redoDepth());
}

QString addTrack(const QString& type)
{
	return run(QStringLiteral("track.add"), {{QStringLiteral("type"), type}}).result.value(QStringLiteral("track")).toString();
}

MidiClip* midiClipOf(const QString& clip)
{
	control::ClipRef ref;
	ControlResult error;
	return control::resolveClip(clip, &ref, &error) ? dynamic_cast<MidiClip*>(ref.clip) : nullptr;
}

} // namespace

class ClipImportFileTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
		QVERIFY(m_dir.isValid());
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void init() { Engine::getSong()->clearProject(); }

	void theReaderPlacesNotesInLmmsTicksWithRunningStatusAndZeroVelocityOffs()
	{
		const interchange::SmfNoteRead read = interchange::readSmfNotes(chordTrack());
		QVERIFY2(read.ok, qPrintable(read.error));
		QCOMPARE(read.division, 96);
		QCOMPARE(static_cast<int>(read.notes.size()), 3);
		// division 96 -> 48 ticks per quarter: a file tick is half a LMMS tick.
		QCOMPARE(read.notes[0].key, 60);
		QCOMPARE(read.notes[0].start, 0LL);
		QCOMPARE(read.notes[0].length, 48LL);
		QCOMPARE(read.notes[1].key, 64);
		QCOMPARE(read.notes[1].start, 48LL);
		QCOMPARE(read.notes[1].length, 48LL);
		QCOMPARE(read.notes[1].velocity, 80);
		QCOMPARE(read.notes[2].key, 67);
		QCOMPARE(read.notes[2].start, 96LL);
		QCOMPARE(read.notes[2].length, 96LL);
		QCOMPARE(read.endTick, 192LL);
	}

	void theReaderRefusesWhatIsNotAUsableMidiFile()
	{
		QVERIFY(!interchange::readSmfNotes(QByteArray()).ok);
		QVERIFY(!interchange::readSmfNotes(QByteArray("RIFFxxxxWAVE")).ok);
		QVERIFY(!interchange::readSmfNotes(silentTrack()).ok);                      // no notes
		QVERIFY(!interchange::readSmfNotes(smf(QByteArray(), 0x8000 | 0x2519)).ok);  // SMPTE division
		QVERIFY(!interchange::readSmfNotes(smf(QByteArray("\x00\x90\x3C\x90", 4))).ok);  // data byte with its high bit set
		QVERIFY(!interchange::readSmfNotes(smf(QByteArray("\x00\x3C\x64", 3))).ok);      // data with no status to run on
		QVERIFY(!interchange::readSmfNotes(smf(QByteArray("\x00\xFF\x01\x7F", 4))).ok);  // a meta event longer than its track
		const QByteArray valid = chordTrack();
		for (int cut = 0; cut < valid.size(); ++cut)
		{
			// Every cut of a valid file is answered; only the whole file (and one that loses only its end marker) reads.
			const interchange::SmfNoteRead read = interchange::readSmfNotes(valid.left(cut));
			if (read.ok) { QVERIFY(!read.notes.empty()); }
		}
	}

	void theReaderSurvivesFlippedBytesAndNoise()
	{
		const QByteArray valid = chordTrack();
		quint32 seed = 12345;
		auto next = [&seed]() { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
		for (int round = 0; round < 3000; ++round)
		{
			QByteArray bytes = valid;
			for (int flips = 0; flips < 1 + round % 4; ++flips) { bytes[static_cast<int>(next() % bytes.size())] = static_cast<char>(next() & 0xFF); }
			const interchange::SmfNoteRead read = interchange::readSmfNotes(bytes);
			if (read.ok) { for (const auto& note : read.notes) { QVERIFY(note.key >= 0 && note.key < 128 && note.length >= 1); } }
		}
		for (int round = 0; round < 500; ++round)
		{
			QByteArray noise("MThd\0\0\0\6\0\0\0\1\0\x60MTrk", 18);
			for (int i = 0; i < 40; ++i) { noise.append(static_cast<char>(next() & 0xFF)); }
			(void) interchange::readSmfNotes(noise);
		}
	}

	void theCommandIsRegisteredWithAContractRow()
	{
		const ControlCommand* cmd = ControlRegistry::instance()->command(QStringLiteral("clip.import_file"));
		QVERIFY(cmd != nullptr);
		QVERIFY(cmd->mutating && !cmd->description.isEmpty() && !cmd->argsSchema.isEmpty());
		const control::ReversibilityEntry* row = control::ReversibilityTable::instance().lookup(QStringLiteral("clip.import_file"));
		QVERIFY(row != nullptr);
		QCOMPARE(control::reversibilityClassName(row->cls), QStringLiteral("true_inverse"));
	}

	void aMidiFileBecomesAClipAtTheTickOnAnInstrumentTrackAndOneUndoRemovesIt()
	{
		const QString track = addTrack(QStringLiteral("instrument"));
		const QString file = m_dir.filePath(QStringLiteral("generated.mid"));
		QVERIFY(write(file, chordTrack()));
		const QString before = stateOf();
		const ControlResult made = run(QStringLiteral("clip.import_file"),
			{{QStringLiteral("path"), file}, {QStringLiteral("track"), track}, {QStringLiteral("position"), 384}});
		QVERIFY2(made.ok, qPrintable(made.errorMessage));
		QCOMPARE(made.result.value(QStringLiteral("position")).toInt(), 384);
		QCOMPARE(made.result.value(QStringLiteral("kind")).toString(), QStringLiteral("midi"));
		QCOMPARE(made.result.value(QStringLiteral("note_count")).toInt(), 3);
		QVERIFY(!made.result.value(QStringLiteral("track_created")).toBool());
		MidiClip* clip = midiClipOf(made.result.value(QStringLiteral("clip")).toString());
		QVERIFY(clip != nullptr);
		QCOMPARE(clip->startPosition().getTicks(), 384);
		QCOMPARE(static_cast<int>(clip->notes().size()), 3);
		QCOMPARE(clip->notes()[2]->key(), 67);
		QCOMPARE(clip->notes()[2]->pos().getTicks(), 96);
		QCOMPARE(clip->notes()[2]->length().getTicks(), 96);
		QCOMPARE(clip->notes()[0]->getVolume(), static_cast<volume_t>(std::lround(100 * 200.0 / 127.0)));
		QCOMPARE(clip->name(), QStringLiteral("generated"));

		const QString after = stateOf();
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(stateOf().section(QStringLiteral("undo"), 0, 0), before.section(QStringLiteral("undo"), 0, 0));  // the clip is gone
		QVERIFY(run(QStringLiteral("control.redo")).ok);
		QCOMPARE(stateOf(), after);                                                                                  // and back, notes and all
	}

	void aMidiFileWithNoTrackGetsANewInstrumentTrackAndOneUndoRemovesBoth()
	{
		const QString file = m_dir.filePath(QStringLiteral("loop.mid"));
		QVERIFY(write(file, chordTrack()));
		const int tracks = static_cast<int>(Engine::getSong()->tracks().size());
		const ControlResult made = run(QStringLiteral("clip.import_file"), {{QStringLiteral("path"), file}, {QStringLiteral("position"), 192}});
		QVERIFY2(made.ok, qPrintable(made.errorMessage));
		QVERIFY(made.result.value(QStringLiteral("track_created")).toBool());
		QCOMPARE(static_cast<int>(Engine::getSong()->tracks().size()), tracks + 1);
		Track* created = Engine::getSong()->tracks().back();
		QVERIFY(dynamic_cast<InstrumentTrack*>(created) != nullptr);
		QCOMPARE(created->name(), QStringLiteral("loop"));
		QCOMPARE(created->numOfClips(), 1);
		QCOMPARE(created->getClip(0)->startPosition().getTicks(), 192);
		const QString after = stateOf();
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(static_cast<int>(Engine::getSong()->tracks().size()), tracks);
		QVERIFY(run(QStringLiteral("control.redo")).ok);
		QCOMPARE(static_cast<int>(Engine::getSong()->tracks().size()), tracks + 1);
		QCOMPARE(Engine::getSong()->tracks().back()->numOfClips(), 1);
		QCOMPARE(dynamic_cast<MidiClip*>(Engine::getSong()->tracks().back()->getClip(0))->notes().size(), std::size_t(3));
		Q_UNUSED(after);
	}

	void anAudioFileBecomesASampleClipAtTheTickOnASampleTrack()
	{
		const QString track = addTrack(QStringLiteral("sample"));
		const QString file = m_dir.filePath(QStringLiteral("hit.wav"));
		QVERIFY(write(file, wav(1)));
		const ControlResult made = run(QStringLiteral("clip.import_file"),
			{{QStringLiteral("path"), file}, {QStringLiteral("track"), track}, {QStringLiteral("position"), 576}});
		QVERIFY2(made.ok, qPrintable(made.errorMessage));
		QCOMPARE(made.result.value(QStringLiteral("kind")).toString(), QStringLiteral("audio"));
		control::ClipRef ref;
		ControlResult error;
		QVERIFY(control::resolveClip(made.result.value(QStringLiteral("clip")).toString(), &ref, &error));
		auto* clip = dynamic_cast<SampleClip*>(ref.clip);
		QVERIFY(clip != nullptr);
		QCOMPARE(clip->startPosition().getTicks(), 576);
		QCOMPARE(static_cast<int>(clip->sample().sampleSize()), 44100);
		QVERIFY(clip->length().getTicks() > 0);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(Engine::getSong()->tracks().back()->numOfClips(), 0);
	}

	void anAudioFileDeletedRightAfterTheDropStillPlaysAndSaves()
	{
		const QString file = m_dir.filePath(QStringLiteral("plugin-drag.wav"));  // in the temporary folder, as a plug-in leaves it
		QVERIFY(write(file, wav(1)));
		const ControlResult made = run(QStringLiteral("clip.import_file"), {{QStringLiteral("path"), file}, {QStringLiteral("position"), 0}});
		QVERIFY2(made.ok, qPrintable(made.errorMessage));
		QVERIFY(made.result.value(QStringLiteral("embedded")).toBool());
		QVERIFY(QFile::remove(file));
		control::ClipRef ref;
		ControlResult error;
		QVERIFY(control::resolveClip(made.result.value(QStringLiteral("clip")).toString(), &ref, &error));
		auto* clip = dynamic_cast<SampleClip*>(ref.clip);
		QVERIFY(clip != nullptr);
		QCOMPARE(static_cast<int>(clip->sample().sampleSize()), 44100);
		QVERIFY(clip->sampleFile().isEmpty());  // no path to dangle: the project carries the audio itself
		QDomDocument doc;
		QDomElement root = doc.createElement(QStringLiteral("root"));
		doc.appendChild(root);
		QDomElement element = doc.createElement(QStringLiteral("sampleclip"));
		root.appendChild(element);
		clip->saveSettings(doc, element);
		QVERIFY(!element.attribute(QStringLiteral("data")).isEmpty());
	}

	//! A file outside the temporary folder is referenced by path, as the browser's drag does: no copy of the audio in the clip.
	void anAudioFileOutsideTheTemporaryFolderIsReferencedByPath()
	{
		const QString folder = QCoreApplication::applicationDirPath() + QStringLiteral("/clip-import-file-test");
		QVERIFY(QDir().mkpath(folder));
		const QString file = folder + QStringLiteral("/kept.wav");
		QVERIFY(write(file, wav(1)));
		const ControlResult made = run(QStringLiteral("clip.import_file"), {{QStringLiteral("path"), file}, {QStringLiteral("position"), 0}});
		QVERIFY2(made.ok, qPrintable(made.errorMessage));
		QVERIFY(!made.result.value(QStringLiteral("embedded")).toBool());
		control::ClipRef ref;
		ControlResult error;
		QVERIFY(control::resolveClip(made.result.value(QStringLiteral("clip")).toString(), &ref, &error));
		auto* clip = dynamic_cast<SampleClip*>(ref.clip);
		QVERIFY(clip != nullptr);
		QVERIFY(clip->sampleFile().endsWith(QStringLiteral("kept.wav")));
		QCOMPARE(static_cast<int>(clip->sample().sampleSize()), 44100);
		QDir(folder).removeRecursively();
	}

	void aFileThatCannotBeImportedChangesNothing()
	{
		const QString instrument = addTrack(QStringLiteral("instrument"));
		const QString sample = addTrack(QStringLiteral("sample"));
		QVERIFY(run(QStringLiteral("clip.add"), {{QStringLiteral("track"), instrument}, {QStringLiteral("position"), 0}}).ok);
		const QString good = m_dir.filePath(QStringLiteral("good.mid"));
		QVERIFY(write(good, chordTrack()));
		const QString text = m_dir.filePath(QStringLiteral("notes.txt"));
		QVERIFY(write(text, "not music"));
		const QString emptyFile = m_dir.filePath(QStringLiteral("empty.mid"));
		QVERIFY(write(emptyFile, QByteArray()));
		const QString cut = m_dir.filePath(QStringLiteral("cut.mid"));
		QVERIFY(write(cut, chordTrack().left(chordTrack().size() - 9)));
		const QString noNotes = m_dir.filePath(QStringLiteral("none.mid"));
		QVERIFY(write(noNotes, silentTrack()));
		const QString smpte = m_dir.filePath(QStringLiteral("smpte.mid"));
		QVERIFY(write(smpte, smf(QByteArray("\x00\x90\x3C\x64\x10\x80\x3C\x00", 8), 0x8000 | 0x2519)));
		const QString garbageWav = m_dir.filePath(QStringLiteral("garbage.wav"));
		QVERIFY(write(garbageWav, "RIFF....WAVEjunk junk junk junk"));
		const QString audio = m_dir.filePath(QStringLiteral("ok.wav"));
		QVERIFY(write(audio, wav(1)));
		const QString folder = m_dir.filePath(QStringLiteral("folder"));
		QVERIFY(QDir().mkpath(folder));

		struct Case { QString name; QJsonObject args; };
		const QVector<Case> cases = {
			{QStringLiteral("missing"), {{QStringLiteral("path"), m_dir.filePath(QStringLiteral("nope.mid"))}}},
			{QStringLiteral("relative"), {{QStringLiteral("path"), QStringLiteral("good.mid")}}},
			{QStringLiteral("folder"), {{QStringLiteral("path"), folder}}},
			{QStringLiteral("text"), {{QStringLiteral("path"), text}}},
			{QStringLiteral("empty file"), {{QStringLiteral("path"), emptyFile}}},
			{QStringLiteral("truncated midi"), {{QStringLiteral("path"), cut}}},
			{QStringLiteral("midi without notes"), {{QStringLiteral("path"), noNotes}}},
			{QStringLiteral("smpte midi"), {{QStringLiteral("path"), smpte}}},
			{QStringLiteral("damaged audio"), {{QStringLiteral("path"), garbageWav}}},
			{QStringLiteral("midi on a sample track"), {{QStringLiteral("path"), good}, {QStringLiteral("track"), sample}}},
			{QStringLiteral("audio on an instrument track"), {{QStringLiteral("path"), audio}, {QStringLiteral("track"), instrument}}},
			{QStringLiteral("unknown track"), {{QStringLiteral("path"), good}, {QStringLiteral("track"), QStringLiteral("trk-9999")}}},
			{QStringLiteral("past the end of the song"), {{QStringLiteral("path"), good}, {QStringLiteral("position"), 9999 * 192}}},
		};
		for (const Case& c : cases)
		{
			const QString before = stateOf();
			const ControlResult refused = run(QStringLiteral("clip.import_file"), c.args);
			QVERIFY2(!refused.ok, qPrintable(c.name + " was imported"));
			QVERIFY2(!refused.errorMessage.isEmpty(), qPrintable(c.name));
			QCOMPARE(stateOf(), before);
		}
		// A locked track refuses too, and stays as it was.
		EditLocks::instance().setTrackLocked(control::idToIndex(instrument, QStringLiteral("trk-")), true);
		const QString locked = stateOf();
		QVERIFY(!run(QStringLiteral("clip.import_file"), {{QStringLiteral("path"), good}, {QStringLiteral("track"), instrument}}).ok);
		QCOMPARE(stateOf(), locked);
		EditLocks::instance().setTrackLocked(control::idToIndex(instrument, QStringLiteral("trk-")), false);
	}

private:
	QTemporaryDir m_dir;
};

QTEST_MAIN(ClipImportFileTest)
#include "ClipImportFileTest.moc"
