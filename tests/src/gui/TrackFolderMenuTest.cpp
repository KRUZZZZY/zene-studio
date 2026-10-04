/*
 * TrackFolderMenuTest.cpp - folders from the interface: make one, fill it, collapse it
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

/*! The M3 folder surface, in the real application: the song editor's "Add folder track" makes a
 *  folder (track.add), a track's Folder menu moves it in (track.set_folder), the folder's own menu
 *  collapses it (track.folder_set_collapsed) - and a collapsed folder's children leave the track
 *  list while the folder's row stays - pins it and switches its routing. A collapse sent over the
 *  socket redraws the list too, through TrackFolder::stateChanged. And (0.5.0 E21) a pinned folder's
 *  row stays on screen while the list scrolls: it sits in the strip above the scroll area, leaving a
 *  zero-height placeholder at its index in the scrolling list, and an unpin puts it back there. */

#include <QtTest>

#include <QLayout>
#include <QMenu>
#include <QScrollArea>
#include <QScrollBar>
#include <QToolBar>

#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "MainWindow.h"
#include "Song.h"
#include "SongEditor.h"
#include "TrackFolder.h"
#include "TrackFolderMenu.h"
#include "TrackPinStrip.h"
#include "TrackView.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

//! The action in @a menu (searched through its submenus) whose text is @a text.
QAction* actionIn(QMenu* menu, const QString& text)
{
	for (QAction* action : menu->actions())
	{
		if (action->text() == text) { return action; }
		if (action->menu() != nullptr)
		{
			if (QAction* inner = actionIn(action->menu(), text)) { return inner; }
		}
	}
	return nullptr;
}

TrackView* viewOf(const Track* track)
{
	for (TrackView* view : getGUI()->songEditor()->m_editor->trackViews())
	{
		if (view->getTrack() == track) { return view; }
	}
	return nullptr;
}

} // namespace

class TrackFolderMenuTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		guitest::startGui(m_home);
		ControlRegistry::setReady(true);
		getGUI()->mainWindow()->show();
		getGUI()->songEditor()->parentWidget()->show();
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void aFolderIsMadeFilledAndCollapsedFromTheInterface()
	{
		// "Add folder track" on the song editor's own toolbar.
		QAction* addFolder = nullptr;
		for (QToolBar* bar : getGUI()->songEditor()->findChildren<QToolBar*>())
		{
			for (QAction* action : bar->actions())
			{
				if (action->text() == SongEditorWindow::tr("Add folder track")) { addFolder = action; }
			}
		}
		QVERIFY2(addFolder != nullptr, "no Add folder track action on the song editor");
		addFolder->trigger();
		TrackFolder* folder = nullptr;
		for (Track* track : Engine::getSong()->tracks())
		{
			if (auto* candidate = dynamic_cast<TrackFolder*>(track)) { folder = candidate; }
		}
		QVERIFY2(folder != nullptr, "the action made no folder");

		Track* child = Track::create(Track::Type::Sample, Engine::getSong());
		QCoreApplication::processEvents();
		QVERIFY(viewOf(child) != nullptr);

		// The child's Folder menu moves it in.
		QMenu childMenu;
		addTrackFolderMenu(&childMenu, child);
		QAction* into = actionIn(&childMenu, folder->name());
		QVERIFY2(into != nullptr, "the Move to folder menu does not list the folder");
		into->trigger();
		QCOMPARE(child->parentFolder(), folder);
		// The child's row is indented, its settings column keeping its width.
		const int width = viewOf(child)->getTrackSettingsWidget()->width();
		QTRY_VERIFY(viewOf(child)->getTrackSettingsWidget()->contentsMargins().left() > 0);
		QCOMPARE(viewOf(child)->getTrackSettingsWidget()->width(), width);
		QCOMPARE(viewOf(folder)->getTrackSettingsWidget()->contentsMargins().left(), 0);

		// The folder's own menu collapses it: the child's row leaves the list, the folder's stays.
		QMenu folderMenu;
		addTrackFolderMenu(&folderMenu, folder);
		actionIn(&folderMenu, QObject::tr("Collapsed"))->trigger();
		QVERIFY(folder->isCollapsed());
		QTRY_VERIFY(!viewOf(child)->isVisible());
		QVERIFY(viewOf(folder)->isVisible());

		// Expanded over the socket: the list follows (stateChanged -> realignTracks).
		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("track.folder_set_collapsed"),
			{{QStringLiteral("track"), control::trackIdOf(folder)}, {QStringLiteral("collapsed"), false}}).ok);
		QTRY_VERIFY(viewOf(child)->isVisible());

		QMenu again;
		addTrackFolderMenu(&again, folder);
		actionIn(&again, QObject::tr("Pinned"))->trigger();
		QVERIFY(folder->isPinned());

		// "No folder" takes the child back out.
		QMenu out;
		addTrackFolderMenu(&out, child);
		actionIn(&out, QObject::tr("No folder"))->trigger();
		QVERIFY(child->parentFolder() == nullptr);
		QTRY_COMPARE(viewOf(child)->getTrackSettingsWidget()->contentsMargins().left(), 0);
	}

	void aPinnedFolderStaysInViewWhileTheListScrolls()
	{
		SongEditor* editor = getGUI()->songEditor()->m_editor;
		Track* folder = Track::create(Track::Type::Folder, Engine::getSong());
		for (int i = 0; i < 40; ++i) { Track::create(Track::Type::Sample, Engine::getSong()); }
		QCoreApplication::processEvents();
		TrackView* view = viewOf(folder);
		QVERIFY(view != nullptr);
		const int index = editor->trackViews().indexOf(view);
		QWidget* content = editor->contentWidget()->widget();
		QLayout* rows = content->layout();

		const auto pin = [folder](bool on) {
			return ControlRegistry::instance()->invoke(QStringLiteral("track.set_pinned"),
				{{QStringLiteral("track"), control::trackIdOf(folder)}, {QStringLiteral("pinned"), on}}).ok;
		};
		QVERIFY(pin(true));
		TrackPinStrip* strip = editor->pinStrip();
		QTRY_VERIFY(strip->pinnedViews().contains(view));
		QCOMPARE(view->parentWidget(), strip);
		// Its index in the scrolling list holds a placeholder that takes no room, so every row below keeps its index.
		QVERIFY(rows->itemAt(index)->widget() != view);
		QCOMPARE(rows->itemAt(index)->widget()->height(), 0);

		// Scroll to the end of the list: the folder's own row would be far above the viewport, and it is in view.
		QScrollBar* scroll = editor->contentWidget()->verticalScrollBar();
		QTRY_VERIFY(scroll->maximum() > 0);
		scroll->setValue(scroll->maximum());
		QTRY_VERIFY(strip->isVisible());
		QVERIFY(view->isVisible());
		const int viewBottom = view->mapTo(editor, QPoint(0, view->height())).y();
		QVERIFY2(viewBottom <= editor->contentWidget()->geometry().top(), "the pinned row is not above the scrolling tracks");
		QVERIFY(view->mapTo(editor, QPoint(0, 0)).y() >= 0);

		// Unpinned, the row goes back into the scrolling list at its own index.
		QVERIFY(pin(false));
		QTRY_COMPARE(view->parentWidget(), content);
		QCOMPARE(rows->itemAt(index)->widget(), view);
		QVERIFY(!strip->pinnedViews().contains(view));
		QVERIFY(view->isVisible());
	}

private:
	QTemporaryDir m_home;
};

QTEST_MAIN(TrackFolderMenuTest)
#include "TrackFolderMenuTest.moc"
