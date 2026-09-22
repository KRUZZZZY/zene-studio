/*
 * ProjectRevIdsTest.cpp - ARCH-4 slice S3: the id-FAMILY registry and the
 *                         per-object revision pair (rev / writer).
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
 */

#include <QtTest>

#include <QDir>
#include <QDomDocument>
#include <QDomElement>
#include <QFile>
#include <QSet>
#include <QTemporaryDir>
#include <QTextStream>

#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "ProjectIds.h"
#include "Song.h"
#include "Track.h"
#include "TrackContainer.h"

using namespace lmms;

namespace
{

QString readFile(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) { return QString(); }
	return QString::fromUtf8(file.readAll());
}

//! Every element in the document that wears a `rev` or `writer` attribute, as
//! "tagid rev=… writer=…" strings. The additive proof leans on this: a project
//! nobody revised must produce an EMPTY list, no matter how many times it is
//! saved.
QStringList revisionCarriers(const QDomElement& root)
{
	QStringList carriers;
	if (root.hasAttribute(QStringLiteral("rev")) || root.hasAttribute(QStringLiteral("writer")))
	{
		carriers << root.nodeName() + QStringLiteral(" ") + root.attribute(QStringLiteral("rev"));
	}
	for (QDomNode child = root.firstChild(); !child.isNull(); child = child.nextSibling())
	{
		if (child.isElement()) { carriers << revisionCarriers(child.toElement()); }
	}
	return carriers;
}

QDomDocument parseFile(const QString& path)
{
	QDomDocument doc;
	QFile file(path);
	if (file.open(QIODevice::ReadOnly)) { doc.setContent(&file); }
	return doc;
}

//! The <trackcontainer>'s track elements, in document order.
QList<QDomElement> trackElements(const QDomDocument& doc)
{
	QList<QDomElement> tracks;
	const QDomElement root = doc.documentElement();
	const QDomElement song = root.firstChildElement(QStringLiteral("song"));
	QDomNode container = song.firstChild();
	for (; !container.isNull(); container = container.nextSibling())
	{
		if (!container.isElement()) { continue; }
		for (QDomNode track = container.firstChild(); !track.isNull(); track = track.nextSibling())
		{
			if (track.isElement() && track.nodeName() == QLatin1String("track"))
			{
				tracks << track.toElement();
			}
		}
	}
	return tracks;
}

QDomElement headElement(const QDomDocument& doc)
{
	return doc.documentElement().firstChildElement(QStringLiteral("head"));
}

} // namespace


class ProjectRevIdsTest : public QObject
{
	Q_OBJECT

private:
	QTemporaryDir m_dir;

	//! Loads \a path and requires that the load was not refused.
	void loadProject(const QString& path)
	{
		Song* song = Engine::getSong();
		song->loadProject(path);
		QVERIFY2(song->loadRefusal().isEmpty(), qPrintable(song->loadRefusal()));
	}

