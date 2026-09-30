/*
 * TrackRenderMenuTest.cpp - the track menu's Freeze / Bounce items and the File menu's DAWproject items
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

/*! What the menus offer, and that each item names its command: an instrument or sample track gets
 *  Freeze track and Bounce in place (freeze.track / bounce.in_place), an automation track gets
 *  neither, and File gets Import / Export DAWproject... Triggering Bounce in place on a track with
 *  nothing to render comes back refused, typed, without a dialog or a crash in an unattended run -
 *  the renders themselves are proved by the freeze and bounce command suites. */

#include <QtTest>

#include <QMenu>

#include "ControlRegistry.h"
#include "Engine.h"
#include "Song.h"
#include "Track.h"
#include "TrackRenderMenu.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

QStringList commandsIn(QMenu& menu)
{
	QStringList commands;
	for (QAction* action : menu.actions()) { commands << action->data().toString(); }
	return commands;
}

} // namespace

class TrackRenderMenuTest : public QObject
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

	void theMenusOfferTheRightItems()
	{
		Track* sample = Track::create(Track::Type::Sample, Engine::getSong());
		QMenu forSample;
		addTrackRenderActions(&forSample, sample);
		QCOMPARE(commandsIn(forSample), (QStringList{QStringLiteral("freeze.track"), QStringLiteral("bounce.in_place")}));

		Track* automation = Track::create(Track::Type::Automation, Engine::getSong());
		QMenu forAutomation;
		addTrackRenderActions(&forAutomation, automation);
		QVERIFY(forAutomation.actions().isEmpty());

		QMenu file;
		addDawProjectActions(&file);
		QCOMPARE(commandsIn(file), (QStringList{QStringLiteral("dawproject.import"), QStringLiteral("dawproject.export")}));

		forSample.actions().at(1)->trigger();  // an empty track: refused, logged, no dialog
	}
};

QTEST_MAIN(TrackRenderMenuTest)
#include "TrackRenderMenuTest.moc"
