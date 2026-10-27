/*
 * LibraryBrowserTest.cpp - clips into the library, the library index, similar sounds, thumbnails, preview, history
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
/*! 0.5.0 L11 (W3): a MIDI clip saved into the library is a Standard MIDI File and one undo deletes it
 *  (WF-022); a user preset is renamed and removed, each one undo step, and one that is not there is
 *  refused (WF-025); a folder scanned into the index answers an
 *  indexed search without a walk (WF-012) and ranks the closest tone first (WF-020); a thumbnail is a
 *  picture of the requested size (WF-018); a matched preview plays and stops (WF-009); the browser's
 *  history steps back and refuses past its start (WF-014); a remembered source reads back (WF-023); a search
 *  refined as it is typed replaces its history entry, and a result loads onto a new track that one undo takes
 *  away, a project or MIDI file being refused (theme 3, 0.5.0 UX). */

#include <QtTest>

#include <limits>

#ifndef _WIN32
#include <unistd.h>
#endif

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "BrowserIndex.h"
#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "Note.h"
#include "SampleTrack.h"
#include "Song.h"
#include "TakeLaneTestSupport.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

QByteArray readWhole(const QString& path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

} // namespace

class LibraryBrowserTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_dir.isValid());
		qputenv("ZENE_BROWSER_INDEX", QDir(m_dir.path()).filePath(QStringLiteral("index.json")).toUtf8());
		Engine::init(true);
		// A private working directory: the user presets this test writes and removes are never the developer's own.
		ConfigManager::inst()->setWorkingDir(QDir(m_dir.path()).filePath(QStringLiteral("work")));
		ControlRegistry::setReady(true);
		BrowserIndex::instance().reload();
		QDir(m_dir.path()).mkpath(QStringLiteral("tones"));
		for (const double hz : {440.0, 470.0, 5000.0})
		{
			QVERIFY(!taketest::writeToneWav(tones(QStringLiteral("tone-%1.wav").arg(static_cast<int>(hz))), hz).isEmpty());
		}
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void aMidiClipIsSavedIntoTheLibrary()
	{
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		auto* clip = dynamic_cast<MidiClip*>(track->createClip(TimePos(0)));
		clip->addNote(Note(TimePos(48), TimePos(0), 60), false);
		clip->addNote(Note(TimePos(48), TimePos(48), 64), false);
		const QJsonObject args{{QStringLiteral("clip"), control::clipIdOf(clip)}, {QStringLiteral("dir"), m_dir.path()},
			{QStringLiteral("name"), QStringLiteral("riff")}};
		const ControlResult saved = run(QStringLiteral("browser.save_clip"), args);
		QVERIFY2(saved.ok, qPrintable(saved.errorMessage));
		const QString path = saved.result.value(QStringLiteral("path")).toString();
		QCOMPARE(QFileInfo(path).fileName(), QStringLiteral("riff.mid"));
		QFile file(path);
		QVERIFY(file.open(QIODevice::ReadOnly));
		const QByteArray bytes = file.readAll();
		QVERIFY(bytes.startsWith("MThd"));
		QVERIFY(bytes.contains("MTrk"));
		QCOMPARE(saved.result.value(QStringLiteral("kind")).toString(), QStringLiteral("midi"));
		QCOMPARE(run(QStringLiteral("browser.save_clip"), args).errorKind, ControlErrorKind::Refused);  // it exists
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(!QFileInfo::exists(path));
		QJsonObject relative = args;
		relative.insert(QStringLiteral("dir"), QStringLiteral("not/absolute"));
		QCOMPARE(run(QStringLiteral("browser.save_clip"), relative).errorKind, ControlErrorKind::InvalidArgs);
	}

	void nonStringClipTagsAreRejectedBeforeSaving()
	{
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		auto* clip = dynamic_cast<MidiClip*>(track->createClip(TimePos(0)));
		const QString path = QDir(m_dir.path()).filePath(QStringLiteral("invalid-tag.mid"));
		const ControlResult rejected = run(QStringLiteral("browser.save_clip"), {{QStringLiteral("clip"), control::clipIdOf(clip)},
			{QStringLiteral("dir"), m_dir.path()}, {QStringLiteral("name"), QStringLiteral("invalid-tag")},
			{QStringLiteral("tags"), QJsonArray{123}}});
		QCOMPARE(rejected.errorKind, ControlErrorKind::InvalidArgs);
		QVERIFY(!QFileInfo::exists(path));
	}

	//! A save over a file that exists but cannot be read is refused before anything is written: undo could not put
	//! back what the overwrite destroyed, so the file stays exactly as it was.
	void aSaveNeverReplacesAFileItCannotRead()
	{
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		auto* clip = dynamic_cast<MidiClip*>(track->createClip(TimePos(0)));
		clip->addNote(Note(TimePos(48), TimePos(0), 60), false);
		const QJsonObject args{{QStringLiteral("clip"), control::clipIdOf(clip)}, {QStringLiteral("dir"), m_dir.path()},
			{QStringLiteral("name"), QStringLiteral("unreadable")}, {QStringLiteral("overwrite"), true}};
		const QString path = QDir(m_dir.path()).filePath(QStringLiteral("unreadable.mid"));
		QFile earlier(path);
		QVERIFY(earlier.open(QIODevice::WriteOnly));
		earlier.write("earlier bytes");
		earlier.close();
		QVERIFY(QFile::setPermissions(path, QFileDevice::WriteOwner));  // write-only: a save can replace it, nothing can read it
		QFile probe(path);
		if (probe.open(QIODevice::ReadOnly))  // root reads anything: nothing to prove here
		{
			probe.close();
			QVERIFY(QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner));
			QSKIP("this account reads files without read permission");
		}

		const ControlResult refused = run(QStringLiteral("browser.save_clip"), args);
		QVERIFY(QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner));
		QCOMPARE(refused.errorKind, ControlErrorKind::Refused);
		QFile back(path);
		QVERIFY(back.open(QIODevice::ReadOnly));
		QCOMPARE(back.readAll(), QByteArray("earlier bytes"));
	}

	//! A dangling link at the clip's file is refused like an unreadable file, not replaced: the link stays.
	void aSaveNeverReplacesADanglingLink()
	{
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		auto* clip = dynamic_cast<MidiClip*>(track->createClip(TimePos(0)));
		clip->addNote(Note(TimePos(48), TimePos(0), 60), false);
		const QString path = QDir(m_dir.path()).filePath(QStringLiteral("dangling.mid"));
		const QString missing = m_dir.filePath(QStringLiteral("nowhere.target"));
		QVERIFY(QFile::link(missing, path));
		QVERIFY(QFileInfo(path).isSymLink() && !QFileInfo::exists(path));

		const ControlResult refused = run(QStringLiteral("browser.save_clip"), {{QStringLiteral("clip"), control::clipIdOf(clip)},
			{QStringLiteral("dir"), m_dir.path()}, {QStringLiteral("name"), QStringLiteral("dangling")}});
		QCOMPARE(refused.errorKind, ControlErrorKind::Refused);
		QVERIFY2(QFileInfo(path).isSymLink(), "the save replaced a dangling link it did not write");
		QCOMPARE(QFile::symLinkTarget(path), missing);
		QVERIFY(!QFileInfo::exists(missing));
	}

	// After aMidiClipIsSavedIntoTheLibrary, which ends with its file undone away.
	void anOverwrittenClipComesBackOnUndo()
	{
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		auto* clip = dynamic_cast<MidiClip*>(track->createClip(TimePos(0)));
		clip->addNote(Note(TimePos(48), TimePos(0), 60), false);
		clip->addNote(Note(TimePos(96), TimePos(48), 67), false);
		const QJsonObject args{{QStringLiteral("clip"), control::clipIdOf(clip)}, {QStringLiteral("dir"), m_dir.path()},
			{QStringLiteral("name"), QStringLiteral("replaced")}};
		QVERIFY(run(QStringLiteral("browser.save_clip"), args).ok);
		const QString path = QDir(m_dir.path()).filePath(QStringLiteral("replaced.mid"));
		QVERIFY(QFileInfo::exists(path));
		// Something else of our own stands there: an overwrite replaced it.
		QFile other(path);
		QVERIFY(other.open(QIODevice::WriteOnly));
		other.write("earlier bytes");
		other.close();
		QJsonObject over = args;
		over.insert(QStringLiteral("overwrite"), true);
		QVERIFY(run(QStringLiteral("browser.save_clip"), over).ok);
		QVERIFY(QFileInfo(path).size() > 14);
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // the file the overwrite replaced is back, not deleted
		QVERIFY(QFileInfo::exists(path));
		QFile back(path);
		QVERIFY(back.open(QIODevice::ReadOnly));
		QCOMPARE(back.readAll(), QByteArray("earlier bytes"));
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // and the save under it still deletes the file
		QVERIFY(!QFileInfo::exists(path));
	}

	//! A note whose note-off would pass the largest tick is refused, not written: the end cannot be held, and a clamped
	//! end would sort onto or before its own note-on. A MIDI delta time is at most four bytes (0x0FFFFFFF ticks), so a
	//! note further than that from the event before it is refused too. A note inside both limits is written exactly.
	void aNoteNearTheTopOfTheRangeIsRefusedOrWrittenExactly()
	{
		const tick_t top = std::numeric_limits<tick_t>::max();
		const auto saveNote = [this](tick_t start, tick_t length, const QString& name) {
			auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
			auto* clip = dynamic_cast<MidiClip*>(track->createClip(TimePos(0)));
			clip->addNote(Note(TimePos(length), TimePos(start), 60), false);  // Note(length, pos, key)
			return run(QStringLiteral("browser.save_clip"), {{QStringLiteral("clip"), control::clipIdOf(clip)},
				{QStringLiteral("dir"), m_dir.path()}, {QStringLiteral("name"), name}});
		};
		const QString refusedPath = QDir(m_dir.path()).filePath(QStringLiteral("overhigh.mid"));
		const ControlResult overhigh = saveNote(top - 10, 48, QStringLiteral("overhigh"));
		QCOMPARE(overhigh.errorKind, ControlErrorKind::Refused);
		QVERIFY2(!QFileInfo::exists(refusedPath), "a refused save wrote its file");
		QCOMPARE(saveNote(top, 1, QStringLiteral("exact")).errorKind, ControlErrorKind::Refused);  // start == largest tick
		QVERIFY(!QFileInfo::exists(QDir(m_dir.path()).filePath(QStringLiteral("exact.mid"))));
		// The first delta is the note's start, from the tempo meta at 0: 2^28 is one past a four-byte delta.
		const tick_t fiveBytes = tick_t{1} << 28;
		const ControlResult fiveByte = saveNote(fiveBytes, 48, QStringLiteral("fivebyte"));
		QCOMPARE(fiveByte.errorKind, ControlErrorKind::Refused);
		QVERIFY2(fiveByte.errorMessage.contains(QStringLiteral("tick 268435456")), qPrintable(fiveByte.errorMessage));
		QVERIFY(!QFileInfo::exists(QDir(m_dir.path()).filePath(QStringLiteral("fivebyte.mid"))));

		// Exactly the largest four-byte delta: the note's start is 0x0FFFFFFF, its first delta from tick 0.
		const tick_t maxDelta = (tick_t{1} << 28) - 1;  // 0x0FFFFFFF
		const tick_t start = maxDelta;
		const tick_t length = 1;
		QVERIFY(saveNote(start, length, QStringLiteral("high")).ok);
		QFile file(QDir(m_dir.path()).filePath(QStringLiteral("high.mid")));
		QVERIFY(file.open(QIODevice::ReadOnly));
		const QByteArray bytes = file.readAll();
		QVERIFY(bytes.startsWith("MThd") && bytes.contains("MTrk"));
		// The delta-timed track: after the header and the tempo meta event.
		int at = bytes.indexOf("MTrk") + 4 + 4;
		// A valid SMF delta is at most four bytes; a fifth continuation byte fails the read.
		const auto readDelta = [&bytes, &at](quint32* out) {
			quint32 value = 0;
			for (int n = 0; n < 4; ++n)
			{
				const quint8 byte = static_cast<quint8>(bytes.at(at++));
				value = (value << 7) | (byte & 0x7F);
				if ((byte & 0x80) == 0)
				{
					*out = value;
					return true;
				}
			}
			return false;
		};
		quint32 delta = 0;
		QVERIFY(readDelta(&delta));
		QCOMPARE(delta, quint32{0});  // the tempo meta's own delta
		at += 6;  // FF 51 03 and the three tempo bytes
		QVERIFY(readDelta(&delta));
		QCOMPARE(delta, quint32{static_cast<quint32>(start)});  // the note-on, at its own start
		QCOMPARE(static_cast<quint8>(bytes.at(at++)), quint8{0x90});
		at += 2;
		QVERIFY(readDelta(&delta));
		QCOMPARE(delta, quint32{static_cast<quint32>(length)});  // the note-off, 'length' after its note-on
		QCOMPARE(static_cast<quint8>(bytes.at(at++)), quint8{0x80});
		QVERIFY(run(QStringLiteral("control.undo")).ok);
	}

	void aSymlinkDestinationIsRefused()
	{
		const QString target = QDir(m_dir.path()).filePath(QStringLiteral("original.mid"));
		QFile original(target);
		QVERIFY(original.open(QIODevice::WriteOnly));
		QCOMPARE(original.write("original midi"), qint64{13});
		original.close();
		const QString link = QDir(m_dir.path()).filePath(QStringLiteral("linked.mid"));
		QVERIFY(QFile::link(target, link));

		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		auto* clip = dynamic_cast<MidiClip*>(track->createClip(TimePos(0)));
		clip->addNote(Note(TimePos(48), TimePos(0), 60), false);
		const ControlResult saved = run(QStringLiteral("browser.save_clip"), {{QStringLiteral("clip"), control::clipIdOf(clip)},
			{QStringLiteral("dir"), m_dir.path()}, {QStringLiteral("name"), QStringLiteral("linked")},
			{QStringLiteral("overwrite"), true}});
		if (saved.ok) { QVERIFY(run(QStringLiteral("control.undo")).ok); }

		QVERIFY2(!saved.ok, "saving over a symlink destination must be refused");
		QVERIFY(QFileInfo(link).isSymLink());
		QCOMPARE(QFileInfo(link).symLinkTarget(), target);
		QVERIFY(original.open(QIODevice::ReadOnly));
		QCOMPARE(original.readAll(), QByteArray("original midi"));
	}

	void aMissingPresetIsRefused()
	{
		auto* track = Track::create(Track::Type::Instrument, Engine::getSong());
		QVERIFY(!run(QStringLiteral("plugin.preset_remove"), {{QStringLiteral("target"), control::trackIdOf(track)},
			{QStringLiteral("plugin"), QStringLiteral("fx-999999")}, {QStringLiteral("name"), QStringLiteral("nothing")}}).ok);
		QCOMPARE(run(QStringLiteral("plugin.preset_rename"), {{QStringLiteral("target"), control::trackIdOf(track)},
			{QStringLiteral("plugin"), QStringLiteral("fx-999999")}, {QStringLiteral("name"), QStringLiteral("a")}}).errorKind,
			ControlErrorKind::InvalidArgs);  // no new_name
	}

	void aUserPresetIsRenamedAndRemoved()
	{
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		track->loadInstrument(QStringLiteral("tripleoscillator"));  // a placeholder instrument where the module is absent: still a device
		const QString target = control::trackIdOf(track);
		const QJsonObject device{{QStringLiteral("target"), target}, {QStringLiteral("plugin"), QStringLiteral("inst")}};
		const QString dir = run(QStringLiteral("plugin.preset_list"), device).result.value(QStringLiteral("dir")).toString();
		QVERIFY2(dir.startsWith(m_dir.path()), qPrintable(dir));
		QVERIFY(QDir().mkpath(dir));
		QFile file(dir + QStringLiteral("lead.xpf"));
		QVERIFY(file.open(QIODevice::WriteOnly));
		QCOMPARE(file.write("<preset/>"), qint64{9});
		file.close();
		const ControlResult renamed = run(QStringLiteral("plugin.preset_rename"), {{QStringLiteral("target"), target},
			{QStringLiteral("plugin"), QStringLiteral("inst")}, {QStringLiteral("name"), QStringLiteral("lead")},
			{QStringLiteral("new_name"), QStringLiteral("solo")}});
		QVERIFY2(renamed.ok, qPrintable(renamed.errorMessage));
		QCOMPARE(renamed.result.value(QStringLiteral("path")).toString(), dir + QStringLiteral("solo.xpf"));
		QCOMPARE(renamed.result.value(QStringLiteral("name")).toString(), QStringLiteral("solo.xpf"));
		QVERIFY(QFileInfo::exists(dir + QStringLiteral("solo.xpf")) && !QFileInfo::exists(dir + QStringLiteral("lead.xpf")));
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // the old name back
		QVERIFY(QFileInfo::exists(dir + QStringLiteral("lead.xpf")) && !QFileInfo::exists(dir + QStringLiteral("solo.xpf")));
		const ControlResult removed = run(QStringLiteral("plugin.preset_remove"), {{QStringLiteral("target"), target},
			{QStringLiteral("plugin"), QStringLiteral("inst")}, {QStringLiteral("name"), QStringLiteral("lead")}});
		QVERIFY2(removed.ok, qPrintable(removed.errorMessage));
		QCOMPARE(removed.result.value(QStringLiteral("removed")).toString(), dir + QStringLiteral("lead.xpf"));
		QCOMPARE(removed.result.value(QStringLiteral("bytes")).toInt(), 9);
		QVERIFY(!QFileInfo::exists(dir + QStringLiteral("lead.xpf")));
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // the file written back with its bytes
		QFile back(dir + QStringLiteral("lead.xpf"));
		QVERIFY(back.open(QIODevice::ReadOnly));
		QCOMPARE(back.readAll(), QByteArray("<preset/>"));
	}

	void theIndexAnswersWithoutAWalk()
	{
		const ControlResult scanned = run(QStringLiteral("browser.reindex"), {{QStringLiteral("path"), tones(QString())},
			{QStringLiteral("wait"), true}});
		QVERIFY2(scanned.ok, qPrintable(scanned.errorMessage));
		QCOMPARE(scanned.result.value(QStringLiteral("entries")).toInt(), 3);
		QCOMPARE(scanned.result.value(QStringLiteral("analysed")).toInt(), 3);
		const ControlResult found = run(QStringLiteral("browser.index_query"), {{QStringLiteral("text"), QStringLiteral("tone-4")}});
		QCOMPARE(found.result.value(QStringLiteral("matched")).toInt(), 2);  // tone-440 and tone-470
		BrowserIndex::instance().reload();  // read back from the store file
		QCOMPARE(BrowserIndex::instance().entries().size(), std::size_t{3});
		QVERIFY(BrowserIndex::instance().entries().front().features.valid);
		QCOMPARE(run(QStringLiteral("browser.reindex"), {{QStringLiteral("path"), QStringLiteral("/no/such/folder")}}).errorKind,
			ControlErrorKind::NotFound);
	}

	void theClosestToneComesFirst()
	{
		const ControlResult similar = run(QStringLiteral("browser.similar"), {{QStringLiteral("path"), tones(QStringLiteral("tone-440.wav"))}});
		QVERIFY2(similar.ok, qPrintable(similar.errorMessage));
		const QJsonArray items = similar.result.value(QStringLiteral("items")).toArray();
		QCOMPARE(items.size(), 2);  // not the file itself
		QCOMPARE(QFileInfo(items.at(0).toObject().value(QStringLiteral("path")).toString()).fileName(), QStringLiteral("tone-470.wav"));
		QVERIFY(items.at(0).toObject().value(QStringLiteral("distance")).toDouble() < items.at(1).toObject().value(QStringLiteral("distance")).toDouble());
		const double centroid = similar.result.value(QStringLiteral("features")).toObject().value(QStringLiteral("centroid_hz")).toDouble();
		QVERIFY2(centroid > 300.0 && centroid < 700.0, qPrintable(QString::number(centroid)));
	}

	//! Two records that were never measured are not two identical sounds. A
	//! default-constructed record compared with another would otherwise score a
	//! perfect match and rank first in a similar-sound search, telling the user
	//! an undecodable file is the closest thing in the library to anything.
	void anUnmeasuredSoundIsNotAPerfectMatch()
	{
		const BrowserFeatures blank;
		QVERIFY(!blank.valid);
		QVERIFY2(BrowserFeatures::distance(blank, blank) > 0.0,
			"two unmeasured records compared as identical");
		// An unmeasured record must not look closer than a real measurement, or
		// the refusal below is the only thing keeping it out of the results.
		BrowserFeatures measured = BrowserFeatures::analyse(tones(QStringLiteral("tone-440.wav")));
		QVERIFY2(measured.valid, "the fixture tone could not be measured");
		QVERIFY(BrowserFeatures::distance(blank, measured) > BrowserFeatures::distance(measured, measured));
	}

	void aThumbnailIsAPicture()
	{
		const QString out = QDir(m_dir.path()).filePath(QStringLiteral("thumb.png"));
		const ControlResult drawn = run(QStringLiteral("browser.thumbnail"), {{QStringLiteral("path"), tones(QStringLiteral("tone-440.wav"))},
			{QStringLiteral("width"), 120}, {QStringLiteral("height"), 40}, {QStringLiteral("out"), out}});
		QVERIFY2(drawn.ok, qPrintable(drawn.errorMessage));
		const QImage image(out);
		QCOMPARE(image.width(), 120);
		QCOMPARE(image.height(), 40);
	}

	void aThumbnailNeverWritesOverItsSource()
	{
		// The refusals run on a private copy, so a run that fails cannot damage the shared tone fixtures.
		QVERIFY(QDir(m_dir.path()).mkpath(QStringLiteral("thumb-source")));
		const QDir dir(QDir(m_dir.path()).filePath(QStringLiteral("thumb-source")));
		const QString source = dir.filePath(QStringLiteral("own.wav"));
		QFile::remove(source);
		QVERIFY(QFile::copy(tones(QStringLiteral("tone-440.wav")), source));
		const QByteArray original = readWhole(source);
		// Refused, with the source's bytes unchanged, whatever name the output is given.
		const auto refusedOver = [&](const QString& out) {
			const ControlResult refused = run(QStringLiteral("browser.thumbnail"), {{QStringLiteral("path"), source},
				{QStringLiteral("width"), 120}, {QStringLiteral("height"), 40}, {QStringLiteral("out"), out}});
			return !refused.ok && refused.errorKind == ControlErrorKind::InvalidArgs && readWhole(source) == original;
		};
		QVERIFY(!original.isEmpty());
		QVERIFY2(refusedOver(source), "the source itself");
		QVERIFY2(refusedOver(dir.absolutePath() + QStringLiteral("/../thumb-source/own.wav")), "the source through a '..' segment");
		// Only the source is refused: another name beside it still gets its picture.
		const ControlResult drawn = run(QStringLiteral("browser.thumbnail"), {{QStringLiteral("path"), source},
			{QStringLiteral("out"), dir.filePath(QStringLiteral("own.png"))}});
		QVERIFY2(drawn.ok, qPrintable(drawn.errorMessage));
		QVERIFY(!QImage(dir.filePath(QStringLiteral("own.png"))).isNull());
		// On POSIX, a symbolic link and a hard link name the same file, so both must be refused.
		// QFile::link creates a Windows .lnk shortcut rather than a filesystem symbolic link.
	#ifndef _WIN32
		const QString alias = dir.filePath(QStringLiteral("alias.wav"));
		QFile::remove(alias);
		QVERIFY2(QFile(source).link(alias), "could not create a symbolic link for the test");
		QVERIFY2(refusedOver(alias), "a symbolic link to the source");
		const QString hard = dir.filePath(QStringLiteral("hard.wav"));
		QFile::remove(hard);
		QVERIFY2(::link(QFile::encodeName(source).constData(), QFile::encodeName(hard).constData()) == 0,
			"could not create a hard link for the test");
		QVERIFY2(refusedOver(hard), "a hard link to the source");  // a second name for the same inode
#endif
	}

	void aMatchedPreviewPlaysAndStops()
	{
		const ControlResult played = run(QStringLiteral("browser.preview"), {{QStringLiteral("path"), tones(QStringLiteral("tone-440.wav"))},
			{QStringLiteral("match"), QStringLiteral("key")}, {QStringLiteral("key"), 0}});
		QVERIFY2(played.ok, qPrintable(played.errorMessage));
		QVERIFY(played.result.value(QStringLiteral("seconds")).toDouble() > 0.0);
		QVERIFY(played.result.value(QStringLiteral("playing")).toBool());  // a handle reached the engine (review W3-agent-library #5)
		const int semitones = played.result.value(QStringLiteral("semitones")).toInt();
		QVERIFY(semitones >= -6 && semitones <= 5);
		QVERIFY(run(QStringLiteral("browser.preview_stop")).ok);
		QCOMPARE(run(QStringLiteral("browser.preview"), {{QStringLiteral("path"), QStringLiteral("relative.wav")}}).errorKind,
			ControlErrorKind::InvalidArgs);
	}

	void theHistoryStepsBack()
	{
		const auto browse = [](const QString& text) {
			return run(QStringLiteral("browser.browse"), {{QStringLiteral("command"), QStringLiteral("browser.index_query")},
				{QStringLiteral("args"), QJsonObject{{QStringLiteral("text"), text}}}, {QStringLiteral("label"), text}});
		};
		QVERIFY(browse(QStringLiteral("tone-5")).ok);
		QVERIFY(browse(QStringLiteral("tone")).ok);
		const ControlResult back = run(QStringLiteral("browser.history_go"), {{QStringLiteral("step"), -1}});
		QVERIFY2(back.ok, qPrintable(back.errorMessage));
		QCOMPARE(back.result.value(QStringLiteral("entry")).toObject().value(QStringLiteral("label")).toString(), QStringLiteral("tone-5"));
		QCOMPARE(back.result.value(QStringLiteral("matched")).toInt(), 1);
		QCOMPARE(run(QStringLiteral("browser.history_go"), {{QStringLiteral("step"), -1}}).errorKind, ControlErrorKind::Refused);
	}

	// After theHistoryStepsBack, which counts from an empty history.
	void aTypedSearchRefinesOneHistoryEntry()
	{
		const auto browse = [](const QString& text, bool replace) {
			return run(QStringLiteral("browser.browse"), {{QStringLiteral("command"), QStringLiteral("browser.index_query")},
				{QStringLiteral("args"), QJsonObject{{QStringLiteral("text"), text}}}, {QStringLiteral("label"), text},
				{QStringLiteral("replace"), replace}});
		};
		QVERIFY(browse(QStringLiteral("tone-4"), false).ok);
		const int size = run(QStringLiteral("browser.history")).result.value(QStringLiteral("entries")).toArray().size();
		const ControlResult refined = browse(QStringLiteral("tone-44"), true);
		QVERIFY2(refined.ok, qPrintable(refined.errorMessage));
		const QJsonObject history = run(QStringLiteral("browser.history")).result;
		const QJsonArray entries = history.value(QStringLiteral("entries")).toArray();
		QCOMPARE(entries.size(), size);  // in place of the entry it refines, not beside it
		QCOMPARE(entries.last().toObject().value(QStringLiteral("label")).toString(), QStringLiteral("tone-44"));
		QCOMPARE(history.value(QStringLiteral("position")).toInt(), size - 1);
		QVERIFY(browse(QStringLiteral("tone-5"), false).ok);  // without 'replace' a search is a new entry
		QCOMPARE(run(QStringLiteral("browser.history")).result.value(QStringLiteral("entries")).toArray().size(), size + 1);
	}

	void aResultLoadsOntoANewTrack()
	{
		Song* song = Engine::getSong();
		const std::size_t tracks = song->tracks().size();
		const ControlResult loaded = run(QStringLiteral("browser.load"), {{QStringLiteral("path"), tones(QStringLiteral("tone-440.wav"))},
			{QStringLiteral("as"), QStringLiteral("sample_track")}});
		QVERIFY2(loaded.ok, qPrintable(loaded.errorMessage));
		QCOMPARE(song->tracks().size(), tracks + 1);
		auto* track = dynamic_cast<SampleTrack*>(song->tracks().back());
		QVERIFY(track != nullptr);
		QCOMPARE(track->getClips().size(), std::size_t{1});
		QCOMPARE(loaded.result.value(QStringLiteral("track")).toString(), control::trackIdOf(track));
		QCOMPARE(loaded.result.value(QStringLiteral("kind")).toString(), QStringLiteral("audio"));
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // one undo step takes the track away
		QCOMPARE(song->tracks().size(), tracks);
	}

	void aProjectIsNotLoadedAsATrack()
	{
		const QString project = QDir(m_dir.path()).filePath(QStringLiteral("song.mmp"));
		const QString preset = QDir(m_dir.path()).filePath(QStringLiteral("lead.xpf"));
		for (const QString& path : {project, preset})
		{
			QFile file(path);
			QVERIFY(file.open(QIODevice::WriteOnly));
			file.write("<?xml version=\"1.0\"?>\n");
		}
		const std::size_t tracks = Engine::getSong()->tracks().size();
		QCOMPARE(run(QStringLiteral("browser.load"), {{QStringLiteral("path"), project}}).errorKind, ControlErrorKind::Refused);  // project.open's
		QCOMPARE(run(QStringLiteral("browser.load"), {{QStringLiteral("path"), preset}, {QStringLiteral("as"), QStringLiteral("sample_track")}}).errorKind,
			ControlErrorKind::InvalidArgs);  // only audio goes on a sample track
		QCOMPARE(run(QStringLiteral("browser.load"), {{QStringLiteral("path"), QStringLiteral("relative.wav")}}).errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(Engine::getSong()->tracks().size(), tracks);  // a refusal builds nothing
	}

	void aSourceIsRememberedPerView()
	{
		QVERIFY(run(QStringLiteral("browser.remember_source"), {{QStringLiteral("context"), QStringLiteral("mix")},
			{QStringLiteral("source"), QStringLiteral("Samples")}}).ok);
		QCOMPARE(run(QStringLiteral("browser.sources")).result.value(QStringLiteral("sources")).toObject().value(QStringLiteral("mix")).toString(),
			QStringLiteral("Samples"));
		QCOMPARE(run(QStringLiteral("browser.remember_source"), {{QStringLiteral("context"), QStringLiteral("kitchen")},
			{QStringLiteral("source"), QStringLiteral("Samples")}}).errorKind, ControlErrorKind::InvalidArgs);
	}

	void partialFeatureRecordsAreNotRetainedAsValid()
	{
		const QFileInfo audio(tones(QStringLiteral("tone-440.wav")));
		const QJsonObject partialFeatures{{QStringLiteral("db"), -20.0}};
		const QJsonObject entry{{QStringLiteral("p"), audio.canonicalFilePath()}, {QStringLiteral("k"), QStringLiteral("audio")},
			{QStringLiteral("s"), static_cast<double>(audio.size())},
			{QStringLiteral("m"), static_cast<double>(audio.lastModified().toMSecsSinceEpoch())},
			{QStringLiteral("f"), partialFeatures}};
		const QByteArray bytes = QJsonDocument(QJsonObject{{QStringLiteral("entries"), QJsonArray{entry}}})
			.toJson(QJsonDocument::Compact);
		QFile store(BrowserIndex::instance().storePath());
		QVERIFY(store.open(QIODevice::WriteOnly | QIODevice::Truncate));
		QCOMPARE(store.write(bytes), static_cast<qint64>(bytes.size()));
		store.close();

		BrowserIndex::instance().reload();
		const std::vector<BrowserIndexEntry> loaded = BrowserIndex::instance().entries();
		QCOMPARE(loaded.size(), std::size_t{1});
		const bool validOnLoad = loaded.front().features.valid;
		QVERIFY(BrowserIndex::instance().scanNow({tones(QString())}, false, 0));
		bool retainedAsValid = false;
		bool foundAudio = false;
		for (const BrowserIndexEntry& scanned : BrowserIndex::instance().entries())
		{
			if (scanned.path != audio.canonicalFilePath()) { continue; }
			foundAudio = true;
			retainedAsValid = scanned.features.valid;
			break;
		}
		QVERIFY(foundAudio);
		QVERIFY2(!validOnLoad && !retainedAsValid,
			qPrintable(QStringLiteral("partial features: valid on load=%1, retained after unchanged scan=%2")
				.arg(validOnLoad).arg(retainedAsValid)));
	}

private:
	QString tones(const QString& name) const { return QDir(m_dir.path()).filePath(QStringLiteral("tones/") + name); }

	QTemporaryDir m_dir;
};

QTEST_MAIN(LibraryBrowserTest)
#include "LibraryBrowserTest.moc"
