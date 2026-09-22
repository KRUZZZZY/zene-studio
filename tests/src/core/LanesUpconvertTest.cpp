/*
 *  * LanesUpconvertTest.cpp - ARCH-4 slice S7 (lanes native): the <takelanes>
 *                          children + clip `lane` attribute form upconvert to
 *                          the top-level <z:lanes> section (SPEC-ARCH-4 1.1
 *                          sketch, migration row :404, census rows 3/4).
 *
 * The claims this file holds (docs/COMPING.md section 3):
 *
 *  1  The document form. A project that uses comping saves its lane state as
 *     <z:lanes v="1"> in <song> - a top-level namespaced section, never a
 *     child of <track> and never a new <track> attribute (the compatibility
 *     rule, SPEC-ARCH-4 2) - each lane an ENTITY carrying its `id` from the
 *     lane- family, the track's own lane list keyed by the track id, and the
 *     composite beside the lanes it names. The clips keep their `lane`
 *     index reference and their bytes: that side of the format does not
 *     move.
 *
 *  2  The round trip. The entities, their ids, their names, the composite
 *     and the clips' lane references all read back out of the file, and a
 *     save/load/save of the same project is byte-identical - the lane-using
 *     half of "byte identity for every fixture" the S7 row owes.
 *
 *  3  The additive rule. A project that uses none of this writes no
 *     <z:lanes>, no <takelanes> and no xmlns:z binding at all, and re-saves
 *     byte-identically (I9 at document scope).
 *
 *  4  RISK 1's negative control (SPEC-ARCH-4 5.2): everything directly under
 *     a <track> in a saved file is a name the legacy walk CLAIMS (the
 *     track's own settings element, a clip element) or skips as marked; any
 *     other child is new-model state that a reader without this build's
 *     claim materialises as a phantom Clip. The test names the element it
 *     finds. See docs/s7-logs/negative-control.md for the measured RED run
 *     with state planted under <track>.
 *
 * The comping model's own invariants stay where they were, in
 * TakeLaneTest/TakeLaneCompTest; this file is the file-format layer only, so
 * those two needed no change beyond the element vocabulary ride-alongs their
 * own assertions do not reach.
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
 * You should have received a copy of the GNU General Public License
 * along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

#include <QtTest>

#include <QByteArray>
#include <QDomDocument>
#include <QDomElement>
#include <QFile>
#include <QSet>
#include <QString>
#include <QTemporaryDir>
#include <QVector>

#include "Engine.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "Song.h"
#include "TakeLane.h"
#include "TakeLaneTestSupport.h"   // the shared take-lane fixture (ONE definition)
#include "TimePos.h"
#include "Track.h"
#include "TrackContainer.h"

using namespace lmms;
using namespace taketest;

namespace
{

QByteArray readFileBytes(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) { return {}; }
	return file.readAll();
}

/*! One instrument track, in the legacy shape the tree's own id tests synthesise
 *  (StableTrackIdsTest's fixture family): no ids, no next-id, none of this
 *  feature. Loading it and re-saving it is the "uses none of this" project. */
QString minimalLegacyProject()
{
	return QStringLiteral(
		"<?xml version=\"1.0\"?>\n"
		"<lmms-project version=\"75\" type=\"song\" creator=\"LMMS\" creatorversion=\"1.2.0\">\n"
		"  <head/>\n"
		"  <song>\n"
		"    <trackcontainer type=\"song\">\n"
		"      <track type=\"0\" name=\"s\" muted=\"0\">\n"
		"        <instrumenttrack/>\n"
		"      </track>\n"
		"    </trackcontainer>\n"
		"  </song>\n"
		"</lmms-project>\n");
}

} // namespace


class LanesUpconvertTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		QVERIFY(m_dir.isValid());
	}

	void cleanupTestCase()
	{
		Engine::destroy();
	}

	// ---------------------------------------------------- the document form

	//! Claims 1 + 2: the section's shape, the entities' ids, the round trip,
	//! and save/load/save byte-identity for a project that USES lanes.
	void lanesRoundTripThroughTheTopLevelSectionAndResavesIdentically()
	{
		QVector<SampleClip*> clips;
		SampleTrack* track = makeTakeTrack(m_dir.path(), 2, &clips);
		QVERIFY(track != nullptr);
		QCOMPARE(clips.size(), 2);
		const int trackId = track->id();
		const QString trackNode = track->nodeName();

		TakeLaneModel& model = track->takeLanes();
		QCOMPARE(model.addLane(QStringLiteral("base")), 0);
		QCOMPARE(model.addLane(QStringLiteral("take 2")), 1);
		const int baseId = model.lanes().at(0).id;
		const int takeId = model.lanes().at(1).id;
		QVERIFY2(takeId != baseId, "two lane entities answered to one id");
		QVERIFY(model.selectSegment(0, 64, 1));
		QVERIFY(model.selectSegment(64, 128, 1, 7));
		QCOMPARE(static_cast<int>(model.segments().size()), 2);
		clips[1]->setLaneIndex(1);

		Song* song = Engine::getSong();
		const QString round1Path = m_dir.filePath(QStringLiteral("lanes-round1.mmp"));
		QVERIFY(song->saveProjectFile(round1Path, false));
		const QByteArray round1 = readFileBytes(round1Path);
		QVERIFY(!round1.isEmpty());

		// --- the document's shape: the section lives in <song>, not <track>
		QDomDocument parsed;
		QVERIFY(parsed.setContent(round1));
		QVERIFY2(parsed.elementsByTagName(QStringLiteral("takelanes")).isEmpty(),
			"a <takelanes> child reached the file - lane state under <track> is the "
			"compatibility rule's forbidden form (SPEC-ARCH-4 2), and a reader without "
			"the claim branch would materialise it as a phantom Clip");
		const QDomNodeList sections = parsed.elementsByTagName(QStringLiteral("z:lanes"));
		QCOMPARE(sections.length(), 1);
		const QDomElement section = sections.item(0).toElement();
		QCOMPARE(section.parentNode().toElement().tagName(), QStringLiteral("song"));
		QCOMPARE(section.attribute(QStringLiteral("v")), QStringLiteral("1"));
		QVERIFY2(parsed.documentElement().hasAttribute(QStringLiteral("xmlns:z")),
			"the z: prefix must be bound on the root when the section is written");
		const QDomElement group = section.firstChildElement(QStringLiteral("z:tracklanes"));
		QVERIFY(!group.isNull());
		QCOMPARE(group.attribute(QStringLiteral("track")).toInt(), trackId);

		QDomElement lane = group.firstChildElement(QStringLiteral("z:lane"));
		QVERIFY(!lane.isNull());
		QCOMPARE(lane.attribute(QStringLiteral("id")).toInt(), baseId);
		QCOMPARE(lane.attribute(QStringLiteral("index")).toInt(), 0);
		lane = lane.nextSiblingElement(QStringLiteral("z:lane"));
		QVERIFY(!lane.isNull());
		QCOMPARE(lane.attribute(QStringLiteral("id")).toInt(), takeId);
		QCOMPARE(lane.attribute(QStringLiteral("index")).toInt(), 1);
		QCOMPARE(lane.attribute(QStringLiteral("name")), QStringLiteral("take 2"));
		QVERIFY(lane.nextSiblingElement(QStringLiteral("z:lane")).isNull());

		const QDomElement segment = group.firstChildElement(QStringLiteral("z:segment"));
		QVERIFY(!segment.isNull());
		QCOMPARE(segment.attribute(QStringLiteral("begin")).toInt(), 0);
		QCOMPARE(segment.attribute(QStringLiteral("end")).toInt(), 64);
		QCOMPARE(segment.attribute(QStringLiteral("lane")).toInt(), 1);
		QVERIFY(!group.lastChildElement(QStringLiteral("z:segment")).isNull());
		QCOMPARE(group.lastChildElement(QStringLiteral("z:segment"))
			.attribute(QStringLiteral("srcpos")).toInt(), 7);

		// The clip side does not move: the reference stays the `lane` index
		// attribute, written only when non-zero (I9 at clip scope).
		const QDomNodeList clipElements = parsed.elementsByTagName(QStringLiteral("sampleclip"));
		QCOMPARE(clipElements.length(), 2);
		QVERIFY2(clipElements.item(0).toElement().attribute(QStringLiteral("lane")).isEmpty(),
			"a clip on lane 0 must write no lane attribute");
		QCOMPARE(clipElements.item(1).toElement().attribute(QStringLiteral("lane")),
			QStringLiteral("1"));

		// --- read back: entities, ids, composite, references ----------------
		loadProject(round1Path);
		SampleTrack* back = nullptr;
		for (Track* candidate : Engine::getSong()->tracks())
		{
			if (candidate->nodeName() == trackNode)
			{
				back = dynamic_cast<SampleTrack*>(candidate);
				QVERIFY(back != nullptr);
				break;
			}
		}
		QVERIFY2(back != nullptr, "the sample track did not survive the round trip");
		QCOMPARE(back->id(), trackId);
		const TakeLaneModel& restored = back->takeLanes();
		QCOMPARE(restored.laneCount(), 2);
		QCOMPARE(restored.lanes().at(0).id, baseId);
		QCOMPARE(restored.lanes().at(1).id, takeId);
		QCOMPARE(restored.lanes().at(1).name, QStringLiteral("take 2"));
		QCOMPARE(static_cast<int>(restored.segments().size()), 2);
		QCOMPARE(restored.segments().at(1).laneIndex, 1);
		QCOMPARE(restored.segments().at(1).sourceOffset, 7);
		QCOMPARE(static_cast<int>(back->getClips().size()), 2);
		QCOMPARE(back->getClips().at(1)->laneIndex(), 1);
		QCOMPARE(back->getClips().at(0)->laneIndex(), 0);

		// --- save again: byte-identical, section included -------------------
		const QString round2Path = m_dir.filePath(QStringLiteral("lanes-round2.mmp"));
		QVERIFY(song->saveProjectFile(round2Path, false));
		QCOMPARE(readFileBytes(round2Path), round1);
	}

	//! Claim 3: none of this feature anywhere in the bytes of a project that
	//! uses none of it, and the document re-saves byte-identically (the
	//! additive rule at document scope - SPEC-ARCH-4 5.2 risk 2).
	void aProjectThatUsesNoneOfThisWritesNoLanesSection()
	{
		const QString legacyPath = m_dir.filePath(QStringLiteral("minimal.mmp"));
		{
			QFile file(legacyPath);
			QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
			file.write(minimalLegacyProject().toUtf8());
		}
		loadProject(legacyPath);

		Song* song = Engine::getSong();
		const QString round1Path = m_dir.filePath(QStringLiteral("minimal-round1.mmp"));
		QVERIFY(song->saveProjectFile(round1Path, false));
		const QByteArray round1 = readFileBytes(round1Path);
		QVERIFY(!round1.isEmpty());
		QVERIFY2(!round1.contains("z:lanes"),
			"a project with no take lanes must write no <z:lanes> section");
		QVERIFY2(!round1.contains("takelanes"),
			"a project with no take lanes must write no <takelanes> anywhere");
		QVERIFY2(!round1.contains("xmlns:z"),
			"the z: prefix must stay unbound for a project carrying no z: section");

		loadProject(round1Path);
		const QString round2Path = m_dir.filePath(QStringLiteral("minimal-round2.mmp"));
		QVERIFY(song->saveProjectFile(round2Path, false));
		QCOMPARE(readFileBytes(round2Path), round1);
	}

	// ------------------------------------------------------ RISK 1's control

	//! Claim 4: SPEC-ARCH-4 5.2 risk 1 - "every slice that writes under
	//! <track> risks silent data invention". Everything a saved <track>
	//! carries directly is a name the legacy walk claims (its own settings
	//! element, a clip element) or skips as marked; any OTHER child is
	//! new-model state, and a reader without this build's claim branch turns
	//! it into a real Clip. This is the test the planted-writer negative
	//! control is seen RED against (docs/s7-logs/negative-control.md).
	void newModelStateIsNeverWrittenUnderATrack()
	{
		QVector<SampleClip*> clips;
		SampleTrack* track = makeTakeTrack(m_dir.path(), 2, &clips);
		QVERIFY(track != nullptr);
		TakeLaneModel& model = track->takeLanes();
		model.addLane(QStringLiteral("base"));
		model.addLane(QStringLiteral("take 2"));
		QVERIFY(model.selectSegment(0, 64, 1));
		clips[1]->setLaneIndex(1);

		const QString path = m_dir.filePath(QStringLiteral("under-track.mmp"));
		QVERIFY(Engine::getSong()->saveProjectFile(path, false));
		const QByteArray bytes = readFileBytes(path);
		QVERIFY(!bytes.isEmpty());
		QDomDocument parsed;
		QVERIFY(parsed.setContent(bytes));

		// Exactly what the legacy walk handles under <track>: the track's own
		// settings element (by the type's nodeName), the four clip classes'
		// elements plus the four pre-rename spellings, the journalling marker
		// the metadata guard skips, and - named so its ABSENCE is the point -
		// nothing else. Extend this set only with a name some writer already
		// puts there; never with new-model state.
		const QSet<QString> claimed{
			QStringLiteral("sampletrack"), QStringLiteral("instrumenttrack"),
			QStringLiteral("sampleclip"), QStringLiteral("midiclip"),
			QStringLiteral("patternclip"), QStringLiteral("automationclip"),
			QStringLiteral("pattern"), QStringLiteral("sampletco"),
			QStringLiteral("bbtco"), QStringLiteral("automationpattern"),
			QStringLiteral("journallingObject")};

		const QDomNodeList trackElements = parsed.elementsByTagName(QStringLiteral("track"));
		QVERIFY(trackElements.length() >= 1);
		int violations = 0;
		for (int i = 0; i < trackElements.length(); ++i)
		{
			const QDomElement trackElement = trackElements.item(i).toElement();
			for (QDomNode child = trackElement.firstChild(); !child.isNull();
				 child = child.nextSibling())
			{
				if (!child.isElement()) { continue; }
				const QString name = child.nodeName();
				if (claimed.contains(name)) { continue; }
				++violations;
				QFAIL(qPrintable(QStringLiteral(
					"PHANTOM-CLIP RISK: <%1> sits directly under <track> (element %2) in a "
					"file this build wrote. A reader without this build's claim branch "
					"materialises an unrecognised <track> child as a real Clip "
					"(SPEC-ARCH-4 5.2 risk 1): new lane state belongs in the top-level "
					"<z:lanes> section, never under <track>.").arg(name).arg(i)));
			}
		}
		QCOMPARE(violations, 0);
	}

private:
	QTemporaryDir m_dir;

	void loadProject(const QString& path)
	{
		Song* song = Engine::getSong();
		song->loadProject(path);
		QVERIFY2(song->loadRefusal().isEmpty(), qPrintable(song->loadRefusal()));
	}
};

QTEST_GUILESS_MAIN(LanesUpconvertTest)
#include "LanesUpconvertTest.moc"