	//! track.add and whether it answered.
	bool addTrack()
	{
		return ControlRegistry::instance()->invoke(QStringLiteral("track.add"),
			QJsonObject{{QStringLiteral("type"), QStringLiteral("instrument")}}).ok;
	}

private slots:
	void initTestCase()
	{
		QVERIFY2(m_dir.isValid(), "no temporary directory for the round-trip files");
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! ITEM 1 (S3 row: id families): the registry carries every family the
	//! spec names, each with the exact prefix the grammar parses, and every
	//! prefix resolves through control::idToIndex - the parser that turns a
	//! surface address back into the one counter's number. One counter, one
	//! repair rule, five declared families waiting on their objects (S7+).
	void familyRegistryCarriesEveryNamedFamily()
	{
		using F = ProjectIds::IdFamily;
		const QPair<F, const char*> expected[] = {
			{F::Track, "trk-"}, {F::Clip, "clip-"}, {F::Lane, "lane-"},
			{F::Note, "note-"}, {F::Scene, "scene-"}, {F::Warp, "warp-"},
			{F::Channel, "ch-"}, {F::Effect, "fx-"}};
		QSet<QString> unique;
		for (const auto& entry : expected)
		{
			const QString prefix = QString::fromLatin1(
				ProjectIds::familyPrefix(entry.first));
			QCOMPARE(prefix, QString::fromLatin1(entry.second));
			QVERIFY2(!unique.contains(prefix), "family prefixes must be unique");
			unique.insert(prefix);
			// the grammar binds the registry to the parser, not just to text
			QCOMPARE(control::idToIndex(prefix + QStringLiteral("3"), prefix), 3);
		}
		// the families whose objects exist today spell the same way
		QCOMPARE(control::clipId(4), QStringLiteral("clip-4"));
		QCOMPARE(control::noteId(4), QStringLiteral("note-4"));
		QCOMPARE(control::trackId(4), QStringLiteral("trk-4"));
		// the counter itself is still ONE number (census row 8): next() peeks
		// it and peeks do not move it
		const int next = ProjectIds::next();
		QCOMPARE(ProjectIds::next(), next);
	}

	//! ITEM 2, the additive half: a project this build writes and re-writes
	//! carries NO revision pair anywhere - not on any track, not `writer` in
	//! <head> - and save/load/save stays byte-identical. "Neither is needed
	//! to load a file" and, for an unrevised file, neither is ever written.
	void anUnrevisedProjectWritesNoRevisionAttributes()
	{
		QVERIFY(addTrack());
		QVERIFY(addTrack());
		QVERIFY(addTrack());

		const QString round1 = m_dir.filePath(QStringLiteral("unrevised-1.mmp"));
		QVERIFY(Engine::getSong()->saveProjectFile(round1, false));
		const QDomDocument doc1 = parseFile(round1);
		QVERIFY(!doc1.documentElement().isNull());
		QCOMPARE(revisionCarriers(doc1.documentElement()), QStringList());
		QVERIFY(!headElement(doc1).hasAttribute(QStringLiteral("writer")));

		loadProject(round1);
		const QString round2 = m_dir.filePath(QStringLiteral("unrevised-2.mmp"));
		QVERIFY(Engine::getSong()->saveProjectFile(round2, false));
		QCOMPARE(readFile(round2), readFile(round1));
		QCOMPARE(revisionCarriers(parseFile(round2).documentElement()), QStringList());
	}

	//! ITEM 2, the revision half: change ONE loaded track, save, and exactly
	//! that track wears `rev="1"` plus the process's instance id, the <head>
	//! names the same instance, and nobody else is stamped.
	void aChangeAfterLoadStampsRevOneAndTheInstanceWriter()
	{
		const QString base = m_dir.filePath(QStringLiteral("change-base.mmp"));
		QVERIFY(Engine::getSong()->saveProjectFile(base, false));
		loadProject(base);

		Track* target = Engine::getSong()->tracks().at(1);
		target->setName(QStringLiteral("B-revised"));

		const QString changed = m_dir.filePath(QStringLiteral("changed.mmp"));
		QVERIFY(Engine::getSong()->saveProjectFile(changed, false));
		const QDomDocument doc = parseFile(changed);
		const QList<QDomElement> tracks = trackElements(doc);
		QCOMPARE(tracks.size(), Engine::getSong()->tracks().size());
		QCOMPARE(tracks.at(1).attribute(QStringLiteral("name")), QStringLiteral("B-revised"));
		QCOMPARE(tracks.at(1).attribute(QStringLiteral("rev")), QStringLiteral("1"));
		QCOMPARE(tracks.at(1).attribute(QStringLiteral("writer")),
			ProjectIds::writerInstance());
		QVERIFY(!tracks.at(0).hasAttribute(QStringLiteral("rev")));
		QVERIFY(!tracks.at(0).hasAttribute(QStringLiteral("writer")));
		QCOMPARE(headElement(doc).attribute(QStringLiteral("writer")),
			ProjectIds::writerInstance());
	}

	//! The pair survives a load that changes nothing: same bytes in, same
	//! bytes out - the re-saver preserves the revision it loaded rather than
	//! inventing a new one.
	void aRevisionSurvivesAnUnchangedResave()
	{
		const QString base = m_dir.filePath(QStringLiteral("keep-base.mmp"));
		QVERIFY(Engine::getSong()->saveProjectFile(base, false));
		loadProject(base);
		Engine::getSong()->tracks().at(0)->setName(QStringLiteral("A-revised"));
		const QString revised = m_dir.filePath(QStringLiteral("keep-revised.mmp"));
		QVERIFY(Engine::getSong()->saveProjectFile(revised, false));

		loadProject(revised);
		const QString resaved = m_dir.filePath(QStringLiteral("keep-resaved.mmp"));
		QVERIFY(Engine::getSong()->saveProjectFile(resaved, false));
		QCOMPARE(readFile(resaved), readFile(revised));
	}

	//! "the writer increments on every change": reload the revised file,
	//! change the same track again, save -> rev 2, same instance writer.
	void theNextChangeIncrementsRev()
	{
		// Absolute counts need a revision-free starting file. Earlier slots
		// leave revisions in the live song, so reset from the clean file slot
		// `anUnrevisedProject...` wrote (same temp dir, same process) and
		// prove it is clean before counting on it.
		const QString clean = m_dir.filePath(QStringLiteral("inc-clean.mmp"));
		loadProject(m_dir.filePath(QStringLiteral("unrevised-1.mmp")));
		QVERIFY(Engine::getSong()->saveProjectFile(clean, false));
		QVERIFY(revisionCarriers(parseFile(clean).documentElement()).isEmpty());

		loadProject(clean);
		Engine::getSong()->tracks().at(0)->setName(QStringLiteral("A-one"));
		const QString first = m_dir.filePath(QStringLiteral("inc-1.mmp"));
		QVERIFY(Engine::getSong()->saveProjectFile(first, false));
		const QList<QDomElement> firstTracks = trackElements(parseFile(first));
		QCOMPARE(firstTracks.at(0).attribute(QStringLiteral("rev")), QStringLiteral("1"));

		loadProject(first);
		Engine::getSong()->tracks().at(0)->setName(QStringLiteral("A-two"));
		const QString second = m_dir.filePath(QStringLiteral("inc-2.mmp"));
		QVERIFY(Engine::getSong()->saveProjectFile(second, false));

		const QList<QDomElement> tracks = trackElements(parseFile(second));
		QCOMPARE(tracks.at(0).attribute(QStringLiteral("name")), QStringLiteral("A-two"));
		QCOMPARE(tracks.at(0).attribute(QStringLiteral("rev")), QStringLiteral("2"));
		QCOMPARE(tracks.at(0).attribute(QStringLiteral("writer")),
			ProjectIds::writerInstance());
	}

	//! The clip family wears the pair too - through Clip::saveState, the one
	//! override all four clip classes share. A fresh clip's first save only
	//! records the baseline (no attributes: a new object has revised nothing);
	//! a loaded clip that changes gets rev 1.
	void clipElementsCarryThePairToo()
	{
		InstrumentTrack track(Engine::getSong());
		auto* clip = dynamic_cast<MidiClip*>(track.createClip(TimePos(0)));
		QVERIFY(clip != nullptr);

		QDomDocument doc;
		QDomElement parent = doc.createElement(QStringLiteral("track"));
		const QDomElement fresh = clip->saveState(doc, parent);
		QVERIFY(!fresh.hasAttribute(QStringLiteral("rev")));
		QVERIFY(!fresh.hasAttribute(QStringLiteral("writer")));

		MidiClip reloaded(&track);
		reloaded.restoreState(fresh);
		QCOMPARE(reloaded.id(), clip->id());
		reloaded.toggleMute();

		QDomDocument doc2;
		QDomElement parent2 = doc2.createElement(QStringLiteral("track"));
		const QDomElement revised = reloaded.saveState(doc2, parent2);
		QCOMPARE(revised.attribute(QStringLiteral("rev")), QStringLiteral("1"));
		QCOMPARE(revised.attribute(QStringLiteral("writer")),
			ProjectIds::writerInstance());
	}

	//! The id repair is ONE rule per family (census row 8): two clips sharing
	//! an id - what a branch merge produces exactly like duplicate tracks -
	//! come back distinct, and the repair counts as a load assignment while
	//! the track ids in the same file are untouched.
	void duplicateClipIdsFromAMergeAreRepaired()
	{
		const QString path = m_dir.filePath(QStringLiteral("dup-clips.mmp"));
		QFile file(path);
		QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
		file.write(
			"<?xml version=\"1.0\"?>\n"
			"<lmms-project version=\"1.1\" type=\"song\" creator=\"LMMS\" "
			"creatorversion=\"1.2.0\" next-id=\"4\">\n"
			"  <head/>\n"
			"  <song>\n"
			"    <trackcontainer type=\"song\">\n"
			"      <track type=\"0\" name=\"A\" muted=\"0\" id=\"1\">\n"
			"        <instrumenttrack/>\n"
			"        <midiclip id=\"7\" pos=\"0\" len=\"192\"/>\n"
			"        <midiclip id=\"7\" pos=\"192\" len=\"192\"/>\n"
			"      </track>\n"
			"      <track type=\"0\" name=\"B\" muted=\"0\" id=\"2\">\n"
			"        <instrumenttrack/>\n"
			"      </track>\n"
			"    </trackcontainer>\n"
			"  </song>\n"
			"</lmms-project>\n");
		file.close();

		loadProject(path);
		const TrackContainer::TrackList& tracks = Engine::getSong()->tracks();
		QCOMPARE(tracks.size(), 2);
		QCOMPARE(tracks.at(0)->id(), 1);
		QCOMPARE(tracks.at(1)->id(), 2);

		QList<int> clipIds;
		for (Clip* clip : tracks.at(0)->getClips()) { clipIds << clip->id(); }
		QCOMPARE(clipIds.size(), 2);
		QVERIFY(clipIds.at(0) != clipIds.at(1));
		QVERIFY(clipIds.contains(7));
		// one repair happened (the duplicate clip), no track needed one
		QCOMPARE(ProjectIds::loadAssignments(), 1);
	}

	//! The side-writer the merge demo drives (tools/mmpz-git/s3-rev-demo.sh
	//! sets the env vars; three separate processes => three instance ids).
	//! Skipped in ordinary runs.
	void demoWritesTheRequestedSide()
	{
		const QString side = QString::fromLocal8Bit(qgetenv("ZENE_S3_DEMO_SIDE"));
		const QString out = QString::fromLocal8Bit(qgetenv("ZENE_S3_DEMO_OUT"));
		if (side.isEmpty()) { QSKIP("ZENE_S3_DEMO_SIDE not set (merge demo only)"); }
		QVERIFY2(!out.isEmpty(), "ZENE_S3_DEMO_OUT must name the file to write");

		if (side == QLatin1String("base"))
		{
			QVERIFY(addTrack());
			QVERIFY(addTrack());
			QVERIFY(addTrack());
		}
		else
		{
			// both sides start from the SAME base file: load it, change one
			// different track each, save - the real writer stamps the pair.
			const QString base = QString::fromLocal8Bit(qgetenv("ZENE_S3_DEMO_BASE"));
			QVERIFY2(QFile::exists(base), "ZENE_S3_DEMO_BASE must name the base file");
			loadProject(base);
			const int changeTrack = side == QLatin1String("ours") ? 0 : 1;
			Track* target = Engine::getSong()->tracks().at(changeTrack);
			target->setName(QStringLiteral("%1-edited-by-%2").arg(target->name(), side));
		}
		QVERIFY2(Engine::getSong()->saveProjectFile(out, false),
			qPrintable(QStringLiteral("could not write ") + out));
	}
};

QTEST_GUILESS_MAIN(ProjectRevIdsTest)
#include "ProjectRevIdsTest.moc"
