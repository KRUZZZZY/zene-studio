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

void RevisionTimelineTest::refusalsAreTypedAndWriteNothing() {
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
void RevisionTimelineTest::anEmptyRevisionIsRefusedAndTheLiveProjectKept() {
	buildFixture();
	QVERIFY(writeBytes(path(QStringLiteral("song.mmp.bak")), QByteArray()));
	QVERIFY(writeBytes(recoveryFile(), QByteArray()));
	QVERIFY(writeBytes(projectRevisionPath(m_project, 0), QByteArray()));
	const QByteArray live = readBytes(m_project);
	const QStringList before = QDir(m_dir.path()).entryList(QDir::Files, QDir::Name);

	ControlRegistry* registry = ControlRegistry::instance();
	const QStringList ids{QStringLiteral("backup"), QStringLiteral("autosave"),
		QStringLiteral("rev0")};
	for (const QString& id : ids)
	{
		const ControlResult restored = registry->invoke(QStringLiteral("revisions.restore"),
			QJsonObject{{QStringLiteral("project"), m_project},
				{QStringLiteral("id"), id}});
		QVERIFY2(!restored.ok, qPrintable(QStringLiteral("restoring the empty '%1' succeeded").arg(id)));
		QCOMPARE(restored.errorKind, ControlErrorKind::Refused);
		QVERIFY2(restored.errorMessage.contains(QStringLiteral("empty")),
			qPrintable(restored.errorMessage));
		QCOMPARE(readBytes(m_project), live);
	}

	// The direct policy call the undo path also takes refuses the same way.
	QString error;
	QVERIFY(!restoreProjectRevision(m_project, 0, &error));
	QVERIFY2(error.contains(QStringLiteral("empty")), qPrintable(error));
	QCOMPARE(readBytes(m_project), live);

	// Nothing was rotated, staged or removed.
	QCOMPARE(QDir(m_dir.path()).entryList(QDir::Files, QDir::Name), before);
	buildFixture();
}
void RevisionTimelineTest::gitHistoryIsListedAndRestorable() {
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
	// ONE entry, and the number is load-bearing: the fixture's autosave
	// (recover.mmp) is passed as the recovery file to consider, but its
	// sidecar names the OTHER project, so it is not a revision of this one.
	// Measured: before the timeline read the sidecar's identity it listed two
	// entries and presented a recovery of another project as this project's.
	QCOMPARE(entries.size(), 1);
	QCOMPARE(entries.first().source, QStringLiteral("git"));
	QVERIFY(entries.first().id.startsWith(QStringLiteral("git:")));
	QVERIFY(entries.first().timestamp.isValid());
	// The size is MEASURED (one `git cat-file --batch-check` for the list),
	// not reported as zero: a caller reading "0 bytes" would take a real
	// revision for an empty one. Measured is what makes this assertable.
	QCOMPARE(entries.first().bytes, static_cast<qint64>(documentWith(1).size()));
	QVERIFY(report.value(QStringLiteral("in_repository")).toBool());
	QCOMPARE(report.value(QStringLiteral("sizes_measured")).toInt(), 1);

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
void RevisionTimelineTest::aFailedRestoreRecordsNoUndoStep() {
	// Its OWN file: the shared fixture's revisions are rotated by the cases
	// above, so revision 2 would exist there.
	const QString project = path(QStringLiteral("phantom.mmp"));
	QVERIFY(writeBytes(project, documentWith(3)));
	QVERIFY(writeBytes(project + QStringLiteral(".rev0"), documentWith(2)));
	const QByteArray before = readBytes(project);

	const ControlResult depth = run(QStringLiteral("control.undo_depth"));
	QVERIFY(depth.ok);
	const int beforeDepth = depth.result.value(QStringLiteral("depth")).toInt();

	// The fixture holds revision 0 and NO revision 1: a typed miss.
	const ControlResult missing = run(QStringLiteral("project.restore_revision"),
		QJsonObject{{QStringLiteral("path"), project}, {QStringLiteral("revision"), 1}});
	QVERIFY2(!missing.ok, "a restore of a revision that does not exist reported success");
	QCOMPARE(missing.errorKind, ControlErrorKind::NotFound);

	const ControlResult after = run(QStringLiteral("control.undo_depth"));
	QVERIFY(after.ok);
	QCOMPARE(after.result.value(QStringLiteral("depth")).toInt(), beforeDepth);
	QCOMPARE(readBytes(project), before);
}
void RevisionTimelineTest::aRedoRestoresTheBytesTheRestoreSelected() {
	const QString project = path(QStringLiteral("redo.mmp"));
	QVERIFY(writeBytes(project, documentWith(3)));
	QVERIFY(writeBytes(project + QStringLiteral(".rev0"), documentWith(2)));
	QVERIFY(writeBytes(project + QStringLiteral(".rev1"), documentWith(1)));

	const ControlResult restored = run(QStringLiteral("project.restore_revision"),
		QJsonObject{{QStringLiteral("path"), project}, {QStringLiteral("revision"), 1}});
	QVERIFY2(restored.ok, qPrintable(restored.errorMessage));
	QCOMPARE(readBytes(project), documentWith(1));

	// The undo is the displaced live file, which the rotation kept as revision 0.
	const ControlResult undone = run(QStringLiteral("control.undo"));
	QVERIFY2(undone.ok, qPrintable(undone.errorMessage));
	QCOMPARE(readBytes(project), documentWith(3));

	// The redo must bring back what the restore brought in - the SELECTED
	// document, which the undo's rotation left somewhere in the retained set.
	const ControlResult redone = run(QStringLiteral("control.redo"));
	QVERIFY2(redone.ok, qPrintable(redone.errorMessage));
	QCOMPARE(readBytes(project), documentWith(1));
}
void RevisionTimelineTest::aRefusedRestoreRecordsNoUndoStep() {
	// Its OWN file. The live file is over MaxRevisionBytes, which is the
	// refusal the rotation raises; the revision being restored stays UNDER the
	// cap, so the refusal is the live file's and not the source's.
	const QString project = path(QStringLiteral("oversize.mmp"));
	QVERIFY(writeBytes(project, QByteArray(
		static_cast<int>(ProjectRevisionPolicy::MaxRevisionBytes) + 1024, 'x')));
	QVERIFY(writeBytes(project + QStringLiteral(".bak"), documentWith(1)));
	const QByteArray before = readBytes(project);

	const ControlResult depth = run(QStringLiteral("control.undo_depth"));
	QVERIFY(depth.ok);
	const int beforeDepth = depth.result.value(QStringLiteral("depth")).toInt();

	const ControlResult refused = run(QStringLiteral("revisions.restore"),
		QJsonObject{{QStringLiteral("project"), project},
			{QStringLiteral("id"), QStringLiteral("backup")}});
	QVERIFY2(!refused.ok, "a restore over the live file's cap reported success");
	QCOMPARE(refused.errorKind, ControlErrorKind::Refused);

	const ControlResult after = run(QStringLiteral("control.undo_depth"));
	QVERIFY(after.ok);
	QCOMPARE(after.result.value(QStringLiteral("depth")).toInt(), beforeDepth);
	QCOMPARE(readBytes(project), before);
}
void RevisionTimelineTest::theTimelineRedoRestoresTheBytesTheRestoreSelected() {
	const QString project = path(QStringLiteral("timeline-redo.mmp"));
	QVERIFY(writeBytes(project, documentWith(3)));
	QVERIFY(writeBytes(project + QStringLiteral(".rev0"), documentWith(2)));
	QVERIFY(writeBytes(project + QStringLiteral(".rev1"), documentWith(1)));

	const ControlResult restored = run(QStringLiteral("revisions.restore"),
		QJsonObject{{QStringLiteral("project"), project},
			{QStringLiteral("id"), QStringLiteral("rev1")}});
	QVERIFY2(restored.ok, qPrintable(restored.errorMessage));
	QCOMPARE(readBytes(project), documentWith(1));

	const ControlResult undone = run(QStringLiteral("control.undo"));
	QVERIFY2(undone.ok, qPrintable(undone.errorMessage));
	QCOMPARE(readBytes(project), documentWith(3));

	// The redo must bring back what the restore brought in.
	const ControlResult redone = run(QStringLiteral("control.redo"));
	QVERIFY2(redone.ok, qPrintable(redone.errorMessage));
	QCOMPARE(readBytes(project), documentWith(1));
}
void RevisionTimelineTest::theTimelineRestoreAdvertisesAReplayableInverse() {
	const QString project = path(QStringLiteral("inverse.mmp"));
	QVERIFY(writeBytes(project, documentWith(3)));
	QVERIFY(writeBytes(project + QStringLiteral(".rev0"), documentWith(2)));

	const ControlResult restored = run(QStringLiteral("revisions.restore"),
		QJsonObject{{QStringLiteral("project"), project},
			{QStringLiteral("id"), QStringLiteral("rev0")}});
	QVERIFY2(restored.ok, qPrintable(restored.errorMessage));
	QCOMPARE(readBytes(project), documentWith(2));

	const QJsonObject inverse = revtest::stateOf(QStringLiteral("revisions.restore"))
		.value(QStringLiteral("inverse")).toObject();
	QVERIFY2(!inverse.isEmpty(), "revisions.restore advertised no inverse at all");
	// The advertised args have to satisfy the advertised op's own schema.
	const QJsonObject args = inverse.value(QStringLiteral("args")).toObject();
	QVERIFY2(args.value(QStringLiteral("path")).isUndefined(),
		"the advertised inverse still passes 'path', which revisions.restore does "
		"not accept");
	// And replaying what it advertises must actually put the replaced file back.
	QVERIFY2(run(inverse.value(QStringLiteral("op")).toString(), args).ok,
		"the advertised inverse of revisions.restore was refused when replayed");
	QCOMPARE(readBytes(project), documentWith(3));
}
void RevisionTimelineTest::saveDoesNotAdvertiseAnInverseAfterStaleCleanupRefusal() {
	const QString project = path(QStringLiteral("unprunable-save.mmp"));
	const QString stale = projectRevisionPath(project, ProjectRevisionPolicy::Keep);
	QVERIFY(writeBytes(project, documentWith(2)));
	QVERIFY(QDir().mkpath(stale));
	QVERIFY(writeBytes(stale + QStringLiteral("/locked"), QByteArray("keep")));

	const ControlResult saved = ControlRegistry::instance()->invoke(
		QStringLiteral("project.save"), QJsonObject{{QStringLiteral("path"), project}});
	QVERIFY2(saved.ok, qPrintable(saved.errorMessage));
	QVERIFY(saved.result.value(QStringLiteral("revision_kept")).toBool());
	QVERIFY(saved.result.value(QStringLiteral("revision_skipped")).toString().contains(stale));

	ControlRegistry* registry = ControlRegistry::instance();
	const ControlResult history = registry->invoke(QStringLiteral("control.transactions"));
	QVERIFY2(history.ok, qPrintable(history.errorMessage));
	const QJsonArray transactions = history.result.value(QStringLiteral("transactions")).toArray();
	QVERIFY(!transactions.isEmpty());
	const QJsonObject transaction = transactions.last().toObject();
	QCOMPARE(transaction.value(QStringLiteral("command")).toString(), QStringLiteral("project.save"));
	QCOMPARE(transaction.value(QStringLiteral("before")).toObject()
		.value(QStringLiteral("file")).toString(), project);
	QVERIFY(!transaction.value(QStringLiteral("reversible")).toBool());
	QVERIFY(transaction.value(QStringLiteral("inverse")).toObject().isEmpty());
	QCOMPARE(transaction.value(QStringLiteral("before")).toObject()
		.value(QStringLiteral("previous_revision_replaced")).toInt(), 0);

	const QByteArray savedBytes = readBytes(project);
	const ControlResult undo = registry->invoke(QStringLiteral("control.undo"));
	QVERIFY(!undo.ok);
	QCOMPARE(undo.errorKind, ControlErrorKind::Irreversible);
	QCOMPARE(readBytes(project), savedBytes);
}
void RevisionTimelineTest::undoOfARestoreThatCannotPutTheFileBackFailsTyped() {
	buildFixture();
	ControlRegistry* registry = ControlRegistry::instance();
	const ControlResult restored = registry->invoke(QStringLiteral("revisions.restore"),
		QJsonObject{{QStringLiteral("project"), m_project},
			{QStringLiteral("id"), QStringLiteral("backup")}});
	QVERIFY2(restored.ok, qPrintable(restored.errorMessage));
	QCOMPARE(readBytes(m_project), documentWith(1));

	// Take away the very artefact the recorded inverse reads.
	QVERIFY(QFile::remove(projectRevisionPath(m_project, 0)));

	const ControlResult undo = registry->invoke(QStringLiteral("control.undo"), QJsonObject());
	QVERIFY2(!undo.ok, "control.undo answered 'undone: true' although the project file was NOT "
		"restored - the step's own revision 0 was gone");
	QCOMPARE(undo.errorKind, ControlErrorKind::Refused);
	QVERIFY2(undo.errorMessage.contains(QStringLiteral("revisions.restore")),
		qPrintable(undo.errorMessage));
	QVERIFY2(!undo.errorMessage.contains(QStringLiteral("irreversible")),
		qPrintable(undo.errorMessage));
	QCOMPARE(readBytes(m_project), documentWith(1));
}
void RevisionTimelineTest::redoOfARestoreWhoseRevisionIsGoneFailsTyped() {
	buildFixture();
	ControlRegistry* registry = ControlRegistry::instance();
	const ControlResult restored = registry->invoke(QStringLiteral("revisions.restore"),
		QJsonObject{{QStringLiteral("project"), m_project},
			{QStringLiteral("id"), QStringLiteral("backup")}});
	QVERIFY2(restored.ok, qPrintable(restored.errorMessage));
	const ControlResult undone = registry->invoke(QStringLiteral("control.undo"), QJsonObject());
	QVERIFY2(undone.ok, qPrintable(undone.errorMessage));
	QCOMPARE(readBytes(m_project), documentWith(3));

	QVERIFY(QFile::remove(path(QStringLiteral("song.mmp.bak"))));

	const ControlResult redo = registry->invoke(QStringLiteral("control.redo"), QJsonObject());
	QVERIFY2(!redo.ok, "control.redo answered 'redone: true' although the project file was NOT "
		"restored - the revision this call chose is gone");
	QCOMPARE(redo.errorKind, ControlErrorKind::Refused);
	QCOMPARE(readBytes(m_project), documentWith(3));
}
void RevisionTimelineTest::undoOfRestoreCreatedFileThatCannotBeRemovedFailsTyped() {
	buildFixture();
	const QString createdProject = path(QStringLiteral("created-song.mmp"));
	QVERIFY(writeBytes(createdProject, documentWith(3)));
	QVERIFY(writeBytes(createdProject + QStringLiteral(".bak"), documentWith(1)));
	QVERIFY(QFile::remove(createdProject));
	ControlRegistry* registry = ControlRegistry::instance();
	const ControlResult restored = registry->invoke(QStringLiteral("revisions.restore"),
		QJsonObject{{QStringLiteral("project"), createdProject},
			{QStringLiteral("id"), QStringLiteral("backup")}});
	QVERIFY2(restored.ok, qPrintable(restored.errorMessage));
	QCOMPARE(readBytes(createdProject), documentWith(1));

	QVERIFY(QFile::remove(createdProject));
	QVERIFY(QDir().mkdir(createdProject));

	const ControlResult undo = registry->invoke(QStringLiteral("control.undo"), QJsonObject());
	QVERIFY2(!undo.ok, "control.undo answered success although the restore-created path "
		"could not be removed");
	QCOMPARE(undo.errorKind, ControlErrorKind::Refused);
	QVERIFY2(undo.errorMessage.contains(QStringLiteral("revisions.restore")),
		qPrintable(undo.errorMessage));
	QVERIFY(QFileInfo(createdProject).isDir());
}
void RevisionTimelineTest::undoOfRestoreCreatedDanglingSymlinkLeavesTheNewerEntry() {
	buildFixture();
	QString projectName = QStringLiteral("dangling-song.mmp");
#ifdef Q_OS_WIN
	projectName += QStringLiteral(".lnk");
#endif
	const QString createdProject = path(projectName);
	QVERIFY(writeBytes(createdProject, documentWith(3)));
	QVERIFY(writeBytes(createdProject + QStringLiteral(".bak"), documentWith(1)));
	QVERIFY(QFile::remove(createdProject));
	ControlRegistry* registry = ControlRegistry::instance();
	const ControlResult restored = registry->invoke(QStringLiteral("revisions.restore"),
		QJsonObject{{QStringLiteral("project"), createdProject},
			{QStringLiteral("id"), QStringLiteral("backup")}});
	QVERIFY2(restored.ok, qPrintable(restored.errorMessage));
	QCOMPARE(readBytes(createdProject), documentWith(1));

	QVERIFY(QFile::remove(createdProject));
	QVERIFY2(QFile::link(path(QStringLiteral("missing-project-target.mmp")), createdProject),
		"could not replace the restore-created file with a dangling symlink");
	QVERIFY(QFileInfo(createdProject).isSymLink());
	QVERIFY(!QFileInfo(createdProject).exists());

	const ControlResult undo = registry->invoke(QStringLiteral("control.undo"), QJsonObject());
	QVERIFY2(!undo.ok, "control.undo answered success and removed a symlink written after the "
		"restore - the file the restore created was no longer at the path");
	QCOMPARE(undo.errorKind, ControlErrorKind::Refused);
	QVERIFY2(undo.errorMessage.contains(QStringLiteral("revisions.restore")),
		qPrintable(undo.errorMessage));
	QVERIFY(QFileInfo(createdProject).isSymLink());
}
void RevisionTimelineTest::redoOfARestoreWhoseRotationBytesAreGoneFailsTyped() {
	buildFixture();
	ControlRegistry* registry = ControlRegistry::instance();
	const ControlResult restored = registry->invoke(QStringLiteral("revisions.restore"),
		QJsonObject{{QStringLiteral("project"), m_project},
			{QStringLiteral("id"), QStringLiteral("rev0")}});
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
void RevisionTimelineTest::undoOfAProjectRestoreRevisionThatCannotPutTheFileBackFailsTyped() {
	buildFixture();
	ControlRegistry* registry = ControlRegistry::instance();
	const ControlResult restored = registry->invoke(QStringLiteral("project.restore_revision"),
		QJsonObject{{QStringLiteral("path"), m_project},
			{QStringLiteral("revision"), 0}});
	QVERIFY2(restored.ok, qPrintable(restored.errorMessage));
	QCOMPARE(readBytes(m_project), documentWith(2));

	// Take away the very artefact the recorded inverse reads: revision 0 is
	// the file the restore rotated in, and the undo restores exactly that.
	QVERIFY(QFile::remove(projectRevisionPath(m_project, 0)));

	const ControlResult undo = registry->invoke(QStringLiteral("control.undo"), QJsonObject());
	QVERIFY2(!undo.ok, "control.undo answered 'undone: true' although the project file was NOT "
		"restored - the sibling step's own revision 0 was gone");
	QCOMPARE(undo.errorKind, ControlErrorKind::Refused);
	QVERIFY2(undo.errorMessage.contains(QStringLiteral("project.restore_revision")),
		qPrintable(undo.errorMessage));
	QVERIFY2(!undo.errorMessage.contains(QStringLiteral("irreversible")),
		qPrintable(undo.errorMessage));
	QCOMPARE(readBytes(m_project), documentWith(2));
}
