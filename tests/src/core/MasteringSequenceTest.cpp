/*
 * MasteringSequenceTest.cpp - a mastering document's release sequence: songs, order, processing, gaps and fades
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
/*! 0.5.0 L12 (Q050-317.1, stage 1 of 3): the mastering release sequence.
 *
 *  The acceptance this file proves: a mastering document can be created through
 *  project.new_document, two audio-file songs added and their order, processing, gap and
 *  fades edited through the registered mastering.song_* commands; and a save + reopen
 *  leaves every source reference, the order, the processing, the gap and both fades
 *  exactly as they were.
 *
 *  It also holds the invariants the commands must not break: one undo step per edit (and
 *  its redo), a refused edit changes nothing and records no step, an absent section loads
 *  empty (reset-on-absence), and a hand-edited file cannot put an id in twice or a fade
 *  outside the page's bounds. */

#include <QtTest>

#include <QDomDocument>
#include <QDomElement>
#include <QDomNodeList>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>

#include "ControlRegistry.h"
#include "Engine.h"
#include "MasteringJob.h"
#include "MasteringSequence.h"
#include "ProjectInfo.h"
#include "ProjectJournal.h"
#include "Song.h"

#include "../core/TakeLaneTestSupport.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

QJsonArray songs()
{
	// QVERIFY2() is void-returning, so a helper that answers a value checks by hand.
	const ControlResult listed = run(QStringLiteral("mastering.sequence_list"));
	return listed.result.value(QStringLiteral("songs")).toArray();
}

QJsonObject songAt(int index) { return songs().at(index).toObject(); }

//! The `source` values in release order - the order and the references in one read.
QStringList sources()
{
	QStringList out;
	for (const QJsonValue& value : songs()) { out << value.toObject().value(QStringLiteral("source")).toString(); }
	return out;
}

int undoDepth() { return Engine::projectJournal()->undoDepth(); }

//! The saved document's <mastering-sequence> element (null when the file does not carry one).
QDomElement sequenceElement(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) { return QDomElement(); }
	QDomDocument doc;
	const QByteArray bytes = file.readAll();
	if (!doc.setContent(bytes)) { return QDomElement(); }
	// The section is written beside the track container inside the document's own <song>
	// element, so it is found by name rather than assumed to be a child of the root.
	const QDomNodeList found = doc.elementsByTagName(QStringLiteral("mastering-sequence"));
	return found.isEmpty() ? QDomElement() : found.at(0).toElement();
}

bool fileHasSequence(const QString& path) { return !sequenceElement(path).isNull(); }

void addTwoSongs(const QString& first, const QString& second)
{
	const ControlResult one = run(QStringLiteral("mastering.song_add"), {{QStringLiteral("source"), first},
		{QStringLiteral("title"), QStringLiteral("Opening")}});
	QVERIFY2(one.ok, qPrintable(one.errorMessage));
	const ControlResult two = run(QStringLiteral("mastering.song_add"), {{QStringLiteral("source"), second},
		{QStringLiteral("title"), QStringLiteral("Closing")}});
	QVERIFY2(two.ok, qPrintable(two.errorMessage));
}

//! Every song field the acceptance names, as one comparable string per song.
QStringList releaseState()
{
	QStringList out;
	for (const QJsonValue& value : songs())
	{
		const QJsonObject song = value.toObject();
		out << QStringLiteral("%1|%2|%3|%4|%5|%6").arg(song.value(QStringLiteral("song")).toString(),
			song.value(QStringLiteral("source")).toString(), song.value(QStringLiteral("processing")).toString())
			.arg(song.value(QStringLiteral("gap_ms")).toInt()).arg(song.value(QStringLiteral("fade_in_ms")).toInt())
			.arg(song.value(QStringLiteral("fade_out_ms")).toInt());
	}
	return out;
}

} // namespace

