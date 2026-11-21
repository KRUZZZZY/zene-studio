/*
 * UndoHistoryOptionsTest.cpp - WF-033: the project's OWN undo-history options:
 *                              whether the history is persisted at all (the
 *                              opt-in), how many steps and how many bytes it
 *                              may take, and what turning it off costs.
 *
 * The other half of the feature's proof lives in UndoHistoryPersistenceTest.cpp
 * (the file format: what a load accepts and what it discards) and the fixtures
 * both halves build are in UndoHistoryTestSupport.h, so there is ONE definition
 * of "a project with a history".
 *
 *   1. offByDefaultAndAnOldProjectRoundTripsUnchanged
 *        persistence is opt-in; a project that did not opt in writes no section
 *        and a project without one loads and re-saves the same document.
 *   2. theCapsDropTheOldestAndAnOversizeStepIsReported
 *        the step limit and the byte cap drop the OLDEST steps; a single snapshot
 *        over the cap saves nothing from that step back and the save says so.
 *   3. aSettingThatDropsSnapshotsIsNotReversible
 *        turning persistence OFF drops the snapshots the steps carried, and
 *        turning it back on cannot recapture them.
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <QtTest>

#include <QFile>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextStream>

#include "ConfigManager.h"
#include "UndoHistoryTestSupport.h"

using namespace lmms;
using namespace undotest;

class UndoHistoryOptionsTest : public QObject
{
	Q_OBJECT

private:
	QTemporaryDir m_dir;

	QString path(const QString& name) const { return m_dir.filePath(name); }

	//! The baseline: an instrument track with a real instrument, an automation clip linked to its
	//! first parameter, and a sample track with a clip on a real WAV. Built, then the journal is
	//! cleared, so none of it is a step: the steps are the edits each test makes.
	Baseline buildBaseline()
	{
		return undotest::buildBaseline(m_dir.path());
	}

private slots:
	void initTestCase()
	{
		QVERIFY(m_dir.isValid());
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

	void offByDefaultAndAnOldProjectRoundTripsUnchanged()
	{
		const Baseline base = buildBaseline();
		QVERIFY2(!base.track.isEmpty(), "no instrument track");
		const QJsonObject state = historyState();
		QVERIFY2(state.value(QStringLiteral("default_enabled")) == QJsonValue(false), qPrintable(compact(state)));
		QVERIFY2(state.value(QStringLiteral("enabled")) == QJsonValue(false), qPrintable(compact(state)));

		QVERIFY(run(QStringLiteral("track.rename"), {{QStringLiteral("track"), base.track}, {QStringLiteral("name"), QStringLiteral("edited")}}).ok);
		const QString old = path(QStringLiteral("old.mmp"));
		const ControlResult saved = run(QStringLiteral("project.save"), {{QStringLiteral("path"), old}});
		QVERIFY2(saved.ok, qPrintable(saved.errorMessage));
		const QJsonObject report = saved.result.value(QStringLiteral("undo_history")).toObject();
		QVERIFY2(report.value(QStringLiteral("enabled")) == QJsonValue(false)
			&& report.value(QStringLiteral("written")) == QJsonValue(false), qPrintable(compact(saved.result)));
		QVERIFY2(!textOf(old).contains(QStringLiteral("undohistory")), "a project that did not opt in carries a history section");

		// The project without a history loads and re-saves as the same document, with no section.
		QVERIFY(run(QStringLiteral("project.open"), {{QStringLiteral("path"), old}}).ok);
		QCOMPARE(historyState().value(QStringLiteral("last_load")).toObject().value(QStringLiteral("found")).toBool(true), false);
		const QString again = path(QStringLiteral("old-again.mmp"));
		QVERIFY(run(QStringLiteral("project.save"), {{QStringLiteral("path"), again}}).ok);
		QVERIFY(!textOf(again).contains(QStringLiteral("undohistory")));
		QCOMPARE(contentOf(again), contentOf(old));
	}

	void theCapsDropTheOldestAndAnOversizeStepIsReported()
	{
		const Baseline base = buildBaseline();
		QVERIFY(!base.track.isEmpty());
		QVERIFY(run(QStringLiteral("project.set_undo_history"), {{QStringLiteral("setting"), QStringLiteral("on")}}).ok);
		QVERIFY(run(QStringLiteral("settings.set"), {{QStringLiteral("key"), QStringLiteral("undohistory/steps")}, {QStringLiteral("value"), QStringLiteral("2")}}).ok);
		for (const QString& name : {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"), QStringLiteral("d")})
		{
			QVERIFY(run(QStringLiteral("track.rename"), {{QStringLiteral("track"), base.track}, {QStringLiteral("name"), name}}).ok);
		}
		// settings.set is an undo step of its own, so the stack is that step and the four renames.
		const int depth = journalDepth();
		QVERIFY(depth >= 4);
		ControlResult saved = run(QStringLiteral("project.save"), {{QStringLiteral("path"), path(QStringLiteral("limit.mmp"))}});
		QVERIFY2(saved.ok, qPrintable(saved.errorMessage));
		QJsonObject report = saved.result.value(QStringLiteral("undo_history")).toObject();
		QVERIFY2(report.value(QStringLiteral("saved_steps")).toInt() == 2 && report.value(QStringLiteral("dropped_oldest")).toInt() == depth - 2
			&& !report.value(QStringLiteral("oversize")).toBool(), qPrintable(compact(report)));
		QCOMPARE(textOf(path(QStringLiteral("limit.mmp"))).count(QStringLiteral("<z:undostep")), 2);

		// A project whose compressed snapshot is over a 1 MB cap: ~2 MB of random text in a track
		// name (base64 of random bytes compresses to about three quarters of itself).
		QVERIFY(run(QStringLiteral("settings.set"), {{QStringLiteral("key"), QStringLiteral("undohistory/capmb")}, {QStringLiteral("value"), QStringLiteral("1")}}).ok);
		QVERIFY(run(QStringLiteral("track.rename"), {{QStringLiteral("track"), base.track}, {QStringLiteral("name"), noise(1500 * 1024)}}).ok);
		QVERIFY(run(QStringLiteral("track.rename"), {{QStringLiteral("track"), base.track}, {QStringLiteral("name"), QStringLiteral("small again")}}).ok);
		const QString big = path(QStringLiteral("oversize.mmp"));
		saved = run(QStringLiteral("project.save"), {{QStringLiteral("path"), big}});
		QVERIFY2(saved.ok, qPrintable(saved.errorMessage));
		report = saved.result.value(QStringLiteral("undo_history")).toObject();
		// The newest step's pre-state carries the 2 MB name: over the cap alone, so nothing from it back is saved.
		QVERIFY2(report.value(QStringLiteral("oversize")).toBool() && report.value(QStringLiteral("saved_steps")).toInt() == 0
			&& report.value(QStringLiteral("oversize_bytes")).toDouble() > 1024.0 * 1024.0
			&& !report.value(QStringLiteral("warning")).toString().isEmpty()
			&& !report.value(QStringLiteral("written")).toBool(), qPrintable(compact(report)));
		QCOMPARE(textOf(big).count(QStringLiteral("<z:undostep")), 0);
		QVERIFY(textOf(big).contains(QStringLiteral("<z:undohistory")));  // the project's own choice still saved
	}

	//! Turning persistence OFF drops the snapshots the steps carried, and turning it back on cannot recapture
	//! them: that record is irreversible and control.undo refuses it. A switch that drops nothing stays reversible.
	void aSettingThatDropsSnapshotsIsNotReversible()
	{
		const Baseline base = buildBaseline();
		QVERIFY(!base.track.isEmpty());
		// Persistence was never on, so no step carries a snapshot: nothing to drop, still reversible.
		const ControlResult quiet = run(QStringLiteral("project.set_undo_history"), {{QStringLiteral("setting"), QStringLiteral("off")}});
		QVERIFY2(quiet.ok, qPrintable(quiet.errorMessage));
		QVERIFY2(lastTransaction().value(QStringLiteral("reversible")).toBool(), qPrintable(compact(lastTransaction())));

		QVERIFY(run(QStringLiteral("project.set_undo_history"), {{QStringLiteral("setting"), QStringLiteral("on")}}).ok);
		QVERIFY(run(QStringLiteral("track.rename"), {{QStringLiteral("track"), base.track}, {QStringLiteral("name"), QStringLiteral("one")}}).ok);
		QVERIFY(run(QStringLiteral("track.rename"), {{QStringLiteral("track"), base.track}, {QStringLiteral("name"), QStringLiteral("two")}}).ok);

		// Turning it off now drops the snapshots of the two renames: the record must not claim a full inverse.
		const ControlResult off = run(QStringLiteral("project.set_undo_history"), {{QStringLiteral("setting"), QStringLiteral("off")}});
		QVERIFY2(off.ok, qPrintable(off.errorMessage));
		const QJsonObject record = lastTransaction();
		QCOMPARE(record.value(QStringLiteral("command")).toString(), QStringLiteral("project.set_undo_history"));
		QVERIFY2(!record.value(QStringLiteral("reversible")).toBool(), qPrintable(compact(record)));
		const ControlResult undo = run(QStringLiteral("control.undo"));
		QCOMPARE(undo.errorKind, ControlErrorKind::Irreversible);
		QCOMPARE(run(QStringLiteral("project.undo_history")).result.value(QStringLiteral("setting")).toString(), QStringLiteral("off"));
	}
};

QTEST_GUILESS_MAIN(UndoHistoryOptionsTest)
#include "UndoHistoryOptionsTest.moc"
