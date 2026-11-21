/*
 * ControlArmReversibilityTest.cpp - the single-track arm is irreversible too
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

/*! The batch arm's sibling: track.set_arm arms ONE route, and the capture it starts writes a
 *  take file that outlives the capture. Its recorded inverse (this command with armed: false)
 *  stopped the capture and retired the journal but left the take on disk, so control.undo
 *  claimed to reverse an arming it could not restore. The row is irreversible now, the arming
 *  records no inverse, and control.undo refuses it typed while the route and the take stay.
 *  The batch's own regression is UndoHonestyTest::armAllRefusesUndoThatWouldLeaveItsTakesBehind. */

#include <QtTest>

#include <QFile>
#include <QJsonObject>
#include <QScopeGuard>
#include <QTemporaryDir>

#include "ConfigManager.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "Engine.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

} // namespace

class ControlArmReversibilityTest : public QObject
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

	//! Arming one track: control.undo refuses it, and the route and the take both survive.
	void armingOneTrackRefusesUndoThatWouldLeaveItsTakeBehind()
	{
		// The default take sits beside the working directory's recovery file: keep it private.
		QTemporaryDir takes;
		QVERIFY(takes.isValid());
		const auto restoreDir = qScopeGuard([previous = ConfigManager::inst()->workingDir()] { ConfigManager::inst()->setWorkingDir(previous); });
		ConfigManager::inst()->setWorkingDir(takes.path());

		const ControlResult added = run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("sample")}});
		QVERIFY2(added.ok, qPrintable(added.errorMessage));
		const QString track = added.result.value(QStringLiteral("track")).toString();
		QVERIFY(!track.isEmpty());

		const ControlResult armed = run(QStringLiteral("track.set_arm"),
			{{QStringLiteral("track"), track}, {QStringLiteral("armed"), true}});
		QVERIFY2(armed.ok, qPrintable(armed.errorMessage));
		const QString take = armed.result.value(QStringLiteral("file")).toString();
		QVERIFY2(!take.isEmpty(), "the arm did not report its take file");
		QVERIFY2(QFile::exists(take), qPrintable(take));
		ControlResult error;
		QVERIFY(control::trackRecordArmed(control::resolveTrack(track, &error)));

		const ControlResult undo = run(QStringLiteral("control.undo"));
		// The refused undo must leave both the armed route and the take file alone.
		const bool stillArmed = control::trackRecordArmed(control::resolveTrack(track, &error));
		const bool takeRemains = QFile::exists(take);
		// Stop the capture on either implementation, so a RED run leaves no route writing.
		QVERIFY(run(QStringLiteral("track.set_arm"),
			{{QStringLiteral("track"), track}, {QStringLiteral("armed"), false}}).ok);

		QVERIFY2(!undo.ok, "control.undo claimed it could remove the take from track.set_arm");
		QCOMPARE(undo.errorKind, ControlErrorKind::Irreversible);
		QVERIFY2(undo.errorMessage.contains(QStringLiteral("track.set_arm")), qPrintable(undo.errorMessage));
		QVERIFY2(stillArmed, "the refused undo disarmed the route");
		QVERIFY2(takeRemains, "the refused undo took the take away");
	}
};

QTEST_GUILESS_MAIN(ControlArmReversibilityTest)

#include "ControlArmReversibilityTest.moc"
