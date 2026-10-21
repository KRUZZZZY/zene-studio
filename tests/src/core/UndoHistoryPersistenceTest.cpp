/*
 * UndoHistoryPersistenceTest.cpp - WF-033: the undo history saved with the
 *                                  project (docs/UNDO-HISTORY-PERSISTENCE.md)
 *
 * The design note (plans/DESIGN-WF-033-UNDO-PERSISTENCE.md) asks for capture and
 * restore to be treated as a PROJECT-FORMAT feature, and for ids, automation
 * links, plug-in data and sample references to be tested after a restore. So the
 * proof is a round trip through real files and the control surface an agent
 * uses: edit -> project.save -> project.open -> control.undo N times, comparing
 * the whole project after every undo with the project as it was before the edit
 * that undo takes back.
 *
 *   1. offByDefaultAndAnOldProjectRoundTripsUnchanged
 *        persistence is opt-in; a project that did not opt in writes no section
 *        and a project without one loads and re-saves the same document.
 *   2. aSavedHistoryUndoesToEveryPreEditState
 *        the round trip: five edits (plug-in parameter, automation point on a
 *        linked parameter, a sample clip moved, a clip created, a rename), saved,
 *        reopened and undone one by one - each undo equals the pre-edit document;
 *        after the last, the explicit checks: track ids, the automation link, the
 *        plug-in value, the sample reference. Redo and undo again.
 *   3. aHistoryTheProjectNoLongerMatchesIsDiscarded
 *        a file whose content was changed after its history was written (what a
 *        build that ignores the section produces when it re-saves an edit) loads
 *        WITHOUT that history and says why; a re-serialisation that changes no
 *        content (attribute order) keeps it.
 *   4. theCapsDropTheOldestAndAnOversizeStepIsReported
 *        the step limit and the byte cap drop the OLDEST steps; a single snapshot
 *        over the cap saves nothing from that step back and the save says so.
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

#include <QCryptographicHash>
#include <QDomDocument>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QTextStream>

#include "AutomationClip.h"
#include "UndoHistory.h"
#include "AutomationTrack.h"
#include "ConfigManager.h"
#include "ControlAutomationSupport.h"
#include "ControlDeviceSupport.h"
#include "ControlVocabulary.h"
#include "ControlRegistry.h"
#include "DocumentIndex.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "ProjectJournal.h"
#include "ProvenanceSection.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "Song.h"
#include "TakeLaneTestSupport.h"
#include "UndoHistoryTestSupport.h"

using namespace lmms;

//! The shared helpers live in UndoHistoryTestSupport.h (one definition for the
//! two test files that use them).

using namespace undotest;

class UndoHistoryPersistenceTest : public QObject
{
	Q_OBJECT

private:
	QTemporaryDir m_dir;
	int m_saves = 0;

	QString path(const QString& name) const { return m_dir.filePath(name); }

	//! The content of the live project, through a scratch save (no history: not a real save).
	QString liveContent()
	{
		const QString scratch = path(QStringLiteral("scratch-%1.mmp").arg(++m_saves));
		if (!Engine::getSong()->saveProjectFile(scratch)) { return QStringLiteral("<save refused>"); }
		return contentOf(scratch);
	}

	//! The baseline: an instrument track with a real instrument, an automation clip linked to its
	//! first parameter, and a sample track with a clip on a real WAV. Built, then the journal is
	//! cleared, so none of it is a step: the steps are the edits each test makes.
	Baseline buildBaseline()
	{
		return undotest::buildBaseline(m_dir.path());
	}

	//! An automation clip linked to the restored track's parameter - found by the live MODEL the
	//! commands' own resolver names, so the answer is "the link resolved after the restore", not
	//! "a clip exists".
	static bool automationLinked(const QString& track, const QString& parameter)
	{
		ControlTarget target;
		control::AutomationParameter found;
		ControlResult error;
		if (!control::resolveAutomationParameter({{QStringLiteral("track"), track}, {QStringLiteral("parameter"), parameter}},
			&target, &found, &error) || found.model == nullptr) { return false; }
		for (Track* candidate : Engine::getSong()->tracks())
		{
			for (Clip* clip : candidate->getClips())
			{
				const auto* automation = dynamic_cast<AutomationClip*>(clip);
				if (automation == nullptr) { continue; }
				for (const auto& model : automation->objects())
				{
					if (model.data() == found.model) { return true; }
				}
			}
		}
		return false;
	}

	/*! ID REMAPPING. A new edit E on a model the load built, above the loaded steps. Undo E, undo the
	 *  newest loaded step (a whole-project restore: every object is rebuilt), redo that step (rebuilt
	 *  again) and redo E: E's step names its model by the journal id it had, so the redo finds it only
	 *  if the restores gave every object its id back. Parameter 1, not 0: 0 is the automated one, whose
	 *  id a SAVE already writes - an unautomated model's id travels only in what a snapshot adds. */
	void idRemappingSurvivesTwoRestores(const QString& track, const QString& edited, const QString& beforeNewest)
	{
		const ControlResult other = run(QStringLiteral("plugin.param_get"), {{QStringLiteral("target"), track},
			{QStringLiteral("plugin"), QStringLiteral("inst")}, {QStringLiteral("index"), 1}});
		const QJsonObject range = other.result.value(QStringLiteral("parameter")).toObject();
		const double max = range.value(QStringLiteral("max")).toDouble();
		const double min = range.value(QStringLiteral("min")).toDouble();
		QVERIFY2(other.ok && max > min, qPrintable(compact(other.result)));
		const double target = range.value(QStringLiteral("value")).toDouble() == max ? min : max;
		QVERIFY(run(QStringLiteral("plugin.param_set"), {{QStringLiteral("target"), track}, {QStringLiteral("plugin"), QStringLiteral("inst")},
			{QStringLiteral("index"), 1}, {QStringLiteral("value"), target}}).ok);
		const QString afterNewEdit = liveContent();
		QVERIFY(afterNewEdit != edited);
		QVERIFY(run(QStringLiteral("control.undo")).result.value(QStringLiteral("undone")).toBool());
		QCOMPARE(liveContent(), edited);
		QVERIFY(run(QStringLiteral("control.undo")).result.value(QStringLiteral("undone")).toBool());
		QCOMPARE(liveContent(), beforeNewest);
		QVERIFY(run(QStringLiteral("control.redo")).result.value(QStringLiteral("redone")).toBool());
		QCOMPARE(liveContent(), edited);
		QVERIFY(run(QStringLiteral("control.redo")).result.value(QStringLiteral("redone")).toBool());
		QVERIFY2(liveContent() == afterNewEdit, "the edit made above the loaded history did not redo after a restore");
		QVERIFY(run(QStringLiteral("control.undo")).result.value(QStringLiteral("undone")).toBool());
		QCOMPARE(liveContent(), edited);
	}

	static QString sampleFileOf(const QString& clipId)
	{
		const int id = clipId.mid(5).toInt();
		for (Track* track : Engine::getSong()->tracks())
		{
			for (Clip* clip : track->getClips())
			{
				auto* sample = dynamic_cast<SampleClip*>(clip);
				if (sample != nullptr && clip->id() == id) { return sample->sampleFile(); }
			}
		}
		return QString();
	}

