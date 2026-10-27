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

#include "ControlProjectAssets.h"

#include <QCoreApplication>
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
#include "ProjectContainer.h"
#include "ProjectRevisions.h"
#include "RevisionTimeline.h"
#include "ReversibilityTestSupport.h"

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

QByteArray documentWithTagCounts(const int tags, const int countPerTag)
{
	QByteArray xml = "<?xml version=\"1.0\"?>\n<song>\n";
	for (int i = 0; i < tags; ++i)
	{
		const QByteArray tag = "tag" + QByteArray::number(i).rightJustified(3, '0');
		for (int count = 0; count < countPerTag; ++count)
		{
			xml += '<';
			xml += tag;
			xml += "/>\n";
		}
	}
	xml += "</song>\n";
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

//! A command dispatched the way a socket client dispatches it.
ControlResult run(const QString& id, const QJsonObject& args = QJsonObject())
{
	return ControlRegistry::instance()->invoke(id, args);
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

	//! The result stays capped while the total counts every difference, and the
	//! helper constructs JSON entries only after it knows they fit the cap.
	void comparisonBoundsDifferenceConstruction()
	{
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

	//! The private union helper uses set membership so a document with many
	//! distinct tags does not scan the growing result list for every tag.
	void comparisonTagUnionUsesSetMembership()
	{
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

	//! A `.mmpz` v2 CONTAINER is refused rather than counted. This slot is the
	//! other half of the one above, and the two are the reason the guard has to
	//! come BEFORE the `<?xml` probe rather than after it: a v2 container is a
	//! STORE ZIP - DawProjectZip.cpp's header records that it does not compress - so its
	//! bytes contain a LITERAL `<?xml` and the probe matches them. Until the guard
	//! landed, the archive was handed to the tag counter as if it were a document
	//! and the comparison reported `readable: true` over the ZIP's own headers:
	//! not a refusal with a bad message, but a confident meaningless answer.
	void aV2ContainerIsRefusedRatherThanCountedAsADocument()
	{
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

	//! Luna cluster 5236: a shift copy that fails does NOT take the destination
	//! backup with it, and the rotation says it was refused. copyRevision()
	//! removed the destination BEFORE copying, so a failed shift destroyed an
	//! older backup while rotation continued as if it had succeeded.
	void aFailedShiftKeepsTheBackupAndReportsARefusal()
	{
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

	//! Luna cluster 4352: a replace that cannot complete leaves the LIVE project
	//! exactly as it was. replaceProjectWithStaged() removed the live file before
	//! renaming the staged one into place, and its recovery copies revision 0
	//! back - so a failure at the moment the disk is full, the one moment the
	//! recovery copy also cannot succeed, left no project file at all.
	void aFailedReplaceKeepsTheLiveProject()
	{
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

	//! The replacement fails AFTER its staged copy is complete: the live project keeps its bytes and the
	//! staging file survives. Before the rework the live file was removed first, and revision 0 is made
	//! unreadable here, so the old recovery copy could not put it back and the failure emptied the path.
	void aFailedReplacementAfterStagingKeepsTheLiveProject()
	{
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

	//! A file that is not ours, named the way the old pending copy was named, is not deleted by a restore.
	void aReplaceLeavesAnUnrelatedReplaceFileAlone()
	{
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

	//! A replacement that fails AFTER its staged copy is complete leaves the slot it would have replaced
	//! exactly as it was, and the rotation reports the refusal. Before the rework the slot was removed
	//! first, so this failure emptied it.
	void aFailedReplacementAfterStagingKeepsTheSlot()
	{
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

	//! A file that is not ours, named the way the old staging file was named, is not deleted by a rotation.
	void aRotationLeavesAnUnrelatedRotateFileAlone()
	{
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

	//! A stale slot that cannot be removed is reported, rather than a successful
	//! rotation silently leaving a revision outside the keep-3 set.
	void rotationReportsFailureToRemoveStaleRevision()
	{
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

	//! Luna cluster 5737: a restore that CANNOT find its revision records no undo
	//! step. The handler registered the checkpoint before handing the request to
	//! restoreProjectRevision(), the call that can refuse it, so asking for a
	//! revision that is not there failed the command and still pushed a step whose
	//! undo "restores" a file the command never touched. The caller's next
	//! control.undo then spent itself on that phantom, and the step before it moved
	//! one further from the keyboard.
	void aFailedRestoreRecordsNoUndoStep()
	{
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

	//! Luna cluster 5738: a redo restores the BYTES the restore selected, not the
	//! revision NUMBER it named. Every restore rotates the live file in, so the
	//! numbers shift under the recorded redo: after the undo, revision 1 held a
	//! different file and the redo put THAT over the live one, which left the
	//! project holding the pre-restore bytes under a redo that reported success.
	void aRedoRestoresTheBytesTheRestoreSelected()
	{
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

	void aRefusedRestoreRecordsNoUndoStep()
	{
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

	//! Luna cluster 4878: the redo of a revisions.restore re-resolved the captured
	//! revision ID. 'rev1' is a POSITION in the keep-3 set, and the restore's own
	//! rotation plus the undo's rotation each shift every position, so by redo time
	//! the id named different bytes and the redo wrote those over the live file
	//! under a success it had not earned.
	void theTimelineRedoRestoresTheBytesTheRestoreSelected()
	{
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

	//! Luna cluster 1223: the transaction revisions.restore records advertises an
	//! inverse that can neither run nor mean anything. It named
	//! `revisions.restore` again with the SAME id, which reapplies the chosen
	//! revision instead of putting the replaced file back, and it passed the target
	//! as `path` where the command's args schema takes `project` - so a client
	//! replaying it either failed argument validation or targeted the session's own
	//! project instead of the requested file. The advertised inverse must be
	//! replayable AND be the undo.
	void theTimelineRestoreAdvertisesAReplayableInverse()
	{
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

	//! project.save must not advertise restore_revision when its stale slot makes
	//! that inverse fail before it can replace the saved file.
	void saveDoesNotAdvertiseAnInverseAfterStaleCleanupRefusal()
	{
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

	//! UNDO whose FILE OPERATION fails must not answer success. The recorded step
	//! restores revision 0 - the file the restore rotated in - and when that
	//! artefact is gone nothing is restored: `undone: true` would tell a caller
	//! the pre-restore project is on disk when it is not.
	void undoOfARestoreThatCannotPutTheFileBackFailsTyped()
	{
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

	//! REDO, the other half of the same recorded pair: it re-restores the revision
	//! the call chose, from that revision's own artefact, and a vanished artefact
	//! fails the file operation the same way.
	void redoOfARestoreWhoseRevisionIsGoneFailsTyped()
	{
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

	//! When revisions.restore created the live file, undo removes it instead of
	//! restoring a prior revision. A failed removal must still refuse undo.
	void undoOfRestoreCreatedFileThatCannotBeRemovedFailsTyped()
	{
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

	//! BUG 9 (cluster new-p15-27): a dangling symlink written at the path after the
	//! restore created the file is a NEWER entry, not the bytes the restore wrote,
	//! so undo must leave it alone. QFileInfo::exists() follows the link and reports
	//! false, so the check looks at the link itself.
	void undoOfRestoreCreatedDanglingSymlinkLeavesTheNewerEntry()
	{
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


	//! REDO of a rotation restore whose selected bytes are no longer retained must
	//! not answer success: the redo leaves the live file unchanged, and reporting
	//! `redone: true` would claim a restore that did not happen.
	void redoOfARestoreWhoseRotationBytesAreGoneFailsTyped()
	{
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

	//! The SIBLING path into the same restore helper: `project.restore_revision`
	//! records its OWN undo step, and that step's file operation can fail the
	//! same way. `undone: true` over a project file that was NOT put back is the
	//! same lie, so the sibling must report it the same way.
	void undoOfAProjectRestoreRevisionThatCannotPutTheFileBackFailsTyped()
	{
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

	//! The sibling's REDO half: it re-resolves the bytes the restore brought in
	//! and re-restores them. When those bytes are no longer retained the redo
	//! restores NOTHING - and must say so instead of answering 'redone: true'.
	void redoOfAProjectRestoreRevisionWhoseRotationBytesAreGoneFailsTyped()
	{
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


	//! An unsaved session has no project-file revisions. Its rotation and backup
	//! paths are built FROM the empty project path, which would name `.rev0` and
	//! `.bak` in the working directory, while its session autosave remains valid.
	void anUnsavedSessionListsNoArtefactsFromTheWorkingDirectory()
	{
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
};

QTEST_GUILESS_MAIN(RevisionTimelineTest)

#include "RevisionTimelineTest.moc"
