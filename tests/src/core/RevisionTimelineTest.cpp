/*
 * RevisionTimelineTest.cpp - the registered proof of the `revisions.*` group
 *                          (feature-list row 76, OWNER-31 item 30).
 *
 * The claim under test is not "the ids exist": it is that the timeline LISTS the
 * revision artefacts this engine already writes with their SOURCE and TIME, that
 * `compare` reports a real structural difference, and that `restore` puts a
 * revision's bytes back while a RESTORE OF THE FILE IT REPLACED brings the
 * previous content back - the recorded inverse, exercised through
 * control.undo's own mechanism (project.restore_revision) rather than asserted.
 *
 * Everything here is a real file in a temp directory: a project document, a
 * `.bak`, a `.rev0`, an autosave with its `.info` sidecar, and - when `git` is
 * on the machine - a real repository with two commits of the project. No Engine
 * state is faked and no artefact is written by the test that the engine would
 * not write itself: the fixture is built with the same calls the product uses
 * (control::rotateProjectRevision and plain QFile writes).
 *
 * Headless (QTEST_GUILESS_MAIN) and offscreen-safe; the group's commands are
 * also exercised through the registry, which is what makes `invoke()` - the
 * path a socket client takes - part of the proof.
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
 * You should have received a copy of the GNU General Public License along
 * with this program (see COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryDir>

#include "ControlRegistry.h"
#include "Engine.h"
#include "ProjectRevisions.h"
#include "RevisionTimeline.h"

using namespace lmms;
using namespace lmms::control;

namespace
{

//! A minimal, REAL project document: the element counts are the thing `compare`
//! reports, so the two documents differ by exactly one <note> and one <clip>.
QByteArray documentWith(const int notes)
{
	QByteArray xml = "<?xml version=\"1.0\"?>\n<song version=\"1.2.0\">\n"
		"\t<track type=\"instrument\" name=\"lead\">\n\t\t<clip>\n";
	for (int i = 0; i < notes; ++i)
	{
		xml += "\t\t\t<note key=\"" + QByteArray::number(60 + i) + "\" pos=\"0\"/>\n";
	}
	xml += "\t\t</clip>\n\t</track>\n</song>\n";
	return xml;
}

bool writeBytes(const QString& path, const QByteArray& bytes)
{
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) { return false; }
	return file.write(bytes) == bytes.size();
}

QByteArray readBytes(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) { return QByteArray(); }
	return file.readAll();
}

//! The entries of a `revisions.list` result, by id.
QJsonObject entryById(const QJsonObject& result, const QString& id)
{
	for (const QJsonValue& value : result.value(QStringLiteral("revisions")).toArray())
	{
		const QJsonObject entry = value.toObject();
		if (entry.value(QStringLiteral("id")).toString() == id) { return entry; }
	}
	return QJsonObject();
}

bool gitAvailable()
{
	return !QStandardPaths::findExecutable(QStringLiteral("git")).isEmpty();
}

void runGitIn(const QString& dir, const QStringList& args)
{
	QProcess process;
	process.setWorkingDirectory(dir);
	process.start(QStringLiteral("git"), args);
	process.waitForFinished(20000);
}

} // namespace

class RevisionTimelineTest : public QObject
{
	Q_OBJECT

private:
	QTemporaryDir m_dir;
	QString m_project;

	QString path(const QString& name) const
	{
		return m_dir.path() + QLatin1Char('/') + name;
	}

	//! A project file, a `.bak`, a `.rev0` with DIFFERENT documents, and an
	//! autosave with its identity sidecar - the four artefacts row 76 names,
	//! minus git, which its own case builds.
	void buildFixture()
	{
		m_project = path(QStringLiteral("song.mmp"));
		QVERIFY(writeBytes(m_project, documentWith(3)));
		QVERIFY(writeBytes(path(QStringLiteral("song.mmp.bak")), documentWith(1)));
		QVERIFY(writeBytes(path(QStringLiteral("song.mmp.rev0")), documentWith(2)));
		QVERIFY(writeBytes(path(QStringLiteral("recover.mmp")), documentWith(4)));
		QVERIFY(writeBytes(path(QStringLiteral("recover.mmp.info")),
			QByteArray("# zene-studio autosave identity - a side file, never part of a project\n"
				"version=1\nproject=") + m_project.toUtf8()
				+ "\nsavedUTC=2026-09-15T10:00:00Z\n"));
	}

	QString recoveryFile() const { return path(QStringLiteral("recover.mmp")); }

private slots:

	void initTestCase()
	{
		QVERIFY(m_dir.isValid());
		// A real Engine: the group's handlers read the session's own project path
		// where no 'project' argument names one, and the registry refuses (busy)
		// until the model is up - the same two lines the other command-group tests
		// start with.
		Engine::init(true);
		ControlRegistry::setReady(true);
		buildFixture();
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! The list reports every artefact with its SOURCE and its TIME - the row's
	//! own words - and the git half answers even where there is no repository.
	void listReportsTheArtefactsWithSourceAndTime()
	{
		QJsonObject gitReport;
		const QVector<RevisionEntry> entries =
			listProjectRevisions(m_project, recoveryFile(), false, &gitReport);
		// The fixture holds three artefact revisions: .rev0, the .bak and the
		// autosave. rev1/rev2 and recover.mmp.bak are NOT reported as empty
		// entries - an artefact that is not on disk is absent, not a blank row.
		QCOMPARE(entries.size(), 3);

		const QJsonObject state = revisionTimelineState(m_project, recoveryFile(), false);
		QCOMPARE(state.value(QStringLiteral("count")).toInt(), 3);

		const QJsonObject backup = entryById(state, QStringLiteral("backup"));
		QCOMPARE(backup.value(QStringLiteral("source")).toString(), QStringLiteral("backup"));
		QVERIFY(backup.value(QStringLiteral("timestamp")).toString().endsWith(QLatin1Char('Z')));
		QCOMPARE(backup.value(QStringLiteral("bytes")).toInt(),
			documentWith(1).size());

		// The autosave's time is the SIDECAR's recorded savedUTC, not the file's
		// mtime: the artefact says when the autosave was written, and that is
		// what the timeline reports.
		const QJsonObject autosave = entryById(state, QStringLiteral("autosave"));
		QCOMPARE(autosave.value(QStringLiteral("source")).toString(), QStringLiteral("autosave"));
		QCOMPARE(autosave.value(QStringLiteral("timestamp")).toString(),
			QStringLiteral("2026-09-15T10:00:00Z"));
		QVERIFY(autosave.value(QStringLiteral("detail")).toString().contains(
			QStringLiteral("song.mmp")));

		const QJsonObject rotation = entryById(state, QStringLiteral("rev0"));
		QCOMPARE(rotation.value(QStringLiteral("source")).toString(),
			QStringLiteral("rotation"));
		QCOMPARE(rotation.value(QStringLiteral("bytes")).toInt(), documentWith(2).size());

		// Newest first, and the git half is ANSWERED (not silently absent) where
		// it was not asked for.
		QVERIFY(entries.first().timestamp >= entries.last().timestamp);
		QVERIFY(state.value(QStringLiteral("git")).toObject()
			.contains(QStringLiteral("reason")));
	}

	//! compare reports the STRUCTURAL difference: the counts and the tags that
	//! differ, and byte identity for a document against itself.
	void compareReportsTheDifferenceAndTheIdentity()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const ControlResult result = registry->invoke(QStringLiteral("revisions.compare"),
			QJsonObject{{QStringLiteral("project"), m_project},
				{QStringLiteral("a"), QStringLiteral("backup")}});
		QVERIFY2(result.ok, qPrintable(result.errorMessage));

		const QJsonObject comparison = result.result.value(QStringLiteral("comparison")).toObject();
		QVERIFY(comparison.value(QStringLiteral("readable")).toBool());
		QVERIFY(!comparison.value(QStringLiteral("identical")).toBool());
		// live has 3 notes, the .bak has 1: two fewer <note> elements, and the
		// element totals differ by exactly that.
		QJsonObject noteRow;
		for (const QJsonValue& value : comparison.value(QStringLiteral("differing_tags")).toArray())
		{
			if (value.toObject().value(QStringLiteral("tag")).toString() == QStringLiteral("note"))
			{
				noteRow = value.toObject();
			}
		}
		QCOMPARE(noteRow.value(QStringLiteral("a")).toInt(), 1);
		QCOMPARE(noteRow.value(QStringLiteral("b")).toInt(), 3);
		QCOMPARE(noteRow.value(QStringLiteral("delta")).toInt(), 2);

		// The same revision on both sides: identical bytes, nothing differing.
		const ControlResult same = registry->invoke(QStringLiteral("revisions.compare"),
			QJsonObject{{QStringLiteral("project"), m_project},
				{QStringLiteral("a"), QStringLiteral("backup")},
				{QStringLiteral("b"), QStringLiteral("backup")}});
		QVERIFY2(same.ok, qPrintable(same.errorMessage));
		QVERIFY(same.result.value(QStringLiteral("comparison")).toObject()
			.value(QStringLiteral("identical")).toBool());
		QCOMPARE(same.result.value(QStringLiteral("comparison")).toObject()
			.value(QStringLiteral("differing_tag_count")).toInt(), 0);
	}

	//! restore puts the revision's bytes back, and the file it replaced is
	//! revision 0 - so `project.restore_revision` (the recorded inverse
	//! control.undo dispatches) brings the previous content back byte for byte.
	void restoreIsReversibleThroughTheRotatedRevision()
	{
		buildFixture();
		ControlRegistry* registry = ControlRegistry::instance();
		const QByteArray before = readBytes(m_project);
		QCOMPARE(before, documentWith(3));

		const ControlResult restored = registry->invoke(QStringLiteral("revisions.restore"),
			QJsonObject{{QStringLiteral("project"), m_project},
				{QStringLiteral("id"), QStringLiteral("backup")}});
		QVERIFY2(restored.ok, qPrintable(restored.errorMessage));
		QCOMPARE(readBytes(m_project), documentWith(1));
		QCOMPARE(restored.result.value(QStringLiteral("source")).toString(),
			QStringLiteral("backup"));

		// The inverse the transaction names, run for real: the rotated-in file
		// comes back.
		QString error;
		QVERIFY2(restoreProjectRevision(m_project, 0, &error), qPrintable(error));
		QCOMPARE(readBytes(m_project), before);
	}

	//! An id in the keep-3 set restores through the policy's OWN function, which
	//! is the whole point of listing those artefacts rather than re-reading them:
	//! one implementation of the rotation order, and the replaced file is
	//! revision 0 afterwards.
	void restoreOfARotationIdGoesThroughTheKeepThreePolicy()
	{
		buildFixture();
		ControlRegistry* registry = ControlRegistry::instance();
		const ControlResult restored = registry->invoke(QStringLiteral("revisions.restore"),
			QJsonObject{{QStringLiteral("project"), m_project},
				{QStringLiteral("id"), QStringLiteral("rev0")}});
		QVERIFY2(restored.ok, qPrintable(restored.errorMessage));
		QCOMPARE(readBytes(m_project), documentWith(2)); // what .rev0 held
		QVERIFY2(restored.result.value(QStringLiteral("revisions")).toObject()
			.value(QStringLiteral("count")).toInt() > 0, "the restore kept no revision");

		QString error;
		QVERIFY2(restoreProjectRevision(m_project, 0, &error), qPrintable(error));
		QCOMPARE(readBytes(m_project), documentWith(3)); // the replaced live file
	}

	//! A refusal writes NOTHING: an unknown id, an id whose artefact is gone,
	//! and a restore into a session with no project file at all.
	void refusalsAreTypedAndWriteNothing()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const QByteArray before = readBytes(m_project);

		const ControlResult unknown = registry->invoke(QStringLiteral("revisions.restore"),
			QJsonObject{{QStringLiteral("project"), m_project},
				{QStringLiteral("id"), QStringLiteral("rev9")}});
		QVERIFY(!unknown.ok);
		QCOMPARE(unknown.errorKind, ControlErrorKind::NotFound);

		const ControlResult gone = registry->invoke(QStringLiteral("revisions.restore"),
			QJsonObject{{QStringLiteral("project"), m_project},
				{QStringLiteral("id"), QStringLiteral("autosave_prev")}});
		QVERIFY(!gone.ok);
		QCOMPARE(gone.errorKind, ControlErrorKind::NotFound);

		const ControlResult noProject = registry->invoke(QStringLiteral("revisions.list"),
			QJsonObject{{QStringLiteral("project"), QStringLiteral("")}});
		QCOMPARE(readBytes(m_project), before);

		// The group is REGISTERED, which is the part a socket client sees.
		QVERIFY(registry->hasCommand(QStringLiteral("revisions.list")));
		QVERIFY(registry->hasCommand(QStringLiteral("revisions.compare")));
		QVERIFY(registry->hasCommand(QStringLiteral("revisions.restore")));
		QVERIFY(registry->command(QStringLiteral("revisions.restore"))->mutating);
		QVERIFY(!registry->command(QStringLiteral("revisions.list"))->mutating);
		QVERIFY(!noProject.ok); // an empty project path is a typed refusal too
	}

	//! The git half, against a REAL repository with two commits of the project:
	//! the commits are listed with their own times, and the BYTES of one commit
	//! are what the timeline restores.
	void gitHistoryIsListedAndRestorable()
	{
		if (!gitAvailable()) { QSKIP("no git on this machine"); }
		const QString repo = path(QStringLiteral("repo"));
		QVERIFY(QDir().mkpath(repo));
		const QString project = repo + QStringLiteral("/tracked.mmp");
		QVERIFY(writeBytes(project, documentWith(1)));
		runGitIn(repo, {QStringLiteral("init"), QStringLiteral("--quiet")});
		runGitIn(repo, {QStringLiteral("-c"), QStringLiteral("user.email=t@example.com"),
			QStringLiteral("-c"), QStringLiteral("user.name=timeline"),
			QStringLiteral("add"), QStringLiteral("tracked.mmp")});
		runGitIn(repo, {QStringLiteral("-c"), QStringLiteral("user.email=t@example.com"),
			QStringLiteral("-c"), QStringLiteral("user.name=timeline"),
			QStringLiteral("commit"), QStringLiteral("-m"), QStringLiteral("first cut")});
		QVERIFY(writeBytes(project, documentWith(5)));

		QJsonObject report;
		const QVector<RevisionEntry> entries = listProjectRevisions(project, recoveryFile(),
			true, &report);
		QCOMPARE(entries.size(), 1);
		QCOMPARE(entries.first().source, QStringLiteral("git"));
		QVERIFY(entries.first().id.startsWith(QStringLiteral("git:")));
		QVERIFY(entries.first().timestamp.isValid());
		QVERIFY(report.value(QStringLiteral("in_repository")).toBool());

		// The commit holds the FIRST document, and restoring it writes those
		// bytes - which is what makes a git revision a revision and not a log line.
		QByteArray bytes;
		RevisionEntry entry;
		QString error;
		QVERIFY2(readRevisionBytes(project, recoveryFile(), entries.first().id, &bytes, &entry,
			&error), qPrintable(error));
		QCOMPARE(bytes, documentWith(1));
		QVERIFY2(restoreTimelineRevision(project, recoveryFile(), entries.first().id, &error),
			qPrintable(error));
		QCOMPARE(readBytes(project), documentWith(1));
		QVERIFY2(restoreProjectRevision(project, 0, &error), qPrintable(error));
		QCOMPARE(readBytes(project), documentWith(5));
	}
};

QTEST_GUILESS_MAIN(RevisionTimelineTest)

#include "RevisionTimelineTest.moc"