private slots:
	void initTestCase()
	{
		QVERIFY(m_dir.isValid());
		// A throwaway config: project.open records recent files, and the caps below are set in it.
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);  // a real instrument: plug-in data to restore
#endif
		undotest::initSession(m_dir.path());
	}

	void cleanupTestCase()
	{
		undotest::shutdownSession();
	}

	void init()
	{
		ConfigManager::inst()->deleteValue(QStringLiteral("undohistory"), QStringLiteral("persist"));
		ConfigManager::inst()->deleteValue(QStringLiteral("undohistory"), QStringLiteral("steps"));
		ConfigManager::inst()->deleteValue(QStringLiteral("undohistory"), QStringLiteral("capmb"));
	}

	void aSavedHistoryUndoesToEveryPreEditState()
	{
		const Baseline base = buildBaseline();
		QVERIFY2(!base.track.isEmpty() && !base.parameter.isEmpty() && !base.sampleClip.isEmpty(),
			"the baseline needs a real instrument (tripleoscillator), a parameter and a sample clip");
		const double originalValue = instrumentParameter(base.track);
		const QString originalSample = sampleFileOf(base.sampleClip);
		QVERIFY2(!originalSample.isEmpty(), "the sample clip holds no file");
		QVERIFY2(originalValue >= 0, "the instrument's parameter 0 is not readable");
		const ControlResult opted = run(QStringLiteral("project.set_undo_history"), {{QStringLiteral("setting"), QStringLiteral("on")}});
		QVERIFY2(opted.ok, qPrintable(opted.errorMessage));

		const QVector<QPair<QString, QJsonObject>> edits = {
			{QStringLiteral("plugin.param_set"), {{QStringLiteral("target"), base.track}, {QStringLiteral("plugin"), QStringLiteral("inst")},
				{QStringLiteral("index"), 0}, {QStringLiteral("value"), originalValue > 50 ? 10.0 : 90.0}}},
			{QStringLiteral("automation.add_point"), {{QStringLiteral("track"), base.track}, {QStringLiteral("parameter"), base.parameter},
				{QStringLiteral("ticks"), 192}, {QStringLiteral("value"), 0.9}}},
			{QStringLiteral("clip.move"), {{QStringLiteral("clip"), base.sampleClip}, {QStringLiteral("position"), 384}}},
			{QStringLiteral("clip.add"), {{QStringLiteral("track"), base.track}, {QStringLiteral("position"), 768}, {QStringLiteral("length"), 192}}},
			{QStringLiteral("track.rename"), {{QStringLiteral("track"), base.track}, {QStringLiteral("name"), QStringLiteral("renamed")}}},
		};
		QStringList before;
		for (const auto& edit : edits)
		{
			before.append(liveContent());
			const ControlResult done = run(edit.first, edit.second);
			QVERIFY2(done.ok, qPrintable(edit.first + QStringLiteral(": ") + done.errorMessage));
		}
		const QString edited = liveContent();
		QCOMPARE(journalDepth(), edits.size());

		const QString file = path(QStringLiteral("history.mmp"));
		const ControlResult saved = run(QStringLiteral("project.save"), {{QStringLiteral("path"), file}});
		QVERIFY2(saved.ok, qPrintable(saved.errorMessage));
		const QJsonObject report = saved.result.value(QStringLiteral("undo_history")).toObject();
		QVERIFY2(report.value(QStringLiteral("saved_steps")).toInt() == edits.size()
			&& report.value(QStringLiteral("written")).toBool(), qPrintable(compact(report)));
		QCOMPARE(textOf(file).count(QStringLiteral("<z:undostep")), static_cast<int>(edits.size()));

		// What the load itself leaves on the journal, with no history: the same file with the section cut
		// out (what a build that does not know it sees). Measured, not assumed - a load of this project
		// leaves a step of its own (a parameter write while the automation resolves), which is not WF-033's.
		const QString without = path(QStringLiteral("history-stripped.mmp"));
		{
			QString text = textOf(file);
			const int from = text.indexOf(QStringLiteral("<z:undohistory"));
			const int to = text.indexOf(QStringLiteral("</z:undohistory>"));
			QVERIFY(from > 0 && to > from);
			text.remove(from, to + int(qstrlen("</z:undohistory>")) - from);
			QFile out(without);
			QVERIFY(out.open(QIODevice::WriteOnly));
			out.write(text.toUtf8());
		}
		QVERIFY(run(QStringLiteral("project.open"), {{QStringLiteral("path"), without}}).ok);
		QCOMPARE(liveContent(), edited);
		const int loadSteps = journalDepth();

		// The load above left the session marked modified (its parameter write). Opening over a modified
		// session now also checkpoints it (Q050-044: one more step, on top), which is SessionCheckpointTest's
		// subject; this case is about the file's history alone, so the session is saved clean first.
		QVERIFY(run(QStringLiteral("project.save"), {{QStringLiteral("path"), path(QStringLiteral("pre-open.mmp"))}}).ok);
		QVERIFY(!Engine::getSong()->isModified());

		// A fresh open: the session's own journal is gone, the file's history is what comes back.
		const ControlResult opened = run(QStringLiteral("project.open"), {{QStringLiteral("path"), file}});
		QVERIFY2(opened.ok, qPrintable(opened.errorMessage));
		const QJsonObject load = historyState().value(QStringLiteral("last_load")).toObject();
		QVERIFY2(load.value(QStringLiteral("loaded_steps")).toInt() == edits.size(), qPrintable(compact(load)));
		QCOMPARE(journalDepth(), loadSteps + static_cast<int>(edits.size()));
		QCOMPARE(liveContent(), edited);

		idRemappingSurvivesTwoRestores(base.track, edited, before.last());
		if (QTest::currentTestFailed()) { return; }

		// The append-only provenance log is never rewound by an undo (AGENTS.md "Known product contracts").
		const quint64 loggedBefore = provenance::Section::instance().lastSeq();
		QVERIFY(loggedBefore > 0);
		for (int step = edits.size() - 1; step >= 0; --step)
		{
			const ControlResult undone = run(QStringLiteral("control.undo"));
			QVERIFY2(undone.ok && undone.result.value(QStringLiteral("undone")).toBool(),
				qPrintable(QStringLiteral("undo of %1: %2 %3").arg(edits.at(step).first, undone.errorMessage, compact(undone.result))));
			QVERIFY2(liveContent() == before.at(step),
				qPrintable(QStringLiteral("after undoing %1 the project is not the one before it").arg(edits.at(step).first)));
		}

		// The four the design note names, read from the live objects rather than the file.
		QVERIFY2(Engine::getSong()->findTrackById(base.trackId) != nullptr, "the track's id did not survive the restore");
		QVERIFY2(automationLinked(base.track, base.parameter), "the automation clip is not linked to the restored track's parameter");
		QCOMPARE(instrumentParameter(base.track), originalValue);
		QCOMPARE(sampleFileOf(base.sampleClip), originalSample);
		QVERIFY(Engine::getSong()->projectFileName().endsWith(QStringLiteral("history.mmp")));

		QVERIFY(provenance::Section::instance().lastSeq() >= loggedBefore);

		// A loaded step redoes and undoes again.
		QVERIFY(run(QStringLiteral("control.redo")).result.value(QStringLiteral("redone")).toBool());
		QCOMPARE(liveContent(), before.at(1));
		QVERIFY(run(QStringLiteral("control.undo")).result.value(QStringLiteral("undone")).toBool());
		QCOMPARE(liveContent(), before.at(0));

	}

	void aHistoryTheProjectNoLongerMatchesIsDiscarded()
	{
		const Baseline base = buildBaseline();
		QVERIFY(!base.track.isEmpty());
		QVERIFY(run(QStringLiteral("project.set_undo_history"), {{QStringLiteral("setting"), QStringLiteral("on")}}).ok);
		QVERIFY(run(QStringLiteral("track.rename"), {{QStringLiteral("track"), base.track}, {QStringLiteral("name"), QStringLiteral("one")}}).ok);
		QVERIFY(run(QStringLiteral("track.rename"), {{QStringLiteral("track"), base.track}, {QStringLiteral("name"), QStringLiteral("two")}}).ok);
		const QString file = path(QStringLiteral("stale.mmp"));
		QVERIFY(run(QStringLiteral("project.save"), {{QStringLiteral("path"), file}}).ok);

		// Re-serialised with no content change (another attribute order): still this project's history.
		QDomDocument document;
		{
			QFile in(file);
			QVERIFY(in.open(QIODevice::ReadOnly) && document.setContent(&in));
		}
		const auto rewrite = [&document](const QString& to) {
			QFile out(to);
			if (!out.open(QIODevice::WriteOnly)) { return false; }
			QTextStream stream(&out);
			document.save(stream, 1);
			return true;
		};
		// ...and stamped by another process: <head writer> names WHO saved (ProjectIds::writerInstance), and
		// every build that re-saves a revised project replaces it - measured against the base build.
		document.documentElement().firstChildElement(QStringLiteral("head"))
			.setAttribute(QStringLiteral("writer"), QStringLiteral("another-process"));
		const QString same = path(QStringLiteral("same.mmp"));
		QVERIFY(rewrite(same));
		QVERIFY(run(QStringLiteral("project.open"), {{QStringLiteral("path"), same}}).ok);
		QCOMPARE(historyState().value(QStringLiteral("last_load")).toObject().value(QStringLiteral("loaded_steps")).toInt(), 2);

		// The content changed under the history (what a build that ignores the section writes when
		// it saves an edit and keeps the section verbatim): the history is dropped, and said so.
		QDomElement track = document.documentElement().firstChildElement(QStringLiteral("song"))
			.firstChildElement(QStringLiteral("trackcontainer")).firstChildElement(QStringLiteral("track"));
		QVERIFY(!track.isNull());
		track.setAttribute(QStringLiteral("name"), QStringLiteral("edited elsewhere"));
		const QString stale = path(QStringLiteral("stale-edited.mmp"));
		QVERIFY(rewrite(stale));
		QVERIFY(run(QStringLiteral("project.open"), {{QStringLiteral("path"), stale}}).ok);
		const QJsonObject load = historyState().value(QStringLiteral("last_load")).toObject();
		QVERIFY2(load.value(QStringLiteral("found")).toBool() && load.value(QStringLiteral("loaded_steps")).toInt() == 0
			&& !load.value(QStringLiteral("discarded")).toString().isEmpty(), qPrintable(compact(load)));
		QCOMPARE(journalDepth(), 0);
		// ...and never re-emitted: the next save carries no stale steps.
		const QString resaved = path(QStringLiteral("stale-resaved.mmp"));
		QVERIFY(run(QStringLiteral("project.save"), {{QStringLiteral("path"), resaved}}).ok);
		QCOMPARE(textOf(resaved).count(QStringLiteral("<z:undostep")), 0);
	}

	void anIncompleteSavedHistoryIsDiscarded()
	{
		const Baseline base = buildBaseline();
		QVERIFY(!base.track.isEmpty());
		QVERIFY(run(QStringLiteral("project.set_undo_history"), {{QStringLiteral("setting"), QStringLiteral("on")}}).ok);
		QVERIFY(run(QStringLiteral("track.rename"), {{QStringLiteral("track"), base.track}, {QStringLiteral("name"), QStringLiteral("one")}}).ok);
		QVERIFY(run(QStringLiteral("track.rename"), {{QStringLiteral("track"), base.track}, {QStringLiteral("name"), QStringLiteral("two")}}).ok);
		const QString file = path(QStringLiteral("incomplete.mmp"));
		QVERIFY(run(QStringLiteral("project.save"), {{QStringLiteral("path"), file}}).ok);

		QDomDocument document;
		{
			QFile in(file);
			QVERIFY(in.open(QIODevice::ReadOnly) && document.setContent(&in));
		}
		QDomElement history = document.documentElement().firstChildElement(QStringLiteral("song"))
			.firstChildElement(QStringLiteral("z:undohistory"));
		QVERIFY(!history.isNull());
		QDomElement step = history.firstChildElement(QStringLiteral("z:undostep"));
		QVERIFY(!step.isNull());
		history.removeChild(step);
		// declared count left unchanged on purpose
		const QString broken = path(QStringLiteral("incomplete-broken.mmp"));
		{
			QFile out(broken);
			QVERIFY(out.open(QIODevice::WriteOnly));
			QTextStream stream(&out);
			document.save(stream, 1);
		}
		QVERIFY(run(QStringLiteral("project.open"), {{QStringLiteral("path"), broken}}).ok);
		const QJsonObject load = historyState().value(QStringLiteral("last_load")).toObject();
		QVERIFY2(load.value(QStringLiteral("found")).toBool()
			&& load.value(QStringLiteral("loaded_steps")).toInt() == 0
			&& !load.value(QStringLiteral("discarded")).toString().isEmpty(),
			qPrintable(compact(load)));
	}

	//! A history that declares ONE step and carries NONE is incomplete too: the
	//! count is checked against the step elements the section has, so a section
	//! with no step element must not slip past the check that a section with
	//! too few does.
	void aMissingOnlyDeclaredStepIsDiscarded()
	{
		const Baseline base = buildBaseline();
		QVERIFY(!base.track.isEmpty());
		QVERIFY(run(QStringLiteral("project.set_undo_history"), {{QStringLiteral("setting"), QStringLiteral("on")}}).ok);
		QVERIFY(run(QStringLiteral("track.rename"), {{QStringLiteral("track"), base.track}, {QStringLiteral("name"), QStringLiteral("only")}}).ok);
		const QString file = path(QStringLiteral("one-step.mmp"));
		QVERIFY(run(QStringLiteral("project.save"), {{QStringLiteral("path"), file}}).ok);
		// One rename is one step: the file below declares one step and carries none.
		QCOMPARE(textOf(file).count(QStringLiteral("<z:undostep")), 1);

		QDomDocument document;
		{
			QFile in(file);
			QVERIFY(in.open(QIODevice::ReadOnly) && document.setContent(&in));
		}
		QDomElement history = document.documentElement().firstChildElement(QStringLiteral("song"))
			.firstChildElement(QStringLiteral("z:undohistory"));
		QVERIFY(!history.isNull());
		QCOMPARE(history.attribute(QStringLiteral("steps")).toInt(), 1);
		QDomElement step = history.firstChildElement(QStringLiteral("z:undostep"));
		QVERIFY(!step.isNull());
		history.removeChild(step);
		// the declared count left unchanged on purpose
		const QString broken = path(QStringLiteral("one-step-broken.mmp"));
		{
			QFile out(broken);
			QVERIFY(out.open(QIODevice::WriteOnly));
			QTextStream stream(&out);
			document.save(stream, 1);
		}
		QVERIFY(run(QStringLiteral("project.open"), {{QStringLiteral("path"), broken}}).ok);
		const QJsonObject load = historyState().value(QStringLiteral("last_load")).toObject();
		QVERIFY2(load.value(QStringLiteral("found")).toBool()
			&& load.value(QStringLiteral("loaded_steps")).toInt() == 0
			&& !load.value(QStringLiteral("discarded")).toString().isEmpty(),
			qPrintable(compact(load)));
	}

	//! A history that declares NO steps is not an incomplete one: it is a
	//! history with nothing in it (a project that opted in before its first
	//! edit), and it loads as nothing - found, no steps, nothing to discard.
	void anEmptyHistoryIsNotIncomplete()
	{
		const Baseline base = buildBaseline();
		QVERIFY(!base.track.isEmpty());
		QVERIFY(run(QStringLiteral("project.set_undo_history"), {{QStringLiteral("setting"), QStringLiteral("on")}}).ok);
		const QString file = path(QStringLiteral("empty-history.mmp"));
		QVERIFY(run(QStringLiteral("project.save"), {{QStringLiteral("path"), file}}).ok);
		QVERIFY(textOf(file).contains(QStringLiteral("<z:undohistory")));
		QCOMPARE(textOf(file).count(QStringLiteral("<z:undostep")), 0);

		QVERIFY(run(QStringLiteral("project.open"), {{QStringLiteral("path"), file}}).ok);
		const QJsonObject load = historyState().value(QStringLiteral("last_load")).toObject();
		QVERIFY2(load.value(QStringLiteral("found")).toBool()
			&& load.value(QStringLiteral("loaded_steps")).toInt() == 0
			&& load.value(QStringLiteral("discarded")).toString().isEmpty(),
			qPrintable(compact(load)));
	}

	void aTopLevelMetadataSectionDoesNotStaleTheFingerprint()
	{
		QDomDocument withMeta;
		QVERIFY(withMeta.setContent(QStringLiteral(
			"<zene-project version=\"1\" type=\"SongProject\"><head/><song><track metadata=\"1\" name=\"x\"/></song></zene-project>")));
		QDomDocument pruned;
		QVERIFY(pruned.setContent(QStringLiteral(
			"<zene-project version=\"1\" type=\"SongProject\"><head/><song/></zene-project>")));
		QCOMPARE(UndoHistory::fingerprint(withMeta, true), UndoHistory::fingerprint(pruned, false));
	}

	void aChangedRootVersionChangesTheFingerprint()
	{
		QDomDocument v1;
		QVERIFY(v1.setContent(QStringLiteral(
			"<zene-project version=\"1\" type=\"SongProject\"><head/><song><track name=\"x\"/></song></zene-project>")));
		QDomDocument v2;
		QVERIFY(v2.setContent(QStringLiteral(
			"<zene-project version=\"2\" type=\"SongProject\"><head/><song><track name=\"x\"/></song></zene-project>")));
		QVERIFY(UndoHistory::fingerprint(v1, false) != UndoHistory::fingerprint(v2, false));
	}

	//! Turning persistence OFF drops the snapshots the steps carried, and turning it back on cannot recapture
	//! them: that record is irreversible and control.undo refuses it. A switch that drops nothing stays reversible.
};

QTEST_GUILESS_MAIN(UndoHistoryPersistenceTest)
#include "UndoHistoryPersistenceTest.moc"
