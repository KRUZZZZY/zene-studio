/*
 * ControlBounceSupportTest.cpp - default path isolation for unsaved projects
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

#include <QtTest>

#include <QJsonObject>

#include "ControlBounceSupport.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Song.h"
#include "Track.h"

using namespace lmms;

class ControlBounceSupportTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase() { Engine::init(true); }

	void cleanupTestCase() { Engine::destroy(); }

	void unsavedProjectsUseDifferentDefaultPaths()
	{
		Song* song = Engine::getSong();
		const QJsonObject args;
		const BounceInPlace::Range whole;

		song->createNewProject();
		Track* firstTrack = song->tracks().front();
		const QString firstId = control::trackIdOf(firstTrack);
		const QString firstPath = control::resolveOutPath(args, firstTrack, QStringLiteral("bounce"), whole);
		QCOMPARE(control::resolveOutPath(args, firstTrack, QStringLiteral("bounce"), whole), firstPath);

		song->createNewProject();
		Track* secondTrack = song->tracks().front();
		QCOMPARE(control::trackIdOf(secondTrack), firstId);
		const QString secondPath = control::resolveOutPath(args, secondTrack, QStringLiteral("bounce"), whole);

		QVERIFY2(firstPath != secondPath, qPrintable(firstPath));
	}
};

QTEST_GUILESS_MAIN(ControlBounceSupportTest)

#include "ControlBounceSupportTest.moc"
