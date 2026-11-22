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

// Complete fixture method definitions; included once after the fixture declaration.

void RevisionTimelineTest::redoOfAProjectRestoreRevisionWhoseRotationBytesAreGoneFailsTyped() {
	buildFixture();
	ControlRegistry* registry = ControlRegistry::instance();
	const ControlResult restored = registry->invoke(QStringLiteral("project.restore_revision"),
		QJsonObject{{QStringLiteral("path"), m_project},
			{QStringLiteral("revision"), 0}});
	QVERIFY2(restored.ok, qPrintable(restored.errorMessage));
	QCOMPARE(readBytes(m_project), documentWith(2));

	const ControlResult undone = registry->invoke(QStringLiteral("control.undo"), QJsonObject());
	QVERIFY2(undone.ok, qPrintable(undone.errorMessage));
	QCOMPARE(readBytes(m_project), documentWith(3));

	// Take away every retained copy of the bytes this restore brought in, so
	// the redo cannot re-resolve them.
	for (int rev = 0; rev < 3; ++rev)
	{
		const QString revPath = projectRevisionPath(m_project, rev);
		if (QFileInfo::exists(revPath) && readBytes(revPath) == documentWith(2))
		{
			QVERIFY(QFile::remove(revPath));
		}
	}

	const ControlResult redo = registry->invoke(QStringLiteral("control.redo"), QJsonObject());
	QVERIFY2(!redo.ok, "control.redo answered 'redone: true' although the project file was NOT "
		"restored - the selected bytes are no longer retained");
	QCOMPARE(redo.errorKind, ControlErrorKind::Refused);
	QCOMPARE(readBytes(m_project), documentWith(3));
}
void RevisionTimelineTest::anUnsavedSessionListsNoArtefactsFromTheWorkingDirectory() {
	const QString unsaved = path(QStringLiteral("unsaved"));
	QVERIFY(QDir().mkpath(unsaved));
	QVERIFY(writeBytes(unsaved + QStringLiteral("/.bak"), documentWith(1)));
	QVERIFY(writeBytes(unsaved + QStringLiteral("/.rev0"), documentWith(2)));
	const QString unsavedAutosave = path(QStringLiteral("unsaved-recover.mmp"));
	QVERIFY(writeBytes(unsavedAutosave, documentWith(4)));
	QVERIFY(writeBytes(unsavedAutosave + QStringLiteral(".bak"), documentWith(5)));

	// Restore the working directory however the test ends, so no later test
	// inherits this one's.
	struct RestoreCurrentDirectory
	{
		QString previous;
		~RestoreCurrentDirectory() { QDir::setCurrent(previous); }
	} restore{QDir::currentPath()};
	QVERIFY(QDir::setCurrent(unsaved));

	QJsonObject gitReport;
	const QVector<RevisionEntry> entries =
		listProjectRevisions(QString(), unsavedAutosave, false, &gitReport);
	QCOMPARE(entries.size(), 2);
	QCOMPARE(revisionTimelineState(QString(), unsavedAutosave, false)
		.value(QStringLiteral("count")).toInt(), 2);
	QVERIFY(findRevision(QString(), recoveryFile(), QStringLiteral("rev0")).id.isEmpty());
	QVERIFY(findRevision(QString(), recoveryFile(), QStringLiteral("backup")).id.isEmpty());
	QCOMPARE(findRevision(QString(), unsavedAutosave, QStringLiteral("autosave")).id,
		QStringLiteral("autosave"));
	QCOMPARE(findRevision(QString(), unsavedAutosave, QStringLiteral("autosave_prev")).id,
		QStringLiteral("autosave_prev"));
}
void RevisionTimelineTest::anOversizedIdentitySidecarIsUnavailableNotRead() {
	const QString dir = path(QStringLiteral("oversized"));
	QVERIFY(QDir().mkpath(dir));
	const QString project = dir + QStringLiteral("/song.mmp");
	const QString recovery = dir + QStringLiteral("/recover.mmp");
	const QString other = dir + QStringLiteral("/other.mmp");
	QVERIFY(writeBytes(project, documentWith(3)));
	QVERIFY(writeBytes(recovery, documentWith(4)));

	const QByteArray sidecar = QByteArray("version=1\nproject=") + other.toUtf8()
		+ QByteArray("\nsavedUTC=2026-09-15T10:00:00Z\n#") + QByteArray(1024 * 1024, 'x')
		+ QByteArray("\n");
	QVERIFY(writeBytes(recovery + QStringLiteral(".info"), sidecar));

	const QJsonObject state = revisionTimelineState(project, recovery, false);
	const QJsonObject autosave = entryById(state, QStringLiteral("autosave"));
	QVERIFY2(!autosave.isEmpty(), "an oversized sidecar must not hide the autosave");
	QVERIFY(!autosave.value(QStringLiteral("detail")).toString()
		.contains(QStringLiteral("other.mmp")));
}