class MasteringSequenceTest : public QObject
{
	Q_OBJECT

private:
	QTemporaryDir m_dir;
	QString m_first;
	QString m_second;

private slots:
	void initTestCase()
	{
		QVERIFY(m_dir.isValid());
		Engine::init(true);
		ControlRegistry::setReady(true);
		m_first = taketest::writeToneWav(QDir(m_dir.path()).filePath(QStringLiteral("opening.wav")), 220.0);
		m_second = taketest::writeToneWav(QDir(m_dir.path()).filePath(QStringLiteral("closing.wav")), 660.0);
		QVERIFY(!m_first.isEmpty());
		QVERIFY(!m_second.isEmpty());
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! A mastering document is what project.new_document makes; its release begins empty and is not written.
	void aNewMasteringDocumentStartsAnEmptyRelease()
	{
		const ControlResult made = run(QStringLiteral("project.new_document"), {{QStringLiteral("type"), QStringLiteral("mastering")}});
		QVERIFY2(made.ok, qPrintable(made.errorMessage));
		QCOMPARE(ProjectInfo::instance().documentType(), QStringLiteral("mastering"));
		QCOMPARE(songs().size(), 0);
		const ControlResult listed = run(QStringLiteral("mastering.sequence_list"));
		QCOMPARE(listed.result.value(QStringLiteral("document_type")).toString(), QStringLiteral("mastering"));
		// The names a song may take are the engine's own candidate set, "none" first.
		QStringList expected{QLatin1String("none")};
		for (const MasteringCandidate& candidate : MasteringJob::defaultCandidates()) { expected << candidate.name; }
		QCOMPARE(listed.result.value(QStringLiteral("processing_names")).toVariant().toStringList(), expected);
		QVERIFY(!MasteringSequence::instance().shouldPersist());
	}

	//! Acceptance, first half: two audio-file songs, and order, processing, gap and fades through the commands.
	void twoSongsAreAddedAndEditedThroughTheRegisteredCommands()
	{
		QVERIFY(run(QStringLiteral("project.new_document"), {{QStringLiteral("type"), QStringLiteral("mastering")}}).ok);
		addTwoSongs(m_first, m_second);
		QCOMPARE(songs().size(), 2);
		QCOMPARE(songAt(0).value(QStringLiteral("source")).toString(), QFileInfo(m_first).absoluteFilePath());
		QCOMPARE(songAt(1).value(QStringLiteral("source")).toString(), QFileInfo(m_second).absoluteFilePath());
		QVERIFY(songAt(0).value(QStringLiteral("exists")).toBool());
		const QString firstId = songAt(0).value(QStringLiteral("song")).toString();
		const QString secondId = songAt(1).value(QStringLiteral("song")).toString();
		QCOMPARE(firstId, QStringLiteral("song-1"));
		QCOMPARE(secondId, QStringLiteral("song-2"));
		QCOMPARE(songAt(0).value(QStringLiteral("gap_applies")).toBool(), true);
		QCOMPARE(songAt(1).value(QStringLiteral("gap_applies")).toBool(), false);  // the release ends here

		// Per-song processing, fades and the silence that follows the first song.
		QVERIFY(run(QStringLiteral("mastering.song_set"), {{QStringLiteral("song"), firstId},
			{QStringLiteral("processing"), QStringLiteral("streaming-14")}, {QStringLiteral("fade_in_ms"), 1500},
			{QStringLiteral("fade_out_ms"), 2500}, {QStringLiteral("gap_ms"), 3000}}).ok);
		QVERIFY(run(QStringLiteral("mastering.song_set"), {{QStringLiteral("song"), secondId},
			{QStringLiteral("processing"), QStringLiteral("ebu-r128")}, {QStringLiteral("fade_in_ms"), 500},
			{QStringLiteral("fade_out_ms"), 4000}, {QStringLiteral("gap_ms"), 0}}).ok);
		QCOMPARE(songAt(0).value(QStringLiteral("processing")).toString(), QStringLiteral("streaming-14"));
		QCOMPARE(songAt(0).value(QStringLiteral("fade_in_ms")).toInt(), 1500);
		QCOMPARE(songAt(0).value(QStringLiteral("fade_out_ms")).toInt(), 2500);
		QCOMPARE(songAt(0).value(QStringLiteral("gap_ms")).toInt(), 3000);
		QCOMPARE(songAt(1).value(QStringLiteral("processing")).toString(), QStringLiteral("ebu-r128"));
		QCOMPARE(songAt(1).value(QStringLiteral("fade_in_ms")).toInt(), 500);
		QCOMPARE(songAt(1).value(QStringLiteral("fade_out_ms")).toInt(), 4000);

		// Reorder: the second song goes first; the ids move with their songs, and each keeps its settings.
		QVERIFY(run(QStringLiteral("mastering.song_move"), {{QStringLiteral("song"), secondId}, {QStringLiteral("to"), 0}}).ok);
		QCOMPARE(songAt(0).value(QStringLiteral("song")).toString(), secondId);
		QCOMPARE(songAt(0).value(QStringLiteral("processing")).toString(), QStringLiteral("ebu-r128"));
		QCOMPARE(songAt(1).value(QStringLiteral("song")).toString(), firstId);
		QCOMPARE(songAt(1).value(QStringLiteral("gap_ms")).toInt(), 3000);
		QCOMPARE(songAt(0).value(QStringLiteral("gap_applies")).toBool(), true);
		QCOMPARE(songAt(1).value(QStringLiteral("gap_applies")).toBool(), false);
	}

	//! Acceptance, second half: save and reopen leaves sources, order, processing, gaps and fades unchanged.
	void theReleaseSurvivesSaveAndReopen()
	{
		QVERIFY(run(QStringLiteral("project.new_document"), {{QStringLiteral("type"), QStringLiteral("mastering")}}).ok);
		addTwoSongs(m_first, m_second);
		const QString firstId = songAt(0).value(QStringLiteral("song")).toString();
		const QString secondId = songAt(1).value(QStringLiteral("song")).toString();
		QVERIFY(run(QStringLiteral("mastering.song_set"), {{QStringLiteral("song"), firstId},
			{QStringLiteral("processing"), QStringLiteral("streaming-16-dynamics")}, {QStringLiteral("fade_in_ms"), 1200},
			{QStringLiteral("fade_out_ms"), 3400}, {QStringLiteral("gap_ms"), 2500}}).ok);
		QVERIFY(run(QStringLiteral("mastering.song_set"), {{QStringLiteral("song"), secondId},
			{QStringLiteral("processing"), QStringLiteral("streaming-14-ceiling-2")}, {QStringLiteral("fade_in_ms"), 700},
			{QStringLiteral("fade_out_ms"), 2100}, {QStringLiteral("gap_ms"), 900}}).ok);
		QVERIFY(run(QStringLiteral("mastering.song_move"), {{QStringLiteral("song"), secondId}, {QStringLiteral("to"), 0}}).ok);
		const QStringList before = releaseState();
		QCOMPARE(before.size(), 2);

		const QString path = QDir(m_dir.path()).filePath(QStringLiteral("release.mmp"));
		QVERIFY2(run(QStringLiteral("project.save"), {{QStringLiteral("path"), path}}).ok, "the release project was not saved");
		QVERIFY(fileHasSequence(path));

		// Replace the session with something else, then reopen: the restore is a real load, not a left-over.
		QVERIFY(run(QStringLiteral("project.new_document"), {{QStringLiteral("type"), QStringLiteral("production")}}).ok);
		QCOMPARE(songs().size(), 0);
		const ControlResult opened = run(QStringLiteral("project.open"), {{QStringLiteral("path"), path}});
		QVERIFY2(opened.ok, qPrintable(opened.errorMessage));
		QCOMPARE(ProjectInfo::instance().documentType(), QStringLiteral("mastering"));
		QCOMPARE(releaseState(), before);
		QCOMPARE(sources(), (QStringList{QFileInfo(m_second).absoluteFilePath(), QFileInfo(m_first).absoluteFilePath()}));
		// The page and the saved document agree about which song is first and which gap is heard.
		QCOMPARE(songAt(0).value(QStringLiteral("song")).toString(), secondId);
		QCOMPARE(songAt(1).value(QStringLiteral("song")).toString(), firstId);
		QCOMPARE(songAt(0).value(QStringLiteral("gap_applies")).toBool(), true);
		QCOMPARE(songAt(1).value(QStringLiteral("gap_applies")).toBool(), false);
		QCOMPARE(sequenceElement(path).firstChildElement(QStringLiteral("song")).attribute(QStringLiteral("source")),
			QFileInfo(m_second).absoluteFilePath());
	}

	//! Each edit is ONE undo step, and its redo puts the same list back.
	void eachEditIsOneUndoStep()
	{
		QVERIFY(run(QStringLiteral("project.new_document"), {{QStringLiteral("type"), QStringLiteral("mastering")}}).ok);
		const int start = undoDepth();
		addTwoSongs(m_first, m_second);
		QCOMPARE(undoDepth(), start + 2);

		const QString firstId = songAt(0).value(QStringLiteral("song")).toString();
		const QString secondId = songAt(1).value(QStringLiteral("song")).toString();
		const int afterAdds = undoDepth();
		QVERIFY(run(QStringLiteral("mastering.song_set"), {{QStringLiteral("song"), firstId}, {QStringLiteral("fade_in_ms"), 1000},
			{QStringLiteral("processing"), QStringLiteral("streaming-14")}, {QStringLiteral("gap_ms"), 2000}}).ok);
		QCOMPARE(undoDepth(), afterAdds + 1);  // three fields, one step
		QVERIFY(run(QStringLiteral("mastering.song_move"), {{QStringLiteral("song"), secondId}, {QStringLiteral("to"), 0}}).ok);
		QCOMPARE(undoDepth(), afterAdds + 2);
		const QStringList beforeRemove = releaseState();
		QVERIFY(run(QStringLiteral("mastering.song_remove"), {{QStringLiteral("song"), firstId}}).ok);
		QCOMPARE(undoDepth(), afterAdds + 3);
		QCOMPARE(songs().size(), 1);
		QCOMPARE(songAt(0).value(QStringLiteral("song")).toString(), secondId);
		QVERIFY(releaseState() != beforeRemove);

		QVERIFY(run(QStringLiteral("control.undo")).ok);  // the removal
		QCOMPARE(songs().size(), 2);
		QCOMPARE(releaseState(), beforeRemove);
		QCOMPARE(songs().at(0).toObject().value(QStringLiteral("song")).toString(), secondId);
		QCOMPARE(songs().at(1).toObject().value(QStringLiteral("song")).toString(), firstId);
		QCOMPARE(songs().at(1).toObject().value(QStringLiteral("fade_in_ms")).toInt(), 1000);
		QCOMPARE(songs().at(1).toObject().value(QStringLiteral("processing")).toString(), QStringLiteral("streaming-14"));
		QCOMPARE(songs().at(1).toObject().value(QStringLiteral("gap_ms")).toInt(), 2000);

		QVERIFY(run(QStringLiteral("control.undo")).ok);  // the move
		QCOMPARE(songAt(0).value(QStringLiteral("song")).toString(), firstId);
		QCOMPARE(songAt(1).value(QStringLiteral("song")).toString(), secondId);
		QVERIFY(run(QStringLiteral("control.redo")).ok);  // and back again
		QCOMPARE(songAt(0).value(QStringLiteral("song")).toString(), secondId);

		QVERIFY(run(QStringLiteral("control.undo")).ok);  // the move, once more, for the field edit below
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // the field edit: all three values return together
		QCOMPARE(songAt(0).value(QStringLiteral("fade_in_ms")).toInt(), 0);
		QCOMPARE(songAt(0).value(QStringLiteral("processing")).toString(), QLatin1String(MasteringSequence::NoProcessing));
		QCOMPARE(songAt(0).value(QStringLiteral("gap_ms")).toInt(), 0);
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // the second add
		QCOMPARE(songs().size(), 1);
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // the first add
		QCOMPARE(songs().size(), 0);
	}

	//! A refused edit changes nothing and records no step; an edit to the values already held records none either.
	void refusalsChangeNothingAndRecordNoStep()
	{
		QVERIFY(run(QStringLiteral("project.new_document"), {{QStringLiteral("type"), QStringLiteral("mastering")}}).ok);
		addTwoSongs(m_first, m_second);
		const QString id = songAt(0).value(QStringLiteral("song")).toString();
		const int depth = undoDepth();
		const QStringList before = releaseState();

		QCOMPARE(run(QStringLiteral("mastering.song_add"), {{QStringLiteral("source"), QDir(m_dir.path()).filePath(QStringLiteral("gone.wav"))}}).errorKind,
			ControlErrorKind::NotFound);
		QCOMPARE(run(QStringLiteral("mastering.song_add"), {{QStringLiteral("source"), m_first},
			{QStringLiteral("processing"), QStringLiteral("louder-please")}}).errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(run(QStringLiteral("mastering.song_add"), {{QStringLiteral("source"), m_first},
			{QStringLiteral("gap_ms"), 900000}}).errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(run(QStringLiteral("mastering.song_add"), {{QStringLiteral("source"), m_first},
			{QStringLiteral("fade_in_ms"), -1}}).errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(run(QStringLiteral("mastering.song_set"), {{QStringLiteral("song"), QStringLiteral("song-999")},
			{QStringLiteral("gap_ms"), 10}}).errorKind, ControlErrorKind::NotFound);
		QCOMPARE(run(QStringLiteral("mastering.song_set"), {{QStringLiteral("song"), id},
			{QStringLiteral("processing"), QStringLiteral("nope")}}).errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(run(QStringLiteral("mastering.song_set"), {{QStringLiteral("song"), id},
			{QStringLiteral("fade_out_ms"), 600001}}).errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(run(QStringLiteral("mastering.song_move"), {{QStringLiteral("song"), QStringLiteral("song-999")},
			{QStringLiteral("to"), 0}}).errorKind, ControlErrorKind::NotFound);
		QCOMPARE(run(QStringLiteral("mastering.song_remove"), {{QStringLiteral("song"), QStringLiteral("song-999")}}).errorKind,
			ControlErrorKind::NotFound);
		QCOMPARE(undoDepth(), depth);
		QCOMPARE(releaseState(), before);

		// Setting the values a song already holds is a success with no step.
		QVERIFY(run(QStringLiteral("mastering.song_set"), {{QStringLiteral("song"), id}, {QStringLiteral("title"), QStringLiteral("Opening")},
			{QStringLiteral("gap_ms"), 0}}).ok);
		QCOMPARE(undoDepth(), depth);
	}

	//! An element holding only some songs, or a hand-edited one, loads what it can and never twice the same id.
	void aHandEditedSequenceLoadsDefensively()
	{
		QDomDocument doc;
		QDomElement section = doc.createElement(QStringLiteral("mastering-sequence"));
		doc.appendChild(section);
		const auto add = [&doc, &section](const QString& source, const QString& id, const QString& fade, const QString& gap,
			const QString& processing) {
			QDomElement song = doc.createElement(QStringLiteral("song"));
			if (!id.isEmpty()) { song.setAttribute(QStringLiteral("id"), id); }
			song.setAttribute(QStringLiteral("source"), source);
			if (!fade.isEmpty()) { song.setAttribute(QStringLiteral("fade-in-ms"), fade); }
			if (!gap.isEmpty()) { song.setAttribute(QStringLiteral("gap-ms"), gap); }
			if (!processing.isEmpty()) { song.setAttribute(QStringLiteral("processing"), processing); }
			section.appendChild(song);
		};
		add(QStringLiteral("/a.wav"), QStringLiteral("7"), QStringLiteral("-40"), QStringLiteral("999999"), QStringLiteral("streaming-14"));
		add(QStringLiteral("/b.wav"), QStringLiteral("7"), QString(), QString(), QString());  // a duplicate id
		add(QString(), QStringLiteral("8"), QString(), QString(), QString());  // no source: not a song
		add(QStringLiteral("/c.wav"), QStringLiteral("nonsense"), QString(), QString(), QString());

		MasteringSequence::instance().load(section);
		const QVector<MasteringSong>& loaded = MasteringSequence::instance().songs();
		QCOMPARE(loaded.size(), 3);
		QCOMPARE(loaded.at(0).source, QStringLiteral("/a.wav"));
		QCOMPARE(loaded.at(0).fadeInMs, 0);            // negative clamps to 0
		QCOMPARE(loaded.at(0).gapMs, MasteringSequence::MaxGapMs);  // over the bound clamps to it
		QCOMPARE(loaded.at(0).processing, QStringLiteral("streaming-14"));
		QVERIFY(loaded.at(0).id != loaded.at(1).id);  // the duplicate got its own id
		QCOMPARE(loaded.at(2).source, QStringLiteral("/c.wav"));
		QVERIFY(loaded.at(2).id > 0);                 // a nonsense id was re-assigned

		// Reset on absence: clearing the project empties the sequence.
		Engine::getSong()->clearProject();
		QCOMPARE(MasteringSequence::instance().songs().size(), 0);
		QVERIFY(!MasteringSequence::instance().shouldPersist());
	}

	//! A project that never had a sequence re-saves byte for byte: no element when the list is empty.
	void anEmptyReleaseWritesNoElement()
	{
		QVERIFY(run(QStringLiteral("project.new_document"), {{QStringLiteral("type"), QStringLiteral("mastering")}}).ok);
		const QString empty = QDir(m_dir.path()).filePath(QStringLiteral("empty-release.mmp"));
		QVERIFY(run(QStringLiteral("project.save"), {{QStringLiteral("path"), empty}}).ok);
		QVERIFY(!fileHasSequence(empty));

		QVERIFY(run(QStringLiteral("mastering.song_add"), {{QStringLiteral("source"), m_first}}).ok);
		const QString withSong = QDir(m_dir.path()).filePath(QStringLiteral("one-song.mmp"));
		QVERIFY(run(QStringLiteral("project.save"), {{QStringLiteral("path"), withSong}}).ok);
		QVERIFY(fileHasSequence(withSong));

		// Reopening the project saved without the element must take the previous sequence away.
		QVERIFY(run(QStringLiteral("project.open"), {{QStringLiteral("path"), empty}}).ok);
		QCOMPARE(songs().size(), 0);
	}
};

QTEST_GUILESS_MAIN(MasteringSequenceTest)
#include "MasteringSequenceTest.moc"
