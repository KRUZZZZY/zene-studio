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
#include <QJsonObject>
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

//! The wire id "mk-<n>" back to the number ProjectMarkers knows a marker by.
int idMarker(const QString& id)
{
	bool ok = false;
	const int number = id.mid(3).toInt(&ok);
	return ok ? number : -1;
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

	//! WF-073: an action carries ITS OWN arguments. Pointing a marker at a
	//! different command without saying what to pass it must not leave the
	//! previous command's arguments behind - they would be handed to a command
	//! that never asked for them.
	void changingAnActionDoesNotKeepThePreviousArguments()
	{
		Engine::getSong()->clearProject();
		const ControlResult added = run(QStringLiteral("marker.add"), {{QStringLiteral("tick"), 192}});
		QVERIFY2(added.ok, qPrintable(added.errorMessage));
		const QString id = added.result.value(QStringLiteral("marker")).toString();
		QVERIFY(run(QStringLiteral("marker.set"), {{QStringLiteral("marker"), id},
			{QStringLiteral("action"), QStringLiteral("transport.set_tempo")},
			{QStringLiteral("args"), QJsonObject{{QStringLiteral("bpm"), 120}}}}).ok);
		QCOMPARE(ProjectMarkers::instance().find(idMarker(id))->args, QStringLiteral("{\"bpm\":120}"));

		// A different action and no arguments of its own: the old ones go.
		QVERIFY(run(QStringLiteral("marker.set"), {{QStringLiteral("marker"), id},
			{QStringLiteral("action"), QStringLiteral("transport.play")}}).ok);
		const Marker* changed = ProjectMarkers::instance().find(idMarker(id));
		QVERIFY(changed != nullptr);
		QCOMPARE(changed->action, QStringLiteral("transport.play"));
		QVERIFY2(changed->args.isEmpty(),
			qPrintable(QStringLiteral("the marker kept %1 for an action that never asked for it")
				.arg(changed->args)));

		// Naming the SAME action again is not a change, so its arguments stay.
		QVERIFY(run(QStringLiteral("marker.set"), {{QStringLiteral("marker"), id},
			{QStringLiteral("action"), QStringLiteral("transport.set_tempo")},
			{QStringLiteral("args"), QJsonObject{{QStringLiteral("bpm"), 120}}}}).ok);
		QVERIFY(run(QStringLiteral("marker.set"), {{QStringLiteral("marker"), id},
			{QStringLiteral("tick"), 384}}).ok);
		QCOMPARE(ProjectMarkers::instance().find(idMarker(id))->args, QStringLiteral("{\"bpm\":120}"));

		// One undo step per edit, as everywhere else on this surface: four
		// edits went in, so four undos come back out to the marker as it was.
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(ProjectMarkers::instance().find(idMarker(id))->tick, 192);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(ProjectMarkers::instance().find(idMarker(id))->action, QStringLiteral("transport.play"));
		QVERIFY(ProjectMarkers::instance().find(idMarker(id))->args.isEmpty());
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(ProjectMarkers::instance().find(idMarker(id))->action, QStringLiteral("transport.set_tempo"));
		QCOMPARE(ProjectMarkers::instance().find(idMarker(id))->args, QStringLiteral("{\"bpm\":120}"));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY2(ProjectMarkers::instance().find(idMarker(id))->action.isEmpty(),
			"four undos did not take the marker back to no action at all");
	}

	//! A marker action is stored so that the CROSSING can run it, so its
	//! arguments have to satisfy the command it names - refused here, where the
	//! mistake is, rather than on every crossing of the marker in playback.
	void aMarkerActionCannotCarryArgumentsItsCommandWouldRefuse()
	{
		Engine::getSong()->clearProject();
		const ControlResult added = run(QStringLiteral("marker.add"), {{QStringLiteral("tick"), 192}});
		QVERIFY2(added.ok, qPrintable(added.errorMessage));
		const QString id = added.result.value(QStringLiteral("marker")).toString();

		// transport.set_tempo needs a bpm: without one, and with one outside
		// its own range, the stored pair is one the crossing would refuse.
		QCOMPARE(run(QStringLiteral("marker.set"), {{QStringLiteral("marker"), id},
			{QStringLiteral("action"), QStringLiteral("transport.set_tempo")}}).errorKind,
			ControlErrorKind::InvalidArgs);
		QCOMPARE(run(QStringLiteral("marker.set"), {{QStringLiteral("marker"), id},
			{QStringLiteral("action"), QStringLiteral("transport.set_tempo")},
			{QStringLiteral("args"), QJsonObject{{QStringLiteral("bpm"), 5000}}}}).errorKind,
			ControlErrorKind::InvalidArgs);
		QVERIFY2(ProjectMarkers::instance().find(idMarker(id))->action.isEmpty(),
			"a refused action was written to the marker anyway");

		// A command the arguments DO satisfy is stored, and so is a command
		// that takes no arguments at all.
		QVERIFY(run(QStringLiteral("marker.set"), {{QStringLiteral("marker"), id},
			{QStringLiteral("action"), QStringLiteral("transport.set_tempo")},
			{QStringLiteral("args"), QJsonObject{{QStringLiteral("bpm"), 120}}}}).ok);
		QCOMPARE(run(QStringLiteral("marker.set"), {{QStringLiteral("marker"), id},
			{QStringLiteral("action"), QStringLiteral("transport.play")},
			{QStringLiteral("args"), QJsonObject{{QStringLiteral("bpm"), 120}}}}).errorKind,
			ControlErrorKind::InvalidArgs);
		QVERIFY(run(QStringLiteral("marker.set"), {{QStringLiteral("marker"), id},
			{QStringLiteral("action"), QStringLiteral("transport.play")}}).ok);
		QCOMPARE(ProjectMarkers::instance().find(idMarker(id))->action, QStringLiteral("transport.play"));
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
