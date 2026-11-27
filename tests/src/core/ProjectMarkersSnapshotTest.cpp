/*
 * ProjectMarkersSnapshotTest.cpp - an undo snapshot carries its own session's dropped-marker count
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

/*! These slots check that an undo snapshot carries its own session's dropped-marker count
 *  (ProjectMarkers::droppedOnLoad), so a restore gives back the refusal of the session it
 *  restores, not of the one it replaces. */

#include <QtTest>

#include <QFile>
#include <QTemporaryDir>

#include "ControlRegistry.h"
#include "DataFile.h"
#include "Engine.h"
#include "ProjectMarkers.h"
#include "Song.h"

using namespace lmms;

namespace
{

QString fileText(const QString& path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

//! A song project file named @a name in @a dir whose <markers> element is opened with
//! @a markersOpenTag and holds @a count markers M1..M<count>; its path, or an empty string.
QString writeMarkerProject(const QTemporaryDir& dir, const QString& name, int count,
	const QString& markersOpenTag = QStringLiteral("<markers>"))
{
	DataFile version(DataFile::Type::SongProject);
	QString text = QStringLiteral("<?xml version=\"1.0\"?>\n<lmms-project version=\"%1\" type=\"song\" creator=\"Zene Studio\">\n"
		"<head/><song><trackcontainer type=\"song\"><track type=\"0\" name=\"t\" muted=\"0\"><instrumenttrack/></track></trackcontainer>\n%2\n")
		.arg(version.documentElement().attribute(QStringLiteral("version")), markersOpenTag);
	for (int i = 0; i < count; ++i)
	{
		text += QStringLiteral("<marker id=\"%1\" tick=\"%2\" name=\"M%1\"/>\n").arg(i + 1).arg(i * 192);
	}
	text += QStringLiteral("</markers></song></lmms-project>\n");

	const QString path = dir.filePath(name);
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly)) { return QString(); }
	const QByteArray bytes = text.toUtf8();
	return file.write(bytes) == bytes.size() ? path : QString();
}

} // namespace

class ProjectMarkersSnapshotTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! A session checkpoint restores the DISPLACED session's document. Opening an
	//! over-cap file over a fitting session and undoing that open puts the fitting
	//! document back: the refusal belongs to the over-cap session that was
	//! replaced, not to the document restored, so the fitting session still saves.
	void anUndoOfAnOverCapOpenLeavesTheFittingSessionSavable()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString overCap = writeMarkerProject(dir, QStringLiteral("over-cap.mmp"), ProjectMarkers::MaxMarkers + 3);
		QVERIFY(!overCap.isEmpty());

		Song* song = Engine::getSong();
		song->clearProject();
		song->setLoadOnLaunch(false);
		QVERIFY(ProjectMarkers::instance().add(96, QStringLiteral("Fits"), QString()) > 0);
		const std::shared_ptr<const QByteArray> displaced = song->captureUndoSnapshot();
		QVERIFY(displaced && !displaced->isEmpty());

		song->loadProject(overCap);
		QCOMPARE(ProjectMarkers::instance().droppedOnLoad(), 3);

		QVERIFY2(song->restoreUndoSnapshot(*displaced, dir.filePath(QStringLiteral("b.mmp")), {}),
			"the displaced fitting session's snapshot did not restore");
		QCOMPARE(ProjectMarkers::instance().markers().size(), 1);
		QVERIFY2(ProjectMarkers::instance().droppedOnLoad() == 0,
			qPrintable(QStringLiteral("the restored fitting document inherited a dropped count of %1 from the replaced session")
				.arg(ProjectMarkers::instance().droppedOnLoad())));

		const QString target = dir.filePath(QStringLiteral("out.mmp"));
		QVERIFY2(song->saveProjectFile(target),
			qPrintable(QStringLiteral("the restored fitting session is wrongly read-only: %1").arg(song->saveRefusal())));
		QVERIFY(song->saveRefusal().isEmpty());
		QVERIFY(QFile::exists(target));

		song->clearProject();
	}

	//! The reverse: undoing a fitting session's load back to the over-cap
	//! session's snapshot restores a document that came from the file with more
	//! markers, so the refusal must come back with it, or the fitting session's
	//! save would be followed by a save of the reduced 512-marker list.
	void restoringAnOverCapSnapshotOverAFittingSessionKeepsTheRefusal()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString path = writeMarkerProject(dir, QStringLiteral("over-cap.mmp"), ProjectMarkers::MaxMarkers + 3);
		QVERIFY(!path.isEmpty());

		Song* song = Engine::getSong();
		song->clearProject();
		song->setLoadOnLaunch(false);
		song->loadProject(path);
		QCOMPARE(ProjectMarkers::instance().droppedOnLoad(), 3);
		const QString before = fileText(path);
		const std::shared_ptr<const QByteArray> overCap = song->captureUndoSnapshot();
		QVERIFY(overCap && !overCap->isEmpty());

		song->clearProject();
		QVERIFY(ProjectMarkers::instance().add(96, QStringLiteral("Fits"), QString()) > 0);
		QVERIFY2(song->saveProjectFile(dir.filePath(QStringLiteral("fits.mmp"))), qPrintable(song->saveRefusal()));

		QVERIFY2(song->restoreUndoSnapshot(*overCap, path, {}), "the over-cap session's snapshot did not restore");
		QCOMPARE(ProjectMarkers::instance().markers().size(), ProjectMarkers::MaxMarkers);
		QVERIFY2(ProjectMarkers::instance().droppedOnLoad() == 3,
			qPrintable(QStringLiteral("the restored over-cap document lost its dropped count: %1")
				.arg(ProjectMarkers::instance().droppedOnLoad())));

		const QString target = dir.filePath(QStringLiteral("out.mmp"));
		QVERIFY2(!song->saveProjectFile(target),
			"the restored over-cap document saved its reduced marker list");
		QVERIFY2(song->saveRefusal().contains(QStringLiteral("marker"), Qt::CaseInsensitive),
			qPrintable(QStringLiteral("the refusal does not name the markers: %1").arg(song->saveRefusal())));
		QVERIFY2(!QFile::exists(target), "the refused save left a file behind");

		QVERIFY2(!song->saveProjectFile(path), "the restored over-cap document was saved over the original file");
		QCOMPARE(fileText(path), before);

		song->clearProject();
	}

	//! The count is not the list length: a session that dropped markers on load
	//! and then had every remaining marker deleted holds none, and the snapshot
	//! of it must still carry the refusal.
	void anOverCapSessionEmptiedOfMarkersKeepsTheRefusalThroughASnapshot()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString path = writeMarkerProject(dir, QStringLiteral("over-cap.mmp"), ProjectMarkers::MaxMarkers + 3);
		QVERIFY(!path.isEmpty());

		Song* song = Engine::getSong();
		song->clearProject();
		song->setLoadOnLaunch(false);
		song->loadProject(path);
		const QString before = fileText(path);

		ProjectMarkers::instance().restore({});
		QCOMPARE(ProjectMarkers::instance().markers().size(), 0);
		QCOMPARE(ProjectMarkers::instance().droppedOnLoad(), 3);
		const std::shared_ptr<const QByteArray> snapshot = song->captureUndoSnapshot();
		QVERIFY(snapshot && !snapshot->isEmpty());

		song->clearProject();
		QVERIFY(ProjectMarkers::instance().add(96, QStringLiteral("Fits"), QString()) > 0);
		QVERIFY2(song->restoreUndoSnapshot(*snapshot, path, {}), "the emptied over-cap session's snapshot did not restore");
		QCOMPARE(ProjectMarkers::instance().markers().size(), 0);
		QVERIFY2(ProjectMarkers::instance().droppedOnLoad() == 3,
			qPrintable(QStringLiteral("the restored emptied session lost its dropped count: %1")
				.arg(ProjectMarkers::instance().droppedOnLoad())));

		QVERIFY2(!song->saveProjectFile(path), "the emptied over-cap session was saved over the original file");
		QCOMPARE(fileText(path), before);

		song->clearProject();
	}

	//! Collateral guard for the snapshot-only count: a SAVED file can never claim
	//! markers it did not drop. Only an undo snapshot carries droppedOnLoad, and
	//! only while a snapshot is being restored; a project file with the attribute
	//! loads as the plain list it holds.
	void aSavedFileCannotClaimMarkersItDidNotDrop()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString path = writeMarkerProject(dir, QStringLiteral("claims.mmp"), 1,
			QStringLiteral("<markers droppedOnLoad=\"3\">"));
		QVERIFY(!path.isEmpty());

		Song* song = Engine::getSong();
		song->clearProject();
		song->setLoadOnLaunch(false);
		song->loadProject(path);
		QCOMPARE(ProjectMarkers::instance().markers().size(), 1);
		QVERIFY2(ProjectMarkers::instance().droppedOnLoad() == 0,
			qPrintable(QStringLiteral("a project file's droppedOnLoad attribute was believed: %1")
				.arg(ProjectMarkers::instance().droppedOnLoad())));

		const QString target = dir.filePath(QStringLiteral("out.mmp"));
		QVERIFY2(song->saveProjectFile(target), qPrintable(song->saveRefusal()));
		QVERIFY(song->saveRefusal().isEmpty());

		song->clearProject();
	}
};

QTEST_MAIN(ProjectMarkersSnapshotTest)
#include "ProjectMarkersSnapshotTest.moc"
