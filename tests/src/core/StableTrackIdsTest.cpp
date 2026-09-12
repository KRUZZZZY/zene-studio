/*
 * StableTrackIdsTest.cpp - the acceptance tests for SPEC-stable-ids.md slice 1
 *                          (creation-assigned, persisted trk-<n> ids).
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
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QTemporaryDir>
#include <QTextStream>

#include "ControlRegistry.h"
#include "DataFile.h"
#include "Engine.h"
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

QString nodeToString(const QDomNode& node)
{
	QString out;
	QTextStream stream(&out);
	node.save(stream, 2);
	stream.flush();
	return out;
}

QStringList attributeNames(const QDomElement& element)
{
	QStringList names;
	const QDomNamedNodeMap attributes = element.attributes();
	for (int i = 0; i < attributes.length(); ++i)
	{
		names << attributes.item(i).nodeName();
	}
	names.sort();
	return names;
}

//! The element's OWN attributes as deterministic "name=value" strings, sorted.
//! Comparing this is order-independent, which the raw node text is not: this tree
//! already churns nested attribute order between two saves (GIT-FRIENDLY-MMPZ.md
//! section 7 measured it, Qt6 hash-seeded), so a whole-subtree byte comparison of
//! a synthesised track is not a fair test of the id work. The whole-DOCUMENT
//! byte-identity claim is asserted in the project-level case below, where it
//! holds, because a document re-parsed from a file carries the file's order.
QStringList ownAttributes(const QDomElement& element)
{
	QStringList pairs;
	const QDomNamedNodeMap attributes = element.attributes();
	for (int i = 0; i < static_cast<int>(attributes.length()); ++i)
	{
		const QDomNode attribute = attributes.item(i);
		pairs << attribute.nodeName() + QStringLiteral("=") + attribute.nodeValue();
	}
	pairs.sort();
	return pairs;
}

//! The attribute set an upstream <track> element carries, from Track::saveTrack.
QStringList upstreamTrackAttributes()
{
	return QStringList{QStringLiteral("muted"), QStringLiteral("mutedBeforeSolo"),
		QStringLiteral("name"), QStringLiteral("solo"), QStringLiteral("type")};
}

/*! A minimal legacy (id-less) song project. `withIds` appends the attributes this
 *  change introduces, so the same document can be loaded before and after. */
QString legacyProject(const QString& version, const QStringList& ids = {},
	const QString& nextId = QString())
{
	QString tracks;
	const QStringList names{QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")};
	for (int i = 0; i < names.size(); ++i)
	{
		const QString id = i < ids.size() ? QStringLiteral(" id=\"%1\"").arg(ids[i]) : QString();
		tracks += QStringLiteral("      <track type=\"0\" name=\"%1\" muted=\"0\"%2>\n"
			"        <instrumenttrack/>\n"
			"      </track>\n").arg(names[i], id);
	}
	return QStringLiteral(
		"<?xml version=\"1.0\"?>\n"
		"<lmms-project version=\"%1\" type=\"song\" creator=\"LMMS\" creatorversion=\"1.2.0\"%2>\n"
		"  <head/>\n"
		"  <song>\n"
		"    <trackcontainer type=\"song\">\n"
		"%3"
		"    </trackcontainer>\n"
		"  </song>\n"
		"</lmms-project>\n")
		.arg(version,
			nextId.isEmpty() ? QString() : QStringLiteral(" next-id=\"%1\"").arg(nextId),
			tracks);
}

} // namespace


class StableTrackIdsTest : public QObject
{
	Q_OBJECT

private:
	QTemporaryDir m_dir;

	//! Every song track's id, in container order.
	QVector<int> trackIds() const
	{
		QVector<int> ids;
		const TrackContainer::TrackList& tracks = Engine::getSong()->tracks();
		for (Track* track : tracks) { ids.append(track->id()); }
		return ids;
	}

	//! Every song track id as the surface reports it, in container order.
	QStringList surfaceIds()
	{
		QStringList ids;
		const QJsonObject state = ControlRegistry::instance()
			->invoke(QStringLiteral("track.list")).result;
		for (const QJsonValue& value : state.value(QStringLiteral("tracks")).toArray())
		{
			ids << value.toObject().value(QStringLiteral("id")).toString();
		}
		return ids;
	}

	//! Writes \a xml to a fresh temp file and returns its path (empty on failure).
	QString writeTemp(const QString& name, const QString& xml)
	{
		const QString path = m_dir.filePath(name);
		QFile file(path);
		if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
		{
			qWarning() << "cannot write the temp project" << path;
			return QString();
		}
		file.write(xml.toUtf8());
		file.close();
		return path;
	}

