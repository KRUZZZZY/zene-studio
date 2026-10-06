/*
 * ProjectToolsTest.cpp - the render queue, batch conversion, the task list, flattening, tracks copied between projects
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
/*! 0.5.0 L12 (W3): a batch conversion runs in the render queue off the control thread and ends with a
 *  report, at the rate it was asked for (WF-040); a queued render can be cancelled and cleared, and the
 *  task list shows the queue's jobs (WF-046); track.flatten refuses a half range before rendering
 *  anything (ED-207); tracks copied to the clipboard paste into a project as new tracks, one undo step
 *  (WF-042); a project cannot be its own subproject (WF-045); an alternative is kept beside the project,
 *  undone off the list, switched to and back with the list carried, and taken off the list with its file
 *  kept (WF-032). */

#include <QtTest>

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QTemporaryDir>

#include <sndfile.h>

#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "Song.h"
#include "TrackFolder.h"
#include "TakeLaneTestSupport.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

QString stateOf(int job)
{
	return run(QStringLiteral("render.queue_report"), {{QStringLiteral("job"), job}}).result.value(QStringLiteral("state")).toString();
}

int alternativeCount()
{
	return static_cast<int>(run(QStringLiteral("project.alternatives")).result.value(QStringLiteral("alternatives")).toArray().size());
}

} // namespace

class ProjectToolsTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_dir.isValid());
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void aConversionEndsWithAReport()
	{
		const QString tone = taketest::writeToneWav(QDir(m_dir.path()).filePath(QStringLiteral("tone.wav")), 440.0);
		QVERIFY(!tone.isEmpty());
		const QString outDir = QDir(m_dir.path()).filePath(QStringLiteral("converted"));
		const ControlResult queued = run(QStringLiteral("render.convert"), {{QStringLiteral("files"), QJsonArray{tone}},
			{QStringLiteral("out_dir"), outDir}, {QStringLiteral("format"), QStringLiteral("flac")}, {QStringLiteral("sample_rate"), 22050},
			{QStringLiteral("bit_depth"), 16}});
		QVERIFY2(queued.ok, qPrintable(queued.errorMessage));
		const int job = queued.result.value(QStringLiteral("jobs")).toArray().first().toObject().value(QStringLiteral("job")).toInt();
		QTRY_COMPARE_WITH_TIMEOUT(stateOf(job), QStringLiteral("done"), 20000);
		const QJsonObject report = run(QStringLiteral("render.queue_report"), {{QStringLiteral("job"), job}}).result.value(QStringLiteral("report")).toObject();
		QVERIFY(!report.value(QStringLiteral("sha256")).toString().isEmpty());
		const QString out = QDir(outDir).filePath(QStringLiteral("tone.flac"));
		SF_INFO info{};
		SNDFILE* file = sf_open(out.toLocal8Bit().constData(), SFM_READ, &info);
		QVERIFY(file != nullptr);
		QCOMPARE(info.samplerate, 22050);
		QVERIFY(info.frames > 40000 && info.frames < 48000);  // two seconds at the new rate
		sf_close(file);
		QCOMPARE(run(QStringLiteral("render.convert"), {{QStringLiteral("files"), QJsonArray{QStringLiteral("/no/such.wav")}},
			{QStringLiteral("out_dir"), outDir}}).errorKind, ControlErrorKind::NotFound);
	}

	void aQueuedRenderIsCancelledAndListed()
	{
		const QString project = QDir(m_dir.path()).filePath(QStringLiteral("other.mmp"));
		Track::create(Track::Type::Instrument, Engine::getSong());
		QVERIFY(Engine::getSong()->saveProjectFile(project));
		const ControlResult queued = run(QStringLiteral("render.queue_add"), {{QStringLiteral("out"), QDir(m_dir.path()).filePath(QStringLiteral("r.wav"))},
			{QStringLiteral("project"), project}, {QStringLiteral("start"), false}});
		QVERIFY2(queued.ok, qPrintable(queued.errorMessage));
		const int job = queued.result.value(QStringLiteral("job")).toInt();
		QCOMPARE(queued.result.value(QStringLiteral("state")).toString(), QStringLiteral("queued"));
		bool listed = false;
		for (const QJsonValue& task : run(QStringLiteral("app.tasks"), {{QStringLiteral("active_only"), true}}).result.value(QStringLiteral("tasks")).toArray())
		{
			listed = listed || task.toObject().value(QStringLiteral("id")).toString() == QStringLiteral("render-%1").arg(job);
		}
		QVERIFY(listed);
		QVERIFY(run(QStringLiteral("app.task_cancel"), {{QStringLiteral("id"), QStringLiteral("render-%1").arg(job)}}).ok);
		QCOMPARE(stateOf(job), QStringLiteral("cancelled"));
		QVERIFY(run(QStringLiteral("render.queue_clear")).result.value(QStringLiteral("removed")).toInt() >= 1);
		QCOMPARE(run(QStringLiteral("render.queue_report"), {{QStringLiteral("job"), job}}).errorKind, ControlErrorKind::NotFound);
		QCOMPARE(run(QStringLiteral("app.task_cancel"), {{QStringLiteral("id"), QStringLiteral("mastering")}}).errorKind, ControlErrorKind::Refused);
		QCOMPARE(run(QStringLiteral("render.queue_add"), {{QStringLiteral("out"), QStringLiteral("relative.wav")}}).errorKind,
			ControlErrorKind::InvalidArgs);
	}

	void aQueuedJobIsCancelledByNumber()
	{
		const QString project = QDir(m_dir.path()).filePath(QStringLiteral("queued.mmp"));
		Track::create(Track::Type::Instrument, Engine::getSong());
		QVERIFY(Engine::getSong()->saveProjectFile(project));
		const ControlResult queued = run(QStringLiteral("render.queue_add"), {{QStringLiteral("out"), QDir(m_dir.path()).filePath(QStringLiteral("q.wav"))},
			{QStringLiteral("project"), project}, {QStringLiteral("start"), false}});
		QVERIFY2(queued.ok, qPrintable(queued.errorMessage));
		const int job = queued.result.value(QStringLiteral("job")).toInt();
		const ControlResult cancelled = run(QStringLiteral("render.queue_cancel"), {{QStringLiteral("job"), job}});
		QVERIFY2(cancelled.ok, qPrintable(cancelled.errorMessage));
		QCOMPARE(cancelled.result.value(QStringLiteral("cancelled")).toInt(), 1);
		QCOMPARE(stateOf(job), QStringLiteral("cancelled"));
		QVERIFY(!QFileInfo::exists(QDir(m_dir.path()).filePath(QStringLiteral("q.wav"))));  // dropped before it ran
		// An ended job is not open any more: cancelling it again finds nothing to cancel.
		QCOMPARE(run(QStringLiteral("render.queue_cancel"), {{QStringLiteral("job"), job}}).errorKind, ControlErrorKind::NotFound);
		QVERIFY(run(QStringLiteral("render.queue_clear")).result.value(QStringLiteral("removed")).toInt() >= 1);
	}

	void flattenRefusesAHalfRange()
	{
		Track* track = Track::create(Track::Type::Instrument, Engine::getSong());
		const ControlResult refused = run(QStringLiteral("track.flatten"), {{QStringLiteral("track"), control::trackIdOf(track)},
			{QStringLiteral("start"), 0}});
		QCOMPARE(refused.errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(run(QStringLiteral("track.flatten"), {{QStringLiteral("track"), QStringLiteral("trk-999999")}}).errorKind, ControlErrorKind::NotFound);
	}

	void tracksCopyBetweenProjects()
	{
		Track* lead = Track::create(Track::Type::Instrument, Engine::getSong());
		lead->setName(QStringLiteral("Lead"));
		QVERIFY(run(QStringLiteral("project.copy_tracks"), {{QStringLiteral("tracks"), QJsonArray{control::trackIdOf(lead)}}}).ok);
		const std::size_t before = Engine::getSong()->tracks().size();
		const ControlResult pasted = run(QStringLiteral("project.paste_tracks"));
		QVERIFY2(pasted.ok, qPrintable(pasted.errorMessage));
		QCOMPARE(Engine::getSong()->tracks().size(), before + 1);
		Track* copy = Engine::getSong()->tracks().back();
		QCOMPARE(copy->name(), QStringLiteral("Lead"));
		QVERIFY(copy->id() != lead->id());
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(Engine::getSong()->tracks().size(), before);
		QCOMPARE(run(QStringLiteral("project.copy_tracks"), {{QStringLiteral("tracks"), QJsonArray{QStringLiteral("trk-999999")}}}).errorKind,
			ControlErrorKind::NotFound);
	}

	//! Luna cluster 4770: detach() dropped the stable ids of tracks, clips and effects but not of
	//! notes, so a pasted or imported track's notes kept the ids they had in the source project
	//! and could address the wrong note here through any id-based operation.
	void pastedNotesAreGivenFreshIds()
	{
		Track* lead = Track::create(Track::Type::Instrument, Engine::getSong());
		lead->setName(QStringLiteral("Lead"));
		const QString track = control::trackIdOf(lead);
		const QString clipId = run(QStringLiteral("clip.add"), {{QStringLiteral("track"), track},
			{QStringLiteral("position"), 0}, {QStringLiteral("length"), 192}}).result.value(QStringLiteral("clip")).toString();
		QVERIFY(!clipId.isEmpty());
		QVERIFY(run(QStringLiteral("note.add"), {{QStringLiteral("clip"), clipId},
			{QStringLiteral("key"), 60}, {QStringLiteral("position"), 0}, {QStringLiteral("length"), 48},
			{QStringLiteral("velocity"), 100}}).ok);
		auto* clip = dynamic_cast<MidiClip*>(lead->getClip(0));
		QVERIFY(clip != nullptr);
		QCOMPARE(clip->notes().size(), std::size_t(1));
		const int sourceNoteId = clip->notes().front()->id();
		QVERIFY(run(QStringLiteral("project.copy_tracks"), {{QStringLiteral("tracks"), QJsonArray{track}}}).ok);
		const ControlResult pasted = run(QStringLiteral("project.paste_tracks"));
		QVERIFY2(pasted.ok, qPrintable(pasted.errorMessage));

		Track* copy = nullptr;
		for (Track* candidate : Engine::getSong()->tracks())
		{
			if (candidate->name() == QLatin1String("Lead") && candidate->id() != lead->id()) { copy = candidate; }
		}
		QVERIFY(copy != nullptr);
		auto* copied = dynamic_cast<MidiClip*>(copy->getClip(0));
		QVERIFY(copied != nullptr);
		QCOMPARE(copied->notes().size(), std::size_t(1));
		// The pasted note is a NEW note in THIS project: an id carried over from the source
		// would let an id-based note operation address a note of another project's track.
		QVERIFY2(copied->notes().front()->id() != sourceNoteId, "the pasted note kept the source project's note id");
	}

	void subprojectsRefuseThemselves()
	{
		const QString main = QDir(m_dir.path()).filePath(QStringLiteral("song.mmp"));
		QVERIFY(Engine::getSong()->guiSaveProjectAs(main));
		QCOMPARE(run(QStringLiteral("project.subproject_add"), {{QStringLiteral("path"), main}}).errorKind, ControlErrorKind::Refused);
		QCOMPARE(run(QStringLiteral("project.subproject_add"), {{QStringLiteral("path"), QStringLiteral("/no/such.mmp")}}).errorKind,
			ControlErrorKind::NotFound);
		QCOMPARE(run(QStringLiteral("project.subprojects")).result.value(QStringLiteral("count")).toInt(), 0);
	}

	void alternativesSwitchAndCarryTheList()
	{
		Song* song = Engine::getSong();
		QCOMPARE(run(QStringLiteral("project.alternative_save"), {{QStringLiteral("name"), QStringLiteral("main")}}).errorKind,
			ControlErrorKind::InvalidArgs);
		const ControlResult saved = run(QStringLiteral("project.alternative_save"), {{QStringLiteral("name"), QStringLiteral("radio")}});
		QVERIFY2(saved.ok, qPrintable(saved.errorMessage));
		const QString radio = saved.result.value(QStringLiteral("file")).toString();
		QVERIFY(QFileInfo::exists(radio));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(run(QStringLiteral("project.alternatives")).result.value(QStringLiteral("alternatives")).toArray().size(), 0);
		QVERIFY(run(QStringLiteral("project.alternative_save"), {{QStringLiteral("name"), QStringLiteral("radio")}}).ok);
		const ControlResult switched = run(QStringLiteral("project.alternative_switch"), {{QStringLiteral("name"), QStringLiteral("radio")}});
		QVERIFY2(switched.ok, qPrintable(switched.errorMessage));
		QCOMPARE(QFileInfo(song->projectFileName()).canonicalFilePath(), QFileInfo(radio).canonicalFilePath());
		const ControlResult listed = run(QStringLiteral("project.alternatives"));
		QCOMPARE(listed.result.value(QStringLiteral("active")).toString(), QStringLiteral("radio"));
		QCOMPARE(QFileInfo(listed.result.value(QStringLiteral("main")).toString()).fileName(), QStringLiteral("song.mmp"));
		QVERIFY(run(QStringLiteral("project.alternative_switch"), {{QStringLiteral("name"), QStringLiteral("main")}}).ok);
		QCOMPARE(QFileInfo(song->projectFileName()).fileName(), QStringLiteral("song.mmp"));
		QCOMPARE(run(QStringLiteral("project.alternatives")).result.value(QStringLiteral("alternatives")).toArray().size(), 1);
		QCOMPARE(run(QStringLiteral("project.alternative_switch"), {{QStringLiteral("name"), QStringLiteral("nope")}}).errorKind,
			ControlErrorKind::NotFound);
	}

	void anAlternativeLeavesTheListAndKeepsItsFile()
	{
		const int before = alternativeCount();
		const ControlResult saved = run(QStringLiteral("project.alternative_save"), {{QStringLiteral("name"), QStringLiteral("club")}});
		QVERIFY2(saved.ok, qPrintable(saved.errorMessage));
		QCOMPARE(alternativeCount(), before + 1);
		const ControlResult removed = run(QStringLiteral("project.alternative_remove"), {{QStringLiteral("name"), QStringLiteral("club")}});
		QVERIFY2(removed.ok, qPrintable(removed.errorMessage));
		QCOMPARE(removed.result.value(QStringLiteral("removed")).toString(), QStringLiteral("club"));
		QCOMPARE(removed.result.value(QStringLiteral("file_kept")).toString(), saved.result.value(QStringLiteral("file")).toString());
		QVERIFY(QFileInfo::exists(removed.result.value(QStringLiteral("file_kept")).toString()));  // off the list, still on disk
		QCOMPARE(alternativeCount(), before);
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // the removal: back on the list
		QCOMPARE(alternativeCount(), before + 1);
		QCOMPARE(run(QStringLiteral("project.alternative_remove"), {{QStringLiteral("name"), QStringLiteral("nope")}}).errorKind,
			ControlErrorKind::NotFound);
	}

private:
	QTemporaryDir m_dir;
};

QTEST_MAIN(ProjectToolsTest)
#include "ProjectToolsTest.moc"
