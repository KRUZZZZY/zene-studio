/*
 * LibraryMenusTest.cpp - the library and project entries in the shell's menus, the search view's gallery, the browser's remembered sources
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
/*! 0.5.0 L11 / L12 (W3), the interface half: the Project Tools menu declares the new project commands
 *  (each entry runs a registered command); a track's Lock entry locks it and then offers Unlock; the
 *  search view swaps its columns for the gallery; the browser panel shows the source remembered for a
 *  view and remembers the one picked in it. */

#include <QtTest>

#include <QListWidget>
#include <QMenu>
#include <QTreeWidget>

#include "BrowserPanel.h"
#include "BrowserSearchView.h"
#include "ControlRegistry.h"
#include "EditLocks.h"
#include "Engine.h"
#include "LibraryCommandMenus.h"
#include "MenuTestSupport.h"
#include "SideBarWidget.h"
#include "Song.h"
#include "Track.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

QStringList declaredCommands(QMenu* menu)
{
	QStringList out;
	for (QAction* action : lmms::test::leafActions(menu)) { out << action->property("controlCommand").toString(); }
	return out;
}

} // namespace

class LibraryMenusTest : public QObject
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

	void theProjectMenuDeclaresItsCommands()
	{
		QMenu menu;
		LibraryCommandMenus::populateProject(&menu);
		const QStringList declared = declaredCommands(&menu);
		for (const char* id : {"project.settings", "project.metadata", "mixer.snapshot_store", "mixer.snapshot_list", "project.locks",
			"render.queue_list", "app.tasks", "project.paste_tracks", "link.set_enabled", "clock.master_set",
			"project.alternative_save", "project.alternatives", "project.subproject_add", "project.subprojects"})
		{
			QVERIFY2(declared.contains(QLatin1String(id)), id);
			QVERIFY2(ControlRegistry::instance()->hasCommand(QLatin1String(id)), id);
		}
	}

	void aTrackLocksFromItsMenu()
	{
		Track* track = Track::create(Track::Type::Instrument, Engine::getSong());
		QMenu menu;
		LibraryCommandMenus::populateTrack(&menu, track);
		QAction* lock = lmms::test::menuAction(&menu, {QStringLiteral("Lock Track")});
		QVERIFY(lock != nullptr);
		lock->trigger();
		QVERIFY(EditLocks::instance().trackLocked(track->id()));
		QMenu again;
		LibraryCommandMenus::populateTrack(&again, track);
		QVERIFY(lmms::test::menuAction(&again, {QStringLiteral("Unlock Track")}) != nullptr);
		EditLocks::instance().clear();
	}

	void theSearchViewShowsAGallery()
	{
		BrowserSearchView view(nullptr);
		QVERIFY(view.galleryView()->isHidden());
		view.setGallery(true);
		QVERIFY(!view.galleryView()->isHidden());
		QVERIFY(view.results()->isHidden());
		view.setGallery(false);
		QVERIFY(view.galleryView()->isHidden());
	}

	void theBrowserRemembersASourcePerView()
	{
		BrowserPanel panel;
		panel.addSource(new SideBarWidget(QStringLiteral("Alpha"), QPixmap(), &panel));
		panel.addSource(new SideBarWidget(QStringLiteral("Beta"), QPixmap(), &panel));
		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("browser.remember_source"),
			{{QStringLiteral("context"), QStringLiteral("mix")}, {QStringLiteral("source"), QStringLiteral("Beta")}}).ok);
		panel.setContext(QStringLiteral("arrange"));
		QVERIFY(panel.selectSource(QStringLiteral("Beta")));
		QVERIFY(panel.selectSource(QStringLiteral("Alpha")));  // picked in the arranger: remembered for it
		panel.setContext(QStringLiteral("mix"));
		QCOMPARE(panel.currentSource(), QStringLiteral("Beta"));
		panel.setContext(QStringLiteral("arrange"));
		QCOMPARE(panel.currentSource(), QStringLiteral("Alpha"));
	}
};

QTEST_MAIN(LibraryMenusTest)
#include "LibraryMenusTest.moc"
