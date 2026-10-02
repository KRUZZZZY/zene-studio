/*
 * ProjectMarkersTest.cpp - the project's markers: commands, undo, jump, save and load
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

/*! ED-048 (0.5.0 L04): markers through marker.* - add, edit, remove, each one undo step; a bad
 *  colour and an unknown marker refused, typed; jump to the next marker; and the project file:
 *  <markers> round-trips, is written only when there is a marker, and a project without it loads
 *  with none (the reset-on-absence rule the ProjectSection registry gives every section). */

#include <QtTest>

#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>

#include "ControlRegistry.h"
#include "Engine.h"
#include "ProjectMarkers.h"
#include "Song.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

QString fileText(const QString& path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

} // namespace

class ProjectMarkersTest : public QObject
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

	void markersEditThroughTheCommandsOneUndoEach()
	{
		Engine::getSong()->clearProject();
		const ControlResult added = run(QStringLiteral("marker.add"),
			{{QStringLiteral("tick"), 192}, {QStringLiteral("name"), QStringLiteral("Verse")}, {QStringLiteral("color"), QStringLiteral("#ff0000")}});
		QVERIFY2(added.ok, qPrintable(added.errorMessage));
		const QString id = added.result.value(QStringLiteral("marker")).toString();
		QCOMPARE(ProjectMarkers::instance().markers().size(), 1);
		QVERIFY(run(QStringLiteral("marker.set"), {{QStringLiteral("marker"), id}, {QStringLiteral("name"), QStringLiteral("Chorus")}}).ok);
		QCOMPARE(ProjectMarkers::instance().markers().first().name, QStringLiteral("Chorus"));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(ProjectMarkers::instance().markers().first().name, QStringLiteral("Verse"));
		QVERIFY(run(QStringLiteral("marker.remove"), {{QStringLiteral("marker"), id}}).ok);
		QVERIFY(ProjectMarkers::instance().markers().isEmpty());
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(ProjectMarkers::instance().markers().size(), 1);

		QCOMPARE(run(QStringLiteral("marker.add"), {{QStringLiteral("color"), QStringLiteral("nope")}}).errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(run(QStringLiteral("marker.set"), {{QStringLiteral("marker"), QStringLiteral("mk-999")}}).errorKind, ControlErrorKind::NotFound);
	}

	void jumpGoesToTheNextMarker()
	{
		Song* song = Engine::getSong();
		QVERIFY(run(QStringLiteral("transport.seek"), {{QStringLiteral("ticks"), 0}}).ok);
		const ControlResult jumped = run(QStringLiteral("marker.jump"), {{QStringLiteral("direction"), QStringLiteral("next")}});
		QVERIFY2(jumped.ok, qPrintable(jumped.errorMessage));
		QCOMPARE(song->getPlayPos().getTicks(), 192);
		QCOMPARE(run(QStringLiteral("marker.jump"), {{QStringLiteral("direction"), QStringLiteral("next")}}).errorKind, ControlErrorKind::NotFound);
	}

	void markersRoundTripThroughTheProjectFile()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		Song* song = Engine::getSong();
		const QString bare = dir.filePath(QStringLiteral("bare.mmp"));
		song->clearProject();
		QVERIFY(song->saveProjectFile(bare));
		QVERIFY2(!fileText(bare).contains(QStringLiteral("<markers")), "a project with no marker wrote a <markers> element");

		QVERIFY(run(QStringLiteral("marker.add"), {{QStringLiteral("tick"), 384}, {QStringLiteral("name"), QStringLiteral("Drop")}}).ok);
		QVERIFY(run(QStringLiteral("marker.add"), {{QStringLiteral("tick"), 96}, {QStringLiteral("name"), QStringLiteral("Intro")},
			{QStringLiteral("color"), QStringLiteral("#00ff00")}}).ok);
		const QString marked = dir.filePath(QStringLiteral("marked.mmp"));
		QVERIFY(song->saveProjectFile(marked));

		song->loadProject(bare);
		QVERIFY2(ProjectMarkers::instance().markers().isEmpty(), "loading a project without markers kept the previous ones");
		song->loadProject(marked);
		const QVector<Marker>& loaded = ProjectMarkers::instance().markers();
		QCOMPARE(loaded.size(), 2);
		QCOMPARE(loaded.at(0).name, QStringLiteral("Intro"));
		QCOMPARE(loaded.at(0).tick, 96);
		QCOMPARE(loaded.at(0).color, QStringLiteral("#00ff00"));
		QCOMPARE(loaded.at(1).name, QStringLiteral("Drop"));
	}
};

QTEST_MAIN(ProjectMarkersTest)
#include "ProjectMarkersTest.moc"