	//! track.add and the id it answered with (empty when the command failed).
	QString addTrack(const QString& type = QStringLiteral("instrument"))
	{
		const ControlResult added = ControlRegistry::instance()->invoke(
			QStringLiteral("track.add"),
			QJsonObject{{QStringLiteral("type"), type}});
		if (!added.ok)
		{
			qWarning() << "track.add failed:" << added.errorMessage;
			return QString();
		}
		return added.result.value(QStringLiteral("track")).toString();
	}

	//! Loads \a path and requires that the load was not refused.
	void loadProject(const QString& path)
	{
		Song* song = Engine::getSong();
		song->loadProject(path);
		QVERIFY2(song->loadRefusal().isEmpty(), qPrintable(song->loadRefusal()));
	}

	//! The typed failure of track.get_state for \a id.
	ControlResult getTrack(const QString& id)
	{
		return ControlRegistry::instance()->invoke(QStringLiteral("track.get_state"),
			QJsonObject{{QStringLiteral("track"), id}});
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

	//! (c) THE TEST THAT FAILS BEFORE THIS CHANGE. An id is a property of the
	//! object: removing a sibling must not renumber the survivors, the removed id
	//! must stop resolving, and the number must never come back.
	void idsDoNotMoveWhenASiblingIsRemoved()
	{
		const QString first = addTrack();
		const QString second = addTrack();
		const QString third = addTrack();
		QVERIFY(!first.isEmpty() && !second.isEmpty() && !third.isEmpty());
		QVERIFY(first != second && second != third && first != third);

		// all three resolve, and each answers the id it was given
		QCOMPARE(getTrack(first).result.value(QStringLiteral("id")).toString(), first);
		QCOMPARE(getTrack(third).result.value(QStringLiteral("id")).toString(), third);

		const ControlResult removed = ControlRegistry::instance()->invoke(
			QStringLiteral("track.remove"),
			QJsonObject{{QStringLiteral("track"), first}});
		QVERIFY2(removed.ok, qPrintable(removed.errorMessage));

		// the survivors keep their ids, and the surface still reports them
		QVERIFY(getTrack(second).ok);
		QVERIFY(getTrack(third).ok);
		QCOMPARE(getTrack(second).result.value(QStringLiteral("id")).toString(), second);
		QCOMPARE(getTrack(third).result.value(QStringLiteral("id")).toString(), third);
		QVERIFY(surfaceIds().contains(second));
		QVERIFY(surfaceIds().contains(third));
		QVERIFY(!surfaceIds().contains(first));

		// the retired id is a TYPED not_found, never a stale position: before this
		// change `first` was trk-<position> and position 0 was then occupied by
		// `second`, so the old code silently resolved a different track.
		const ControlResult gone = getTrack(first);
		QVERIFY(!gone.ok);
		QCOMPARE(gone.errorKind, ControlErrorKind::NotFound);
		QVERIFY(!gone.errorMessage.isEmpty());

		// a malformed id is still invalid_args, not not_found (the S3 gate's
		// placeholder sweep depends on that typing)
		const ControlResult malformed = getTrack(QStringLiteral(""));
		QVERIFY(!malformed.ok);
		QCOMPARE(malformed.errorKind, ControlErrorKind::InvalidArgs);

		// and a retired number is never handed out again
		const QString replacement = addTrack();
		QVERIFY(replacement != first);
		QVERIFY(replacement != second);
		QVERIFY(replacement != third);
		QVERIFY(getTrack(replacement).ok);

		// leave the song as it was found for the later cases
		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("track.remove"),
			QJsonObject{{QStringLiteral("track"), second}}).ok);
		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("track.remove"),
			QJsonObject{{QStringLiteral("track"), third}}).ok);
		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("track.remove"),
			QJsonObject{{QStringLiteral("track"), replacement}}).ok);
	}

	//! (a) The id is an ATTRIBUTE on the track's own element, it survives
	//! save -> load -> save identically, and it adds exactly one attribute - the
	//! regression guard against a second accidental attribute (and against a child
	//! element, which Track::loadTrack would turn into a phantom clip).
	void idIsAnAttributeAndRoundTripsAtElementLevel()
	{
		Song* song = Engine::getSong();
		Track* track = Track::create(Track::Type::Instrument, song);
		QVERIFY(track != nullptr);

		QDomDocument doc;
		QDomElement parent = doc.createElement(QStringLiteral("song"));
		QDomElement element = track->saveState(doc, parent);
		QCOMPARE(element.tagName(), QStringLiteral("track"));
		QCOMPARE(element.attribute(QStringLiteral("id")).toInt(), track->id());
		// Exactly the upstream attribute set plus `id` - and nothing else. Size
		// plus membership is the same claim as a list equality and does not
		// depend on how QtTest renders a QStringList on failure.
		const QStringList names = attributeNames(element);
		QCOMPARE(names.size(), 6);
		QVERIFY(names.contains(QStringLiteral("id")));
		for (const QString& upstream : upstreamTrackAttributes())
		{
			QVERIFY2(names.contains(upstream), qPrintable(upstream));
		}
		// the id is an ATTRIBUTE, never a child element: an unmatched child of
		// <track> is turned into a real Clip by Track::loadTrack, so an id
		// element would give every track a phantom clip on the next load
		QVERIFY(element.firstChildElement(QStringLiteral("id")).isNull());

		// a known value in, the same value out
		track->setId(4242);
		QDomDocument doc2;
		QDomElement parent2 = doc2.createElement(QStringLiteral("song"));
		QDomElement element2 = track->saveState(doc2, parent2);
		QCOMPARE(element2.attribute(QStringLiteral("id")).toInt(), 4242);
		const QString first = nodeToString(element2);

		// load into a fresh track: the id comes from the file, and the counter is
		// raised above it so nothing else can be given 4242
		Track* loaded = Track::create(Track::Type::Instrument, song);
		loaded->restoreState(element2);
		QCOMPARE(loaded->id(), 4242);
		QVERIFY(ProjectIds::next() > 4242);
		QDomDocument doc3;
		QDomElement parent3 = doc3.createElement(QStringLiteral("song"));
		QDomElement element3 = loaded->saveState(doc3, parent3);
		// The track's OWN attributes survive save -> load -> save unchanged, id
		// included, and the id is still an attribute of <track> and not a value
		// that moved to a child.
		QCOMPARE(ownAttributes(element3), ownAttributes(element2));
		QCOMPARE(element3.attribute(QStringLiteral("id")), QStringLiteral("4242"));
		QCOMPARE(attributeNames(element3), attributeNames(element2));
		QVERIFY(element3.firstChildElement(QStringLiteral("id")).isNull());

		// a track element with no id keeps the constructor's number (R1) and is
		// counted as a load assignment by the caller that reports it
		QDomDocument doc4;
		QDomElement bare = doc4.createElement(QStringLiteral("track"));
		bare.setAttribute(QStringLiteral("type"), 0);
		bare.setAttribute(QStringLiteral("name"), QStringLiteral("legacy"));
		Track* bareTrack = Track::create(Track::Type::Instrument, song);
		const int allocated = bareTrack->id();
		QVERIFY(allocated >= 0);
		bareTrack->restoreState(bare);
		QCOMPARE(bareTrack->id(), allocated);

		delete track;
		delete loaded;
		delete bareTrack;
	}

	//! (b) R1/R2/R3/R4 end to end: an id-less legacy project loads, gets the same
	//! ids on every load, re-saves with exactly those ids plus one `next-id`, and
	//! the file written by this build then re-saves BYTE-IDENTICALLY (the narrowed
	//! SPEC section 5 promise, asserted on the XML and never on .mmpz - Q9).
	void legacyProjectGetsDeterministicIdsAndResavesByteIdentically()
	{
		Song* song = Engine::getSong();
		const QString legacyPath = writeTemp(QStringLiteral("legacy.mmp"),
			legacyProject(QStringLiteral("1.0")));
		const QString legacyBefore = readFile(legacyPath);
		QVERIFY(!legacyBefore.contains(QStringLiteral("id=\"")));
		QVERIFY(!legacyBefore.contains(QStringLiteral("next-id")));

		loadProject(legacyPath);
		const QVector<int> firstLoad = trackIds();
		QCOMPARE(firstLoad.size(), 3);
		// deterministic and in document order: the first track is 0 and the ids
		// are the consecutive numbers the constructor handed out
		QCOMPARE(firstLoad, QVector<int>({0, 1, 2}));
		// the load reported the assignment (the one-time, content-preserving
		// upgrade of Q1) instead of doing it silently
		QCOMPARE(ProjectIds::loadAssignments(), 3);

		// the load itself wrote NOTHING: the legacy file is byte-identical
		QCOMPARE(readFile(legacyPath), legacyBefore);

		// a SECOND load of the same id-less file yields the SAME ids
		loadProject(legacyPath);
		QCOMPARE(trackIds(), firstLoad);

		// the first save of that legacy project is the reported upgrade: ids plus
		// one next-id on the root
		const QString round1Path = m_dir.filePath(QStringLiteral("round1.mmp"));
		QVERIFY(song->saveProjectFile(round1Path, false));
		const QString round1 = readFile(round1Path);
		QVERIFY(!round1.isEmpty());
		QVERIFY(round1.contains(QStringLiteral("next-id=\"")));
		QDomDocument parsed;
		QVERIFY(parsed.setContent(round1.toUtf8()));
		const QDomNodeList trackElements = parsed.elementsByTagName(QStringLiteral("track"));
		QCOMPARE(trackElements.length(), 3);
		for (int i = 0; i < trackElements.length(); ++i)
		{
			const QDomElement element = trackElements.item(i).toElement();
			QVERIFY2(element.hasAttribute(QStringLiteral("id")),
				"the first save left a track without an id");
			QCOMPARE(element.attribute(QStringLiteral("id")).toInt(), firstLoad[i]);
		}
		QCOMPARE(parsed.documentElement().attribute(QStringLiteral("next-id")).toInt(),
			ProjectIds::next());

		// R4: the ids in the file are load order-free - loading it back gives the
		// same ids - and re-saving produces identical XML
		loadProject(round1Path);
		QCOMPARE(trackIds(), firstLoad);
		QCOMPARE(ProjectIds::loadAssignments(), 0);
		const QString round2Path = m_dir.filePath(QStringLiteral("round2.mmp"));
		QVERIFY(song->saveProjectFile(round2Path, false));
		QCOMPARE(readFile(round2Path), round1);
	}

	//! The root counter is honoured, not recomputed blindly: a file whose next-id
	//! is behind its own ids gets caught up, and a file whose ids are sparse does
	//! not hand a live number to a new track.
	void rootCounterIsHonouredAndNeverReused()
	{
		Song* song = Engine::getSong();
		// ids 5 and 7, and the counter is (wrongly) 1: a merge or an older build
		// can produce exactly this
		const QString path = writeTemp(QStringLiteral("sparse.mmp"),
			legacyProject(QStringLiteral("75"),
				{QStringLiteral("5"), QStringLiteral("7"), QStringLiteral("9")},
				QStringLiteral("1")));
		loadProject(path);
		QCOMPARE(trackIds(), QVector<int>({5, 7, 9}));
		QCOMPARE(ProjectIds::loadAssignments(), 0);
		QVERIFY(ProjectIds::next() >= 10);

		// two live tracks sharing an id (only reachable from an external merge):
		// the second is repaired from the high-water mark and counted
		const QString merged = writeTemp(QStringLiteral("merged.mmp"),
			legacyProject(QStringLiteral("75"),
				{QStringLiteral("4"), QStringLiteral("4"), QStringLiteral("4")},
				QStringLiteral("5")));
		loadProject(merged);
		const QVector<int> repaired = trackIds();
		QCOMPARE(repaired.size(), 3);
		QSet<int> distinct;
		for (int id : repaired) { distinct.insert(id); }
		QCOMPARE(distinct.size(), 3);
		QCOMPARE(ProjectIds::loadAssignments(), 2);
		QVERIFY2(repaired[0] == 4, "the first occurrence in document order keeps the id");

		// a new track never takes a number the counter has passed
		const int fresh = Track::create(Track::Type::Instrument, song)->id();
		QVERIFY(!repaired.contains(fresh));
	}

	//! project.open reports the upgrade (Q1) - extending the object that already
	//! carries loaded_with_errors/error_count/errors[], not a second one.
	void projectOpenReportsTheIdUpgrade()
	{
		const QString legacyPath = writeTemp(QStringLiteral("report-legacy.mmp"),
			legacyProject(QStringLiteral("1.0")));
		ControlRegistry* registry = ControlRegistry::instance();
		const ControlResult opened = registry->invoke(QStringLiteral("project.open"),
			QJsonObject{{QStringLiteral("path"), legacyPath}});
		QVERIFY2(opened.ok, qPrintable(opened.errorMessage));
		QCOMPARE(opened.result.value(QStringLiteral("ids_assigned")).toInt(), 3);
		QVERIFY(opened.result.value(QStringLiteral("format_upgraded")).toBool());
		// and it is the same object that carries the load-error report
		QVERIFY(opened.result.contains(QStringLiteral("loaded_with_errors")));
		QVERIFY(opened.result.contains(QStringLiteral("error_count")));

		// the file this build writes needs no upgrade on the next open
		const QString savedPath = m_dir.filePath(QStringLiteral("report-saved.mmp"));
		QVERIFY(Engine::getSong()->saveProjectFile(savedPath, false));
		const ControlResult reopened = registry->invoke(QStringLiteral("project.open"),
			QJsonObject{{QStringLiteral("path"), savedPath}});
		QVERIFY2(reopened.ok, qPrintable(reopened.errorMessage));
		QCOMPARE(reopened.result.value(QStringLiteral("ids_assigned")).toInt(), 0);
		QVERIFY(!reopened.result.value(QStringLiteral("format_upgraded")).toBool());
	}
};

QTEST_GUILESS_MAIN(StableTrackIdsTest)
#include "StableTrackIdsTest.moc"
