/*
 *  ScenesUpconvertTest.cpp - ARCH-4 slice S8 (scenes native): the legacy
 *                          <session>+<scenes>+<clips> form upconverts to the
 *                          top-level <z:scenes> section (SPEC-ARCH-4 5.1 row
 *                          S8, migration row :403).
 *
 * The four claims this file holds (docs/SCENES-NATIVE.md): (1) the document
 * form - one top-level <z:scenes> in <song>, never a child of <track> or any
 * other existing element (compatibility rule, SPEC-ARCH-4 2), cells on the
 * unchanged `track * scenes + scene` rule, no legacy <session> block written;
 * (2) the upconverter - a legacy block loads through the <session> reader into
 * the model the native section fills and re-saves as <z:scenes>, then
 * round-trips byte-identically; (3) the additive rule - a project using none
 * of this writes no section, no block and no xmlns:z binding, byte-identical;
 * (4) RISK 1's negative control - everything under a <track> is claimed or
 * skipped; planted-writer RED run in docs/s8-logs/negative-control.md.
 * Session model invariants stay in SessionModelTest: this file is format only.
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

#include <QByteArray>
#include <QDomDocument>
#include <QDomElement>
#include <QFile>
#include <QSet>
#include <QString>
#include <QTemporaryDir>
#include <QVector>

#include "DataFile.h"
#include "Engine.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "SessionModel.h"
#include "Song.h"
#include "TakeLane.h"
#include "TakeLaneTestSupport.h"   // the shared take-lane fixture (ONE definition)
#include "Track.h"

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

//! The version this build writes - the legacy fixtures below must parse as a
//! current document, which is what DataFile's own version attribute says.
QString currentVersion()
{
	DataFile fresh(DataFile::Type::SongProject);
	return fresh.documentElement().attribute(QStringLiteral("version"));
}

//! One instrument track in the legacy shape the tree's own id tests synthesise
//! (StableTrackIdsTest's fixture family): no ids, no next-id, none of this
//! feature - the "uses none of this" project for the additive pin below.
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

//! A 0.3.x project carrying the LEGACY <session> block - the form every
//! released build wrote and the form the S8 upconverter reads. 2x2 grid, four
//! MIDI cells (the launch test's shape), two named scenes, non-default global
//! quantisation: a reader that loses ANY of it fails below.
QString legacySessionProject()
{
	return QStringLiteral(
		"<?xml version=\"1.0\"?>\n"
		"<lmms-project version=\"%1\" type=\"song\" creator=\"Zene Studio\">\n"
		"  <head/>\n"
		"  <song>\n"
		"    <trackcontainer type=\"song\">\n"
		"      <track type=\"0\" name=\"t\" muted=\"0\">\n"
		"        <instrumenttrack/>\n"
		"      </track>\n"
		"    </trackcontainer>\n"
		"    <session version=\"1\" tracks=\"2\" scenes=\"2\" launchquantisation=\"1\">\n"
		"      <scenes>\n"
		"        <scene index=\"0\" name=\"Scene A\"/>\n"
		"        <scene index=\"1\" name=\"Scene B\" tempo=\"174\"/>\n"
		"      </scenes>\n"
		"      <clips>\n"
		"        <clip track=\"0\" scene=\"0\" type=\"1\" pattern=\"11\" name=\"a-0\"/>\n"
		"        <clip track=\"1\" scene=\"0\" type=\"1\" pattern=\"12\" name=\"b-0\"/>\n"
		"        <clip track=\"0\" scene=\"1\" type=\"1\" pattern=\"13\" name=\"a-1\"/>\n"
		"        <clip track=\"1\" scene=\"1\" type=\"1\" pattern=\"14\" name=\"b-1\"/>\n"
		"      </clips>\n"
		"    </session>\n"
		"  </song>\n"
		"</lmms-project>\n").arg(currentVersion());
}

} // namespace


class ScenesUpconvertTest : public QObject
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

	//! Claim 1 + the byte identity of a project that USES scenes: the
	//! section's shape, its coordinates and fields, no legacy <session> block
	//! in the file, every value read back, and save/load/save byte-identical.
	void scenesRoundTripThroughTheTopLevelSectionAndResavesIdentically()
	{
		Song* song = Engine::getSong();
		song->clearProject();
		SessionModel& model = song->sessionModel();
		QVERIFY(model.isEmpty());

		model.setTrackCount(2);
		model.setSceneCount(2);
		model.setGlobalLaunchQuantisation(LaunchQuantisation::FourBars);
		model.scene(0).setName(QStringLiteral("Scene A"));
		model.scene(1).setName(QStringLiteral("Scene B"));
		model.scene(1).setTempoEnabled(true);
		model.scene(1).setTempo(174.0);
		model.scene(1).setTimeSigEnabled(true);
		model.scene(1).setTimeSigNumerator(7);
		model.scene(1).setTimeSigDenominator(8);

		ClipSlot& cell = model.slot(0, 0);
		cell.setPatternReference(11);
		cell.setName(QStringLiteral("Lead"));
		cell.setLaunchMode(LaunchMode::Repeat);
		cell.setLaunchQuantisation(LaunchQuantisation::Global);
		cell.setLegato(true);
		cell.setLoopStart(48);
		cell.setLoopLength(768);
		cell.setGainDb(-3.25f);
		cell.setTranspose(5);
		cell.setDetune(-7);
		cell.setRamMode(true);
		FollowAction jump;
		jump.type = FollowAction::Type::Jump;
		jump.chance = 0.6;
		jump.linked = false;
		jump.timeBars = 4.0;
		jump.jumpTo = 1;
		cell.addFollowAction(jump);
		model.slot(1, 0).setPatternReference(12);
		model.slot(0, 1).setPatternReference(13);
		model.slot(1, 1).setAudioReference(QStringLiteral("samples/snare.wav"));
		QVERIFY(model.shouldPersist());

		Song* engine = Engine::getSong();
		const QString round1Path = m_dir.filePath(QStringLiteral("scenes-round1.mmp"));
		QVERIFY(engine->saveProjectFile(round1Path, false));
		const QByteArray round1 = readFileBytes(round1Path);
		QVERIFY(!round1.isEmpty());

		// --- the document's shape: one top-level section, no legacy block
		QDomDocument parsed;
		QVERIFY(parsed.setContent(round1));
		QVERIFY2(parsed.elementsByTagName(QStringLiteral("session")).isEmpty(),
			"the project writer must emit the native section, not the legacy "
			"<session> block - the <session> form is the migration's INPUT "
			"(SPEC-ARCH-4 migration row :403), never its output again");
		const QDomNodeList sections = parsed.elementsByTagName(QStringLiteral("z:scenes"));
		QCOMPARE(sections.length(), 1);
		const QDomElement section = sections.item(0).toElement();
		QCOMPARE(section.parentNode().toElement().tagName(), QStringLiteral("song"));
		QCOMPARE(section.attribute(QStringLiteral("v")), QStringLiteral("1"));
		QCOMPARE(section.attribute(QStringLiteral("tracks")).toInt(), 2);
		QCOMPARE(section.attribute(QStringLiteral("scenes")).toInt(), 2);
		QCOMPARE(section.attribute(QStringLiteral("launchquantisation")).toInt(),
			static_cast<int>(LaunchQuantisation::FourBars));
		QVERIFY2(parsed.documentElement().hasAttribute(QStringLiteral("xmlns:z")),
			"the z: prefix must be bound on the root when the section is written");

		// Scene rows: only the two modified ones, each by index.
		const QDomNodeList sceneRows = parsed.elementsByTagName(QStringLiteral("z:scene"));
		QCOMPARE(sceneRows.length(), 2);
		const QDomElement sceneB = sceneRows.item(1).toElement();
		QCOMPARE(sceneB.attribute(QStringLiteral("index")).toInt(), 1);
		QCOMPARE(sceneB.attribute(QStringLiteral("name")), QStringLiteral("Scene B"));
		QCOMPARE(sceneB.attribute(QStringLiteral("tempo")).toDouble(), 174.0);
		QCOMPARE(sceneB.attribute(QStringLiteral("timesig_numerator")).toInt(), 7);
		QCOMPARE(sceneB.attribute(QStringLiteral("timesig_denominator")).toInt(), 8);

		// Cells: four, addressed by the unchanged (track, scene) rule, every
		// field riding the legacy attribute vocabulary so no id or schema moved.
		const QDomNodeList cells = parsed.elementsByTagName(QStringLiteral("z:cell"));
		QCOMPARE(cells.length(), 4);
		const QDomElement lead = cells.item(0).toElement();
		QCOMPARE(lead.attribute(QStringLiteral("track")).toInt(), 0);
		QCOMPARE(lead.attribute(QStringLiteral("scene")).toInt(), 0);
		QCOMPARE(lead.attribute(QStringLiteral("type")).toInt(),
			static_cast<int>(ClipSlot::Type::Midi));
		QCOMPARE(lead.attribute(QStringLiteral("pattern")).toInt(), 11);
		QCOMPARE(lead.attribute(QStringLiteral("name")), QStringLiteral("Lead"));
		QCOMPARE(lead.attribute(QStringLiteral("launchmode")).toInt(),
			static_cast<int>(LaunchMode::Repeat));
		QCOMPARE(lead.attribute(QStringLiteral("quantisation")).toInt(),
			static_cast<int>(LaunchQuantisation::Global));
		QCOMPARE(lead.attribute(QStringLiteral("legato")).toInt(), 1);
		QCOMPARE(lead.attribute(QStringLiteral("loopstart")).toInt(), 48);
		QCOMPARE(lead.attribute(QStringLiteral("looplength")).toInt(), 768);
		QVERIFY(lead.attribute(QStringLiteral("gain")).toDouble() == -3.25);
		QCOMPARE(lead.attribute(QStringLiteral("transpose")).toInt(), 5);
		QCOMPARE(lead.attribute(QStringLiteral("detune")).toInt(), -7);
		QCOMPARE(lead.attribute(QStringLiteral("ram")).toInt(), 1);
		QCOMPARE(lead.firstChildElement(QStringLiteral("followactions"))
			.elementsByTagName(QStringLiteral("followaction")).length(), 1);
		const QDomElement audioCell = cells.item(3).toElement();
		QCOMPARE(audioCell.attribute(QStringLiteral("track")).toInt(), 1);
		QCOMPARE(audioCell.attribute(QStringLiteral("scene")).toInt(), 1);
		QCOMPARE(audioCell.attribute(QStringLiteral("src")), QStringLiteral("samples/snare.wav"));

		// --- read back: every value, through a real load -------------------
		loadProject(round1Path);
		const SessionModel& restored = engine->sessionModel();
		QVERIFY2(restored.hadSessionBlock(),
			"the native section must answer hadSessionBlock() the way the "
			"legacy block did - it is what keeps shouldPersist() stable "
			"across a load/save round trip");
		QCOMPARE(restored.trackCount(), 2);
		QCOMPARE(restored.sceneCount(), 2);
		QCOMPARE(static_cast<int>(restored.globalLaunchQuantisation()),
			static_cast<int>(LaunchQuantisation::FourBars));
		QCOMPARE(restored.scene(0).name(), QStringLiteral("Scene A"));
		QCOMPARE(restored.scene(1).name(), QStringLiteral("Scene B"));
		QVERIFY(restored.scene(1).tempoEnabled());
		QVERIFY(restored.scene(1).tempo() == 174.0);
		QVERIFY(restored.scene(1).timeSigEnabled());
		QCOMPARE(restored.scene(1).timeSigNumerator(), 7);
		QCOMPARE(restored.scene(1).timeSigDenominator(), 8);
		const ClipSlot& back = restored.slot(0, 0);
		QCOMPARE(static_cast<int>(back.type()), static_cast<int>(ClipSlot::Type::Midi));
		QCOMPARE(back.patternId(), 11);
		QCOMPARE(back.name(), QStringLiteral("Lead"));
		QCOMPARE(static_cast<int>(back.launchMode()), static_cast<int>(LaunchMode::Repeat));
		QCOMPARE(static_cast<int>(back.launchQuantisation()),
			static_cast<int>(LaunchQuantisation::Global));
		QVERIFY(back.legato());
		QCOMPARE(back.loopStart(), 48);
		QCOMPARE(back.loopLength(), 768);
		QVERIFY(back.gainDb() == -3.25f);
		QCOMPARE(back.transpose(), 5);
		QCOMPARE(back.detune(), -7);
		QVERIFY(back.ramMode());
		QCOMPARE(back.followActions().size(), std::size_t(1));
		QVERIFY(back.followActions()[0] == jump);
		QCOMPARE(restored.slot(1, 0).patternId(), 12);
		QCOMPARE(restored.slot(0, 1).patternId(), 13);
		QCOMPARE(restored.slot(1, 1).audioSource(), QStringLiteral("samples/snare.wav"));

		// --- save again: byte-identical, section included -------------------
		const QString round2Path = m_dir.filePath(QStringLiteral("scenes-round2.mmp"));
		QVERIFY(engine->saveProjectFile(round2Path, false));
		QCOMPARE(readFileBytes(round2Path), round1);
	}

	// ------------------------------------------------------- the upconverter

	//! Claim 2: a legacy <session> block loads through the old reader INTO the
	//! native model, the save writes <z:scenes> and NOT <session>, and the
	//! native form is then stable save/load/save - the launch test's
	//! build -> save -> reopen flow expressed at the file-format layer.
	void legacySessionBlockUpconvertsToTheNativeSection()
	{
		const QString legacyPath = m_dir.filePath(QStringLiteral("legacy-session.mmp"));
		{
			QFile file(legacyPath);
			QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
			file.write(legacySessionProject().toUtf8());
		}
		loadProject(legacyPath);

		Song* song = Engine::getSong();
		const SessionModel& upconverted = song->sessionModel();
		QVERIFY2(upconverted.hadSessionBlock(),
			"the <session> reader must claim the legacy block - it is now the "
			"upconverter, and an unclaimed block would be preserved verbatim "
			"instead of migrated");
		QCOMPARE(upconverted.trackCount(), 2);
		QCOMPARE(upconverted.sceneCount(), 2);
		QCOMPARE(static_cast<int>(upconverted.globalLaunchQuantisation()),
			static_cast<int>(LaunchQuantisation::Bar));
		QCOMPARE(upconverted.scene(0).name(), QStringLiteral("Scene A"));
		QCOMPARE(upconverted.scene(1).name(), QStringLiteral("Scene B"));
		QVERIFY(upconverted.scene(1).tempoEnabled());
		QVERIFY(upconverted.scene(1).tempo() == 174.0);
		QCOMPARE(upconverted.slot(0, 0).patternId(), 11);
		QCOMPARE(upconverted.slot(1, 0).patternId(), 12);
		QCOMPARE(upconverted.slot(0, 1).patternId(), 13);
		QCOMPARE(upconverted.slot(1, 1).patternId(), 14);
		QCOMPARE(upconverted.slot(0, 0).name(), QStringLiteral("a-0"));

		// The save emits the native form and drops the legacy block.
		const QString nativePath = m_dir.filePath(QStringLiteral("upconverted.mmp"));
		QVERIFY(song->saveProjectFile(nativePath, false));
		const QByteArray native1 = readFileBytes(nativePath);
		QVERIFY(!native1.isEmpty());
		QVERIFY2(!native1.contains("<session"),
			"a file loaded from a legacy <session> block must re-save in the "
			"native form only - the old block was the migration's input");
		QDomDocument parsed;
		QVERIFY(parsed.setContent(native1));
		const QDomNodeList sections = parsed.elementsByTagName(QStringLiteral("z:scenes"));
		QCOMPARE(sections.length(), 1);
		QCOMPARE(sections.item(0).toElement().attribute(QStringLiteral("tracks")).toInt(), 2);
		QCOMPARE(sections.item(0).toElement().attribute(QStringLiteral("scenes")).toInt(), 2);
		QCOMPARE(parsed.elementsByTagName(QStringLiteral("z:cell")).length(), 4);
		QCOMPARE(parsed.elementsByTagName(QStringLiteral("z:scene")).length(), 2);

		// The native form reads back identically and is byte-stable.
		loadProject(nativePath);
		const SessionModel& second = song->sessionModel();
		QVERIFY(second.hadSessionBlock());
		QCOMPARE(second.trackCount(), 2);
		QCOMPARE(second.sceneCount(), 2);
		QCOMPARE(second.slot(0, 0).patternId(), 11);
		QCOMPARE(second.slot(1, 0).patternId(), 12);
		QCOMPARE(second.slot(0, 1).patternId(), 13);
		QCOMPARE(second.slot(1, 1).patternId(), 14);
		QCOMPARE(second.scene(1).name(), QStringLiteral("Scene B"));
		QVERIFY(second.scene(1).tempo() == 174.0);

		const QString native2Path = m_dir.filePath(QStringLiteral("upconverted2.mmp"));
		QVERIFY(song->saveProjectFile(native2Path, false));
		QCOMPARE(readFileBytes(native2Path), native1);
	}

	// ---------------------------------------------------------- the additive rule

	//! Claim 3: none of this feature anywhere in the bytes of a project that
	//! uses none of it, and the document re-saves byte-identically (I9 at
	//! document scope - the byte identity the S8 row owes).
	void aProjectThatUsesNoneOfThisWritesNoScenesSection()
	{
		const QString legacyPath = m_dir.filePath(QStringLiteral("minimal.mmp"));
		{
			QFile file(legacyPath);
			QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
			file.write(minimalLegacyProject().toUtf8());
		}
		loadProject(legacyPath);

		Song* song = Engine::getSong();
		QVERIFY(!song->sessionModel().shouldPersist());
		const QString round1Path = m_dir.filePath(QStringLiteral("minimal-round1.mmp"));
		QVERIFY(song->saveProjectFile(round1Path, false));
		const QByteArray round1 = readFileBytes(round1Path);
		QVERIFY(!round1.isEmpty());
		QVERIFY2(!round1.contains("z:scenes"),
			"a project with no scene state must write no <z:scenes> section");
		QVERIFY2(!round1.contains("session"),
			"a project with no scene state must write no <session> block");
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
	//! control is seen RED against (docs/s8-logs/negative-control.md), with
	//! BOTH z: sections present so the whole S7+S8 document is walked.
	void newModelStateIsNeverWrittenUnderATrack()
	{
		QVector<SampleClip*> clips;
		SampleTrack* track = makeTakeTrack(m_dir.path(), 2, &clips);
		QVERIFY(track != nullptr);
		TakeLaneModel& lanes = track->takeLanes();
		QVERIFY(lanes.addLane(QStringLiteral("base")) >= 0);
		QVERIFY(lanes.addLane(QStringLiteral("take 2")) >= 0);
		QVERIFY(lanes.selectSegment(0, 64, 1));

		// Both native sections in one document: the lanes from S7 and the
		// scene cells this slice writes.
		Song* song = Engine::getSong();
		SessionModel& model = song->sessionModel();
		model.setTrackCount(2);
		model.setSceneCount(2);
		model.scene(0).setName(QStringLiteral("S"));
		model.slot(0, 0).setPatternReference(11);

		const QString path = m_dir.filePath(QStringLiteral("under-track.mmp"));
		QVERIFY(song->saveProjectFile(path, false));
		const QByteArray bytes = readFileBytes(path);
		QVERIFY(!bytes.isEmpty());
		QDomDocument parsed;
		QVERIFY(parsed.setContent(bytes));
		QVERIFY(!parsed.elementsByTagName(QStringLiteral("z:scenes")).isEmpty());
		QVERIFY(!parsed.elementsByTagName(QStringLiteral("z:lanes")).isEmpty());

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
					"(SPEC-ARCH-4 5.2 risk 1): new scene-cell state belongs in the "
					"top-level <z:scenes> section, never under <track>.").arg(name).arg(i)));
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

QTEST_GUILESS_MAIN(ScenesUpconvertTest)
#include "ScenesUpconvertTest.moc"
