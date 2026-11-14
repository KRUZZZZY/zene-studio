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

void RevisionTimelineTest::initTestCase() {
	QVERIFY(m_dir.isValid());
	// Point the HANDLERS at this test's own recovery path too: without the
	// override they read the machine's real recover.mmp.bak and the
	// 'autosave_prev' refusal case resolves an artefact the kit never wrote
	// (measured: the handler opened Documents/Zene Studio/recover.mmp.bak).
	qputenv("ZENE_RECOVERY_FILE", path(QStringLiteral("recover.mmp")).toUtf8());
	// A real Engine: the group's handlers read the session's own project path
	// where no 'project' argument names one, and the registry refuses (busy)
	// until the model is up - the same two lines the other command-group tests
	// start with.
	Engine::init(true);
	ControlRegistry::setReady(true);
	buildFixture();
}
void RevisionTimelineTest::cleanupTestCase() {
	ControlRegistry::setReady(false);
	Engine::destroy();
}
void RevisionTimelineTest::listReportsTheArtefactsWithSourceAndTime() {
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
	// The hash is what a caller compares two revisions by without reading them,
	// so the list carries one for every artefact it can read (measured: the
	// first version of the list reported an empty sha256, which no reader of
	// the description could have told from a hash of nothing).
	QCOMPARE(backup.value(QStringLiteral("sha256")).toString().size(), 64);

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
void RevisionTimelineTest::compareReportsTheDifferenceAndTheIdentity() {
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

	// A `.mmpz` container is compared AFTER decompression: the bytes of a
	// compressed document are not the bytes of the text it holds, so the two
	// are not "identical" - but the structure is the same, and a comparison
	// that reported a compressed revision as unreadable would be useless
	// exactly where the artefact carries a compressed project.
	QVERIFY(writeBytes(path(QStringLiteral("song.mmp.rev1")), qCompress(documentWith(3))));
	const ControlResult container = registry->invoke(QStringLiteral("revisions.compare"),
		QJsonObject{{QStringLiteral("project"), m_project},
			{QStringLiteral("a"), QStringLiteral("rev1")},
			{QStringLiteral("b"), QStringLiteral("live")}});
	QVERIFY2(container.ok, qPrintable(container.errorMessage));
	const QJsonObject compressed = container.result.value(QStringLiteral("comparison")).toObject();
	QVERIFY(compressed.value(QStringLiteral("readable")).toBool());
	QCOMPARE(compressed.value(QStringLiteral("differing_tag_count")).toInt(), 0);
	QVERIFY(!compressed.value(QStringLiteral("identical")).toBool());
}
void RevisionTimelineTest::comparisonBoundsDifferenceConstruction() {
	const QJsonObject comparison = compareRevisionDocuments(
		documentWithTagCounts(32, 1), documentWithTagCounts(32, 2));
	QVERIFY(comparison.value(QStringLiteral("readable")).toBool());
	QCOMPARE(comparison.value(QStringLiteral("differing_tag_count")).toInt(), 32);
	const QJsonArray reported = comparison.value(QStringLiteral("differing_tags")).toArray();
	QCOMPARE(reported.size(), RevisionTimelineBounds::MaxComparedTags);
	QCOMPARE(reported.first().toObject().value(QStringLiteral("tag")).toString(),
		QStringLiteral("tag000"));
	QCOMPARE(reported.last().toObject().value(QStringLiteral("tag")).toString(),
		QStringLiteral("tag023"));

	const QString sourcePath = QDir(QCoreApplication::applicationDirPath()).filePath(
		QStringLiteral("../../src/core/RevisionTimelineCompare.cpp"));
	const QByteArray sourceBytes = readBytes(sourcePath);
	QVERIFY2(!sourceBytes.isEmpty(), qPrintable(sourcePath));
	const QString source = QString::fromUtf8(sourceBytes);
	const int helperStart = source.indexOf(QStringLiteral("QJsonArray differingTags("));
	QVERIFY(helperStart >= 0);
	const int bodyStart = source.indexOf(QLatin1Char('{'), helperStart);
	const int bodyEnd = source.indexOf(QStringLiteral("\n}"), bodyStart);
	QVERIFY(bodyStart >= 0 && bodyEnd > bodyStart);
	const QString helper = source.mid(bodyStart, bodyEnd - bodyStart);
	const int bound = helper.indexOf(
		QStringLiteral("reported.size() < Bounds::MaxComparedTags"));
	const int entry = helper.indexOf(QStringLiteral("QJsonObject"));
	const int append = helper.indexOf(QStringLiteral("reported.append"));
	const int guardEnd = helper.indexOf(QStringLiteral("\n\t\t}"), bound);
	QVERIFY(bound >= 0);
	QVERIFY(guardEnd > bound);
	QVERIFY(entry > bound && entry < guardEnd);
	QVERIFY(append > bound && append < guardEnd);
	QVERIFY(!helper.contains(QStringLiteral("differing.append")));
}
void RevisionTimelineTest::comparisonTagUnionUsesSetMembership() {
	const QJsonObject comparison = compareRevisionDocuments(
		documentWithTagCounts(2048, 1), documentWithTagCounts(2048, 2));
	QVERIFY(comparison.value(QStringLiteral("readable")).toBool());
	QCOMPARE(comparison.value(QStringLiteral("differing_tag_count")).toInt(), 2048);
	const QJsonArray reported = comparison.value(QStringLiteral("differing_tags")).toArray();
	QCOMPARE(reported.size(), RevisionTimelineBounds::MaxComparedTags);
	QCOMPARE(reported.first().toObject().value(QStringLiteral("tag")).toString(),
		QStringLiteral("tag000"));
	QCOMPARE(reported.last().toObject().value(QStringLiteral("tag")).toString(),
		QStringLiteral("tag023"));

	// Timing a quadratic scan is flaky; pin its deterministic algorithmic
	// contract directly, as the neighbouring construction-bound test does.
	const QString sourcePath = QDir(QCoreApplication::applicationDirPath()).filePath(
		QStringLiteral("../../src/core/RevisionTimelineCompare.cpp"));
	const QByteArray sourceBytes = readBytes(sourcePath);
	QVERIFY2(!sourceBytes.isEmpty(), qPrintable(sourcePath));
	const QString source = QString::fromUtf8(sourceBytes);
	const int helperStart = source.indexOf(QStringLiteral("QStringList unionOfTags("));
	QVERIFY(helperStart >= 0);
	const int bodyStart = source.indexOf(QLatin1Char('{'), helperStart);
	const int bodyEnd = source.indexOf(QStringLiteral("\n}"), bodyStart);
	QVERIFY(bodyStart >= 0 && bodyEnd > bodyStart);
	const QString helper = source.mid(bodyStart, bodyEnd - bodyStart);
	QVERIFY(helper.contains(QStringLiteral("QSet<QString> seen")));
	QVERIFY(helper.contains(QStringLiteral("seen.contains(")));
	QVERIFY(helper.contains(QStringLiteral("seen.insert(")));
	QVERIFY(!helper.contains(QStringLiteral("tags.contains(tag)")));
}
void RevisionTimelineTest::aV2ContainerIsRefusedRatherThanCountedAsADocument() {
	// One document, three container shapes, so only the container differs.
	const QByteArray xml = documentWith(3);

	QString error;
	const std::vector<std::pair<QString, QByteArray>> sections{
		{ QStringLiteral("sections/0000-track"), QByteArray("<track/>") } };
	QVERIFY2(projectcontainer::writeContainer(path(QStringLiteral("v2.mmpz")), xml,
		sections, &error), qPrintable(error));
	const QByteArray container = readBytes(path(QStringLiteral("v2.mmpz")));

	// The measurement the guard exists for. If a container ever stops holding
	// its skeleton verbatim this slot stops testing the guard, so it is
	// asserted rather than assumed.
	QVERIFY(projectcontainer::isContainer(container));
	QVERIFY2(container.contains("<?xml"),
		"a STORE container holds its skeleton verbatim; the guard below is "
		"only load-bearing while that is true");

	// The fix, at the unit level.
	QVERIFY(projectDocumentText(container).isEmpty());

	// The two shapes the guard must NOT catch, so a guard that refuses
	// everything cannot pass this slot: an `.mmp`'s own text, and the v1 blob
	// the slot above pins as readable.
	QCOMPARE(projectDocumentText(xml), xml);
	QCOMPARE(projectDocumentText(qCompress(xml)), xml);

	// A compressed revision whose header lies small, over the 64 MiB document cap: refused while it
	// inflates, so the expanded text is never held in full. revisions.compare reaches this path.
	QByteArray lying = qCompress(QByteArray(64 * 1024 * 1024 + 1, '\0'));
	lying[0] = 0;
	lying[1] = 0;
	lying[2] = 0x04;
	lying[3] = 0;  // declares 1 KiB
	QVERIFY(projectDocumentText(lying).isEmpty());
	// The bound itself, at a small cap: over it refused, within it inflated exactly.
	const QByteArray five = qCompress(QByteArray(5000, '\0'));
	QByteArray out;
	QVERIFY(!lmms::control::inflateQtCompressedWithinCap(five, 1000, &out));
	QVERIFY(out.isEmpty());
	QVERIFY(lmms::control::inflateQtCompressedWithinCap(five, 10000, &out));
	QCOMPARE(out.size(), 5000);

	// End to end through the registry, which is the path a socket client
	// takes: a revision that IS a v2 container is reported not readable.
	QVERIFY(writeBytes(path(QStringLiteral("song.mmp.rev2")), container));
	ControlRegistry* registry = ControlRegistry::instance();
	const ControlResult result = registry->invoke(QStringLiteral("revisions.compare"),
		QJsonObject{{QStringLiteral("project"), m_project},
			{QStringLiteral("a"), QStringLiteral("rev2")},
			{QStringLiteral("b"), QStringLiteral("live")}});
	QVERIFY2(result.ok, qPrintable(result.errorMessage));
	const QJsonObject comparison = result.result.value(QStringLiteral("comparison")).toObject();
	QVERIFY(!comparison.value(QStringLiteral("readable")).toBool());
	QCOMPARE(comparison.value(QStringLiteral("differing_tag_count")).toInt(), 0);
	QCOMPARE(comparison.value(QStringLiteral("differing_tags")).toArray().size(), 0);
}
void RevisionTimelineTest::restoreIsReversibleThroughTheRotatedRevision() {
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
void RevisionTimelineTest::aFailedShiftKeepsTheBackupAndReportsARefusal() {
	buildFixture();
	// rev0 is the source the rev1 -> rev2 shift reads; make it unreadable, so
	// the copy fails on a file the destination cannot afford to lose.
	const QString rev0 = projectRevisionPath(m_project, 0);
	const QString rev1 = projectRevisionPath(m_project, 1);
	QVERIFY(QFile::remove(rev1));
	QVERIFY(writeBytes(rev1, documentWith(7)));
	QVERIFY(QFile::setPermissions(rev0, QFile::Permissions()));

	const QByteArray keptBackup = readBytes(rev1);
	bool refused = false;
	QString reason;
	const int kept = rotateProjectRevision(m_project, &refused, &reason);

	// The destination backup survived the failed copy...
	QVERIFY2(QFileInfo::exists(rev1), "the failed shift deleted the destination backup");
	QCOMPARE(readBytes(rev1), keptBackup);
	// ... and rotation reported the refusal rather than claiming success.
	QVERIFY2(refused, "a failed shift copy was reported as a successful rotation");
	QVERIFY(!reason.isEmpty());
	QCOMPARE(kept, 0);

	// Restore the permission so the temp dir can clean up.
	QVERIFY(QFile::setPermissions(rev0, QFile::ReadOwner | QFile::WriteOwner));
}
void RevisionTimelineTest::aFailedReplaceKeepsTheLiveProject() {
	buildFixture();
	const QByteArray live = readBytes(m_project);
	QCOMPARE(live, documentWith(3));
	const QString staging = projectRevisionPath(m_project, 0) + QStringLiteral(".restore");

	// No revision 0 to fall back on, and a staging file that is not there to
	// move: the replacement cannot complete. This is the shape of the
	// reported failure - the copy into the final name is what fails, which is
	// what a full destination does - and here the recovery copy has nothing
	// to recover from either.
	QVERIFY(QFile::remove(projectRevisionPath(m_project, 0)));
	QVERIFY(!QFileInfo::exists(staging));
	QVERIFY(QFileInfo::exists(m_project));

	QVERIFY2(!replaceProjectWithStaged(m_project, staging),
		"the replace reported success");

	QVERIFY2(QFileInfo::exists(m_project), "the failed replace left no project file at all");
	QCOMPARE(readBytes(m_project), live);
}
void RevisionTimelineTest::aFailedReplacementAfterStagingKeepsTheLiveProject() {
	buildFixture();
	const QByteArray live = readBytes(m_project);
	const QString rev0 = projectRevisionPath(m_project, 0);
	const QString staging = rev0 + QStringLiteral(".restore");
	QVERIFY(writeBytes(staging, documentWith(5)));
	// Restores the readable mode however this test leaves, so a failure cannot cascade into the next fixture.
	struct ReadableAgain { QString path; ~ReadableAgain() { QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner); } };
	const ReadableAgain restore{rev0};
	QVERIFY(QFile::setPermissions(rev0, QFile::Permissions()));

	setRevisionReplacementFailureForTest(true);
	const bool replaced = replaceProjectWithStaged(m_project, staging);
	setRevisionReplacementFailureForTest(false);

	QVERIFY2(!replaced, "a failed replacement was reported as success");
	QVERIFY2(QFileInfo::exists(m_project), "the failed replacement emptied the live project");
	QCOMPARE(readBytes(m_project), live);
	QVERIFY(QFileInfo::exists(staging));
}
void RevisionTimelineTest::aReplaceLeavesAnUnrelatedReplaceFileAlone() {
	buildFixture();
	const QString staging = projectRevisionPath(m_project, 0) + QStringLiteral(".restore");
	QVERIFY(writeBytes(staging, documentWith(5)));
	const QString stray = m_project + QStringLiteral(".replace");
	QVERIFY(writeBytes(stray, documentWith(8)));
	const QByteArray before = readBytes(stray);

	QVERIFY2(replaceProjectWithStaged(m_project, staging), "the replacement failed");
	QVERIFY2(QFileInfo::exists(stray), "a replace deleted a file that was not its own");
	QCOMPARE(readBytes(stray), before);
	QCOMPARE(readBytes(m_project), documentWith(5));
	QVERIFY(!QFileInfo::exists(staging));
}
void RevisionTimelineTest::aFailedReplacementAfterStagingKeepsTheSlot() {
	buildFixture();
	const QString rev1 = projectRevisionPath(m_project, 1);
	const QString rev2 = projectRevisionPath(m_project, 2);
	QVERIFY(writeBytes(rev1, documentWith(6)));
	QVERIFY(writeBytes(rev2, documentWith(9)));
	const QByteArray keptBackup = readBytes(rev2);

	setRevisionReplacementFailureForTest(true);
	bool refused = false;
	QString reason;
	const int kept = rotateProjectRevision(m_project, &refused, &reason);
	setRevisionReplacementFailureForTest(false);

	QVERIFY2(QFileInfo::exists(rev2), "the failed replacement emptied the slot it would have replaced");
	QCOMPARE(readBytes(rev2), keptBackup);
	QVERIFY2(refused, "a failed replacement was reported as a successful rotation");
	QVERIFY(!reason.isEmpty());
	QCOMPARE(kept, 0);
	// The staging file this call created is not left behind.
	QCOMPARE(QDir(m_dir.path()).entryList({QStringLiteral("*.rotate-*")}).size(), 0);
}
void RevisionTimelineTest::aRotationLeavesAnUnrelatedRotateFileAlone() {
	buildFixture();
	const QString stray = projectRevisionPath(m_project, 0) + QStringLiteral(".rotate");
	QVERIFY(writeBytes(stray, documentWith(5)));
	const QByteArray before = readBytes(stray);

	bool refused = false;
	QString reason;
	rotateProjectRevision(m_project, &refused, &reason);

	QVERIFY2(QFileInfo::exists(stray), "a rotation deleted a file that was not its own");
	QCOMPARE(readBytes(stray), before);
}
void RevisionTimelineTest::restoreOfARotationIdGoesThroughTheKeepThreePolicy() {
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
void RevisionTimelineTest::rotationReportsFailureToRemoveStaleRevision() {
	const QString project = path(QStringLiteral("unprunable.mmp"));
	const QString stale = projectRevisionPath(project, ProjectRevisionPolicy::Keep);
	QVERIFY(writeBytes(project, documentWith(2)));
	QVERIFY(QDir().mkpath(stale));
	QVERIFY(writeBytes(stale + QStringLiteral("/locked"), QByteArray("keep")));

	bool refused = false;
	QString reason;
	const int kept = rotateProjectRevision(project, &refused, &reason);

	QVERIFY(QFileInfo::exists(stale));
	QVERIFY2(refused, qPrintable(QStringLiteral("rotation did not report the failed removal: %1")
		.arg(reason)));
	QVERIFY(reason.contains(stale));
	QCOMPARE(kept, 1); // revision 0 was still retained for undo
}
