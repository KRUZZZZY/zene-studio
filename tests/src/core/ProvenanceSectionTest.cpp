/*
 * ProvenanceSectionTest.cpp - ARCH-4 slice S6 (SPEC-ARCH-4 1.9 Requirement 8):
 *                             the append-only <z:provenance> section, read
 *                             back from the FILE after a fresh load.
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

#include <QtTest>

#include <QCryptographicHash>
#include <QDateTime>
#include <QDomDocument>
#include <QDomElement>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTemporaryDir>

#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "DocumentIndex.h"
#include "Engine.h"
#include "ProjectIds.h"
#include "ProjectJournal.h"
#include "ProvenanceSection.h"
#include "Song.h"
#include "Track.h"

using namespace lmms;

namespace
{

QString readFile(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) { return QString(); }
	return QString::fromUtf8(file.readAll());
}

QDomDocument parseFile(const QString& path)
{
	QDomDocument document;
	QFile file(path);
	if (file.open(QIODevice::ReadOnly)) { document.setContent(&file); }
	return document;
}

QDomElement provenanceElement(const QDomDocument& document)
{
	const QDomElement song = document.documentElement().firstChildElement(QStringLiteral("song"));
	return song.firstChildElement(QStringLiteral("z:provenance"));
}

QList<QDomElement> changeElements(const QDomElement& provenance)
{
	QList<QDomElement> changes;
	for (QDomNode node = provenance.firstChild(); !node.isNull(); node = node.nextSibling())
	{
		if (node.isElement() && node.nodeName() == QLatin1String("z:change"))
		{
			changes << node.toElement();
		}
	}
	return changes;
}

//! The section's CHILD bytes - from the end of the open tag to the closing
//! tag - as they sit in the file. Append-only is a byte claim, so it is
//! checked on bytes: the first save's children must be a PREFIX of the
//! second's, not merely equal entry-for-entry after a re-serialisation.
QByteArray childrenSlice(const QByteArray& raw)
{
	const int open = raw.indexOf("<z:provenance");
	if (open < 0) { return QByteArray(); }
	const int gt = raw.indexOf('>', open);
	const int close = raw.indexOf("</z:provenance>");
	if (gt < 0 || close < 0 || close < gt) { return QByteArray(); }
	return raw.mid(gt + 1, close - gt - 1);
}

//! The digest rule, written independently of the implementation and applied
//! to what control.transactions reports: if the file's `before` does not
//! equal THIS, the provenance is not quoting the recorded before-state.
QString digestOf(const QJsonObject& object)
{
	const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
	return QStringLiteral("sha256:")
		+ QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

provenance::Change changeFromElement(const QDomElement& element)
{
	provenance::Change change;
	change.seq = element.attribute(QStringLiteral("seq")).toULongLong();
	change.at = element.attribute(QStringLiteral("at"));
	change.actor = element.attribute(QStringLiteral("actor"));
	change.instance = element.attribute(QStringLiteral("instance"));
	change.command = element.attribute(QStringLiteral("command"));
	change.target = element.attribute(QStringLiteral("target"));
	change.before = element.attribute(QStringLiteral("before"));
	change.after = element.attribute(QStringLiteral("after"));
	return change;
}

QList<provenance::Change> changesFrom(const QDomElement& provenance)
{
	QList<provenance::Change> changes;
	for (const QDomElement& element : changeElements(provenance)) { changes << changeFromElement(element); }
	return changes;
}

} // namespace


class ProvenanceSectionTest : public QObject
{
	Q_OBJECT

private:
	QTemporaryDir m_dir;
	QString m_pristine;
	QString m_file1;
	QString m_file2;
	QString m_trackAddr;
	QString m_clipAddr;

	//! Loads \a path and requires the load was not refused. loadProject
	//! constructs its OWN DataFile from the file's bytes - the "fresh
	//! DataFile instance" the S6 proof calls the restart.
	void loadProject(const QString& path)
	{
		Song* song = Engine::getSong();
		song->loadProject(path);
		QVERIFY2(song->loadRefusal().isEmpty(), qPrintable(song->loadRefusal()));
	}

	QList<provenance::Change> heldChanges()
	{
		QList<provenance::Change> changes;
		provenance::Section& section = provenance::Section::instance();
		for (int i = 0; i < section.size(); ++i) { changes << section.at(i); }
		return changes;
	}

private slots:
	void initTestCase()
	{
		QVERIFY2(m_dir.isValid(), "no temporary directory for the round-trip files");
		Engine::init(true);
		ControlRegistry::setReady(true);
		// Every command its own record: the drag-coalescing window would
		// merge back-to-back calls into one record, and the counts below are
		// per-record counts (one record = one <z:change> = one undo step).
		ControlRegistry::instance()->setCoalesceWindowMs(0);
		m_pristine = m_dir.filePath(QStringLiteral("pristine.mmp"));
		// Saved BEFORE any command has run: the additive-rule fixture, a file
		// no change was ever recorded into.
		QVERIFY(Engine::getSong()->saveProjectFile(m_pristine, false));
		QVERIFY(!readFile(m_pristine).contains(QStringLiteral("z:provenance")));
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! THE S6 PROOF: a scripted session's changes are READ BACK from the file
	//! after a restart - seq chain, actor, instance, command, target and both
	//! digests intact - and the digests are re-derived here from what
	//! control.transactions recorded, so they are quotes of the recorded
	//! before-state rather than strings the writer invented.
	void scriptedSessionChangesAreReadBackFromTheFileAfterAFreshLoad()
	{
		loadProject(m_pristine);

		{
			// Driven AS an agent session: the socket's own scope (the same
			// object ControlServer::dispatchLine constructs).
			provenance::AgentScope agent;
			const ControlResult addedTrack = ControlRegistry::instance()->invoke(
				QStringLiteral("track.add"),
				QJsonObject{{QStringLiteral("type"), QStringLiteral("instrument")}});
			QVERIFY2(addedTrack.ok, qPrintable(addedTrack.errorMessage));
			m_trackAddr = addedTrack.result.value(QStringLiteral("track")).toString();
			QVERIFY2(!m_trackAddr.isEmpty(), "track.add must report its stable trk-<n> id");

			const ControlResult addedClip = ControlRegistry::instance()->invoke(
				QStringLiteral("clip.add"),
				QJsonObject{{QStringLiteral("track"), m_trackAddr},
					{QStringLiteral("position"), 0}, {QStringLiteral("length"), 192}});
			QVERIFY2(addedClip.ok, qPrintable(addedClip.errorMessage));
			m_clipAddr = addedClip.result.value(QStringLiteral("clip")).toString();
			QVERIFY2(!m_clipAddr.isEmpty(), "clip.add must report its stable clip-<n> id");

			QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("clip.move"),
				QJsonObject{{QStringLiteral("clip"), m_clipAddr},
					{QStringLiteral("position"), 512}}).ok);
			QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("transport.punch_set"),
				QJsonObject{{QStringLiteral("start"), 0}, {QStringLiteral("end"), 4}}).ok);
		}

		// What the session holds NOW, before anything touches a file: one
		// entry per mutating command driven above (coalescing is off).
		const QList<provenance::Change> expected = heldChanges();
		QCOMPARE(expected.size(), 4);
		for (int i = 0; i < expected.size(); ++i)
		{
			QCOMPARE(expected.at(i).seq, quint64(i + 1)); // a fresh chain: 1,2,3,4
			QCOMPARE(expected.at(i).actor, QStringLiteral("agent"));
			QCOMPARE(expected.at(i).instance, ProjectIds::writerInstance());
			// The command id is the registry's OWN vocabulary (1.9): no new
			// naming layer, so every recorded id must resolve in the registry.
			QVERIFY2(ControlRegistry::instance()->hasCommand(expected.at(i).command),
				qPrintable(QStringLiteral("not a registry id: %1").arg(expected.at(i).command)));
			QVERIFY(!expected.at(i).at.isEmpty());
			QVERIFY(QDateTime::fromString(expected.at(i).at, Qt::ISODate).isValid());
			QVERIFY(expected.at(i).before.startsWith(QStringLiteral("sha256:")));
			QVERIFY(expected.at(i).after.startsWith(QStringLiteral("sha256:")));
		}
		// The target is the object the recorded before-state names: the two
		// address-carrying commands get one; track.add records an AGGREGATE
		// ({track_count, tracks[]}) and the transport's range is no object at
		// all, so both write the attribute not at all (1.9's target is an
		// object id, never a placeholder).
		QVERIFY(expected.at(0).target.isEmpty());      // track.add: an aggregate
		QCOMPARE(expected.at(1).target, m_trackAddr);  // clip.add's before names the track
		QCOMPARE(expected.at(2).target, m_clipAddr);   // clip.move's before names the clip
		QVERIFY(expected.at(3).target.isEmpty());      // punch: tick range, no object

		// The `before` digests QUOTE control.transactions: re-derive them
		// here, from the transaction record, with the test's own hashing.
		const QJsonArray transactions = ControlRegistry::instance()->transactions();
		QVERIFY(transactions.size() >= 4);
		bool sawMove = false;
		for (const QJsonValue& value : transactions)
		{
			const QJsonObject entry = value.toObject();
			if (entry.value(QStringLiteral("command")).toString() != QLatin1String("clip.move"))
			{
				continue;
			}
			sawMove = true;
			QCOMPARE(digestOf(entry.value(QStringLiteral("before")).toObject()), expected.at(2).before);
		}
		QVERIFY2(sawMove, "the transaction record must carry clip.move");

		// SAVE.
		m_file1 = m_dir.filePath(QStringLiteral("session-1.mmp"));
		QVERIFY(Engine::getSong()->saveProjectFile(m_file1, false));
		const QByteArray raw1 = readFile(m_file1).toUtf8();
		QVERIFY(raw1.contains("<z:provenance"));

		// Read the SECTION out of the file's own bytes (not from memory).
		const QDomDocument document1 = parseFile(m_file1);
		const QDomElement provenance1 = provenanceElement(document1);
		QVERIFY(!provenance1.isNull());
		QCOMPARE(provenance1.attribute(QStringLiteral("seq")), QStringLiteral("4"));
		QCOMPARE(provenance1.attribute(QStringLiteral("v")), QStringLiteral("1"));
		QCOMPARE(document1.documentElement().attribute(QStringLiteral("xmlns:z")),
			documentIndexNamespaceUri());
		QList<provenance::Change> inFile = changesFrom(provenance1);
		QCOMPARE(inFile, expected);

		// THE RESTART: a fresh load (loadProject builds a new DataFile from
		// the bytes), then read back.
		loadProject(m_file1);
		const QList<provenance::Change> reloaded = heldChanges();
		QCOMPARE(reloaded.size(), 4);
		for (int i = 0; i < reloaded.size(); ++i)
		{
			// every field verbatim - the digests and the chain survive the
			// restart because they are the file's own strings, not re-derived.
			QCOMPARE(reloaded.at(i).seq, inFile.at(i).seq);
			QCOMPARE(reloaded.at(i).at, inFile.at(i).at);
			QCOMPARE(reloaded.at(i).actor, inFile.at(i).actor);
			QCOMPARE(reloaded.at(i).instance, inFile.at(i).instance);
			QCOMPARE(reloaded.at(i).command, inFile.at(i).command);
			QCOMPARE(reloaded.at(i).target, inFile.at(i).target);
			QCOMPARE(reloaded.at(i).before, inFile.at(i).before);
			QCOMPARE(reloaded.at(i).after, inFile.at(i).after);
		}
		QVERIFY(reloaded.at(0).before.startsWith(QStringLiteral("sha256:")));
		QVERIFY(reloaded.at(0).after.startsWith(QStringLiteral("sha256:")));
	}

	//! APPEND-ONLY: changes after the restart, a SECOND save, and the claim is
	//! checked on bytes - the first save's <z:change> children are a prefix of
	//! the second's - and on the chain, which continues across the reload
	//! instead of restarting.
	void aSecondSaveAppendsAndNeverRewrites()
	{
		loadProject(m_file1);
		QCOMPARE(heldChanges().size(), 4);

		{
			provenance::AgentScope agent;
			QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("clip.move"),
				QJsonObject{{QStringLiteral("clip"), m_clipAddr},
					{QStringLiteral("position"), 1024}}).ok);
			QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("transport.punch_set"),
				QJsonObject{{QStringLiteral("start"), 8}, {QStringLiteral("end"), 16}}).ok);
		}
		const QList<provenance::Change> afterAppend = heldChanges();
		QCOMPARE(afterAppend.size(), 6);
		QCOMPARE(afterAppend.at(4).seq, quint64(5)); // the chain CONTINUES: 5, 6
		QCOMPARE(afterAppend.at(5).seq, quint64(6));

		m_file2 = m_dir.filePath(QStringLiteral("session-2.mmp"));
		QVERIFY(Engine::getSong()->saveProjectFile(m_file2, false));
		const QByteArray raw1 = readFile(m_file1).toUtf8();
		const QByteArray raw2 = readFile(m_file2).toUtf8();
		const QByteArray slice1 = childrenSlice(raw1);
		const QByteArray slice2 = childrenSlice(raw2);
		QVERIFY2(!slice1.isEmpty(), "the first save must carry the section's children");
		QVERIFY2(slice2.startsWith(slice1),
			"the second save must APPEND: the first file's entries are a byte prefix");

		// ...and a restart reads back the whole chain, old and new alike.
		loadProject(m_file2);
		const QList<provenance::Change> reloaded = heldChanges();
		QCOMPARE(reloaded.size(), 6);
		QCOMPARE(reloaded, afterAppend);
		const QDomElement provenance2 = provenanceElement(parseFile(m_file2));
		QCOMPARE(provenance2.attribute(QStringLiteral("seq")), QStringLiteral("6"));
		QCOMPARE(changesFrom(provenance2), afterAppend);
	}

	//! THE ADDITIVE RULE, both halves: a session with no recorded change
	//! writes no section (and a pristine project still round-trips byte for
	//! byte), while a file that carries sections re-saves byte-identically
	//! when this session records nothing new - re-emitted, not rebuilt.
	void noRecordedChangeMeansNoSectionAndAnUnchangedResaveIsByteIdentical()
	{
		loadProject(m_pristine);
		const QString first = m_dir.filePath(QStringLiteral("additive-1.mmp"));
		QVERIFY(Engine::getSong()->saveProjectFile(first, false));
		QVERIFY(!readFile(first).contains(QStringLiteral("z:provenance")));
		loadProject(first);
		const QString second = m_dir.filePath(QStringLiteral("additive-2.mmp"));
		QVERIFY(Engine::getSong()->saveProjectFile(second, false));
		QCOMPARE(readFile(second), readFile(first));

		// A file carrying provenance, loaded and saved with NOTHING recorded
		// in between, is byte-identical: the section was adopted and
		// re-emitted verbatim.
		loadProject(m_file2);
		const QString unchanged = m_dir.filePath(QStringLiteral("unchanged.mmp"));
		QVERIFY(Engine::getSong()->saveProjectFile(unchanged, false));
		QCOMPARE(readFile(unchanged), readFile(m_file2));
	}

	//! THE BOUND: bounded exactly as the in-memory journal is (1.9 cites
	//! ProjectJournal.h) - same count cap, same FIFO rule: the oldest entries
	//! go, the newest is never the one dropped, and the file carries what the
	//! bound retains.
	void theSectionIsBoundedExactlyAsTheJournalIs()
	{
		loadProject(m_pristine);
		ProjectJournal* journal = Engine::projectJournal();
		QVERIFY(journal != nullptr);
		QVERIFY(journal->setMaxUndoStates(3));
		{
			provenance::AgentScope agent;
			for (int i = 0; i < 5; ++i)
			{
				QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("transport.punch_set"),
					QJsonObject{{QStringLiteral("start"), i * 16},
						{QStringLiteral("end"), i * 16 + 8}}).ok);
			}
		}
		QCOMPARE(heldChanges().size(), 3);
		// the three NEWEST (seqs 3,4,5); evictions counted by absence, oldest
		// first - the journal's own rule.
		QCOMPARE(provenance::Section::instance().lastSeq(), quint64(5));
		QCOMPARE(heldChanges().first().seq, quint64(3));

		const QString bounded = m_dir.filePath(QStringLiteral("bounded.mmp"));
		QVERIFY(Engine::getSong()->saveProjectFile(bounded, false));
		const QDomElement provenance = provenanceElement(parseFile(bounded));
		QVERIFY(!provenance.isNull());
		const QList<provenance::Change> inFile = changesFrom(provenance);
		QCOMPARE(inFile.size(), 3);
		QCOMPARE(inFile.at(0).seq, quint64(3));
		QCOMPARE(provenance.attribute(QStringLiteral("seq")), QStringLiteral("5"));

		QVERIFY(journal->setMaxUndoStates(ProjectJournal::MAX_UNDO_STATES));
	}

	//! THE ACTOR: a session KIND, never a person (1.9: "not a credential, not
	//! a user identity - the file must be shareable"). Outside a socket scope
	//! the attribution is "human"; the instance is the same non-identifying
	//! per-process token <head writer> wears - a random UUID fragment, no name,
	//! no path, no address.
	void theActorIsASessionKindNeverAPerson()
	{
		loadProject(m_pristine);
		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("transport.punch_set"),
			QJsonObject{{QStringLiteral("start"), 0}, {QStringLiteral("end"), 8}}).ok);

		const QList<provenance::Change> changes = heldChanges();
		QCOMPARE(changes.size(), 1);
		QCOMPARE(changes.at(0).actor, QStringLiteral("human"));
		// MEASURED shape of ProjectIds::writerInstance():
		// QUuid::createUuid().toString(WithoutBraces).left(12) - a random UUID
		// fragment, eight hex digits then the group dash then three more.
		// Nothing identifying: no name, no path, no address.
		QVERIFY2(QRegularExpression(QStringLiteral("^[0-9a-f]{8}-[0-9a-f]{3}$"))
			.match(changes.at(0).instance).hasMatch(),
			"the instance must be the bare UUID fragment, not anything identifying");
		QVERIFY(ControlRegistry::instance()->hasCommand(changes.at(0).command));
	}
};

QTEST_GUILESS_MAIN(ProvenanceSectionTest)
#include "ProvenanceSectionTest.moc"
