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
 *  view and remembers the one picked in it.
 *  Theme 3 (0.5.0 UX): the current source's search field is the panel's first control, and a source without
 *  one shows none; typing searches once and refines that history entry in place; Enter in the field first
 *  highlights a result, then loads it (one undo step); Space previews a result and toggles it off, another
 *  result stops it; the tab, the query and focus leaving the Browser stop it too, focus moving inside it does not. */

#include <QtTest>

#include <QFile>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QTemporaryDir>
#include <QTreeWidget>

#include "BrowserPanel.h"
#include "BrowserResults.h"
#include "BrowserSearchView.h"
#include "ControlRegistry.h"
#include "EditLocks.h"
#include "Engine.h"
#include "LibraryCommandMenus.h"
#include "MenuTestSupport.h"
#include "ProjectInfo.h"
#include "SampleTrack.h"
#include "SideBarWidget.h"
#include "Song.h"
#include "Track.h"
#include "../core/TakeLaneTestSupport.h"

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

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! A search result row for the audio file @a path, as the search view lists one.
QTreeWidgetItem* audioRow(const QString& path)
{
	return browserresults::rowOf({{QStringLiteral("name"), QFileInfo(path).fileName()}, {QStringLiteral("kind"), QStringLiteral("audio")},
		{QStringLiteral("path"), path}});
}

QJsonArray historyEntries()
{
	return run(QStringLiteral("browser.history")).result.value(QStringLiteral("entries")).toArray();
}

//! Whether one of the view's labels shows @a text (the status line is the one that shows a refusal).
bool labelShows(const QWidget* view, const QString& text)
{
	for (const QLabel* label : view->findChildren<QLabel*>())
	{
		if (label->text().contains(text)) { return true; }
	}
	return false;
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

	void theProjectMenuOffersOtherProjectsAndTypedDocuments()  // 0.5.0 F3: WF-042, WF-048
	{
		QMenu menu;
		LibraryCommandMenus::populateProject(&menu);
		const QStringList declared = declaredCommands(&menu);
		for (const char* id : {"project.new_document", "reference.open", "reference.get_state", "project.switch", "project.paste_tracks"})
		{
			QVERIFY2(declared.contains(QLatin1String(id)), id);
			QVERIFY2(ControlRegistry::instance()->hasCommand(QLatin1String(id)), id);
		}
		QVERIFY(lmms::test::menuAction(&menu, {QStringLiteral("New Document"), QStringLiteral("Mastering")}) != nullptr);
		QVERIFY(lmms::test::menuAction(&menu, {QStringLiteral("Other Projects"), QStringLiteral("Open as Reference...")}) != nullptr);
	}

	void theDocumentTypeMenuTicksOneTypeAtATime()  // 0.5.0 F3: WF-048 - the items are exclusive
	{
		QMenu menu;
		LibraryCommandMenus::populateProject(&menu);
		const QStringList every = ProjectInfo::documentTypes();
		QVERIFY(every.size() >= 2);
		QMenu* types = nullptr;
		for (QAction* action : menu.actions())
		{
			if (action->menu() != nullptr && action->text() == QObject::tr("Document Type")) { types = action->menu(); }
		}
		QVERIFY(types != nullptr);

		// The document's own type, and no other, is ticked.
		const auto ticked = [types] {
			QStringList on;
			for (QAction* action : types->actions())
			{
				if (action->isCheckable() && action->isChecked()) { on << action->text(); }
			}
			return on;
		};
		QCOMPARE(ticked(), QStringList{ProjectInfo::instance().documentType()});

		const QString other = every.contains(QStringLiteral("mastering")) ? QStringLiteral("mastering") : every.at(1);
		QAction* chosen = lmms::test::menuAction(&menu, {QObject::tr("Document Type"), other});
		QVERIFY(chosen != nullptr);
		chosen->trigger();
		QCOMPARE(ticked(), QStringList{other});  // choosing a type unticks the one it replaced

		chosen->trigger();  // choosing the type already chosen leaves it ticked
		QCOMPARE(ticked(), QStringList{other});
	}

	void theTitleNamesANonDefaultDocumentType()  // 0.5.0 F3: WF-048 (production, the default, is untagged)
	{
		QCOMPARE(LibraryCommandMenus::taggedTitle(QStringLiteral("Song - Zene Studio"), QStringLiteral("mastering")),
			QStringLiteral("[Mastering] Song - Zene Studio"));
		QCOMPARE(LibraryCommandMenus::taggedTitle(QStringLiteral("[Live] Song* - Zene Studio"), QStringLiteral("production")),
			QStringLiteral("Song* - Zene Studio"));
	}

	void aTrackGetsASpacerFromItsMenu()  // 0.5.0 F3: WF-107
	{
		Track* track = Track::create(Track::Type::Instrument, Engine::getSong());
		QMenu menu;
		LibraryCommandMenus::populateTrack(&menu, track);
		QVERIFY(lmms::test::menuAction(&menu, {QStringLiteral("Add Spacer Below...")}) != nullptr);
		QVERIFY(lmms::test::menuAction(&menu, {QStringLiteral("Remove Spacer Below")}) == nullptr);
		QVERIFY(run(QStringLiteral("track.set_spacer"), {{QStringLiteral("track"), control::trackIdOf(track)}, {QStringLiteral("height"), 20}}).ok);
		QMenu again;
		LibraryCommandMenus::populateTrack(&again, track);
		QAction* remove = lmms::test::menuAction(&again, {QStringLiteral("Remove Spacer Below")});
		QVERIFY(remove != nullptr);
		remove->trigger();
		QCOMPARE(run(QStringLiteral("track.spacers")).result.value(QStringLiteral("count")).toInt(), 0);
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

	void searchIsTheFirstControl()
	{
		BrowserPanel panel;
		auto* search = new BrowserSearchView(&panel);
		panel.addSource(search);
		panel.addSource(new SideBarWidget(QStringLiteral("Beta"), QPixmap(), &panel));
		QLineEdit* field = panel.searchField();
		QVERIFY(field != nullptr);
		QCOMPARE(field->objectName(), QStringLiteral("browserSearch"));
		QWidget* first = panel.layout()->itemAt(0)->widget();
		QVERIFY(first != nullptr && first->isAncestorOf(field));  // above the selector and the list
		QVERIFY(!search->isAncestorOf(field));
		QVERIFY(panel.selectSource(QStringLiteral("Beta")));
		QVERIFY(panel.searchField() == nullptr);  // search covers the current tab: a tab without one shows none
	}

	void typingSearchesOnceAndRefinesInPlace()
	{
		BrowserPanel panel;
		panel.addSource(new BrowserSearchView(&panel));
		const qsizetype before = historyEntries().size();
		const auto lastLabel = [] { return historyEntries().last().toObject().value(QStringLiteral("label")).toString(); };
		QTest::keyClicks(panel.searchField(), QStringLiteral("zq"));
		QTRY_COMPARE(historyEntries().size(), before + 1);  // live: no Return needed
		QCOMPARE(lastLabel(), QStringLiteral("zq"));
		QTest::keyClicks(panel.searchField(), QStringLiteral("x"));
		QTRY_COMPARE(lastLabel(), QStringLiteral("zqx"));
		QCOMPARE(historyEntries().size(), before + 1);  // refined in place, not one entry per pause in typing
	}

	void enterHighlightsThenLoadsTheResult()
	{
		const QString wav = taketest::writeToneWav(m_dir.filePath(QStringLiteral("enter.wav")), 440.0);
		QVERIFY(!wav.isEmpty());
		BrowserPanel panel;
		auto* search = new BrowserSearchView(&panel);
		panel.addSource(search);
		QTreeWidgetItem* row = audioRow(wav);
		search->results()->addTopLevelItem(row);
		Song* song = Engine::getSong();
		const std::size_t tracks = song->tracks().size();
		QTest::keyClick(panel.searchField(), Qt::Key_Return);
		QCOMPARE(search->results()->currentItem(), row);  // nothing was highlighted: Enter highlights the first result
		QCOMPARE(song->tracks().size(), tracks);
		QTest::keyClick(panel.searchField(), Qt::Key_Return, Qt::ShiftModifier);  // Shift+Enter: onto a new sample track
		QCOMPARE(song->tracks().size(), tracks + 1);
		QVERIFY(dynamic_cast<SampleTrack*>(song->tracks().back()) != nullptr);
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // browser.load is one undo step
		QCOMPARE(song->tracks().size(), tracks);
	}

	void spaceTogglesThePreviewAndAnotherResultStopsIt()
	{
		const QString a = taketest::writeToneWav(m_dir.filePath(QStringLiteral("a.wav")), 440.0);
		const QString b = taketest::writeToneWav(m_dir.filePath(QStringLiteral("b.wav")), 660.0);
		BrowserSearchView view(nullptr);
		QTreeWidgetItem* first = audioRow(a);
		QTreeWidgetItem* second = audioRow(b);
		view.results()->addTopLevelItems({first, second});
		view.results()->setCurrentItem(first);
		QTest::keyClick(view.results(), Qt::Key_Space);
		QCOMPARE(view.actions()->previewing(), a);
		view.results()->setCurrentItem(second);  // another result
		QVERIFY(view.actions()->previewing().isEmpty());
		QTest::keyClick(view.results(), Qt::Key_Space);
		QCOMPARE(view.actions()->previewing(), b);
		QTest::keyClick(view.results(), Qt::Key_Space);  // Space on the one playing stops it
		QVERIFY(view.actions()->previewing().isEmpty());
	}

	void thePreviewStopsWhenTheTabTheQueryOrTheFocusChanges()
	{
		const QString wav = taketest::writeToneWav(m_dir.filePath(QStringLiteral("stop.wav")), 440.0);
		QWidget window;
		auto* outside = new QLineEdit(&window);
		auto* panel = new BrowserPanel(&window);
		auto* search = new BrowserSearchView(panel);
		panel->addSource(search);
		panel->addSource(new SideBarWidget(QStringLiteral("Beta"), QPixmap(), panel));
		BrowserResultActions* actions = search->actions();
		QVERIFY(actions->preview(wav, QStringLiteral("audio")));
		QVERIFY(panel->selectSource(QStringLiteral("Beta")));  // another tab
		QVERIFY(actions->previewing().isEmpty());
		QVERIFY(panel->selectSource(search->title()));
		QVERIFY(actions->preview(wav, QStringLiteral("audio")));
		QTest::keyClick(panel->searchField(), Qt::Key_K);  // the query changes
		QVERIFY(actions->previewing().isEmpty());
		QVERIFY(actions->preview(wav, QStringLiteral("audio")));
		// QApplication's own signal, as a focus change in an active window emits it (offscreen windows need not activate).
		emit qApp->focusChanged(search->results(), panel->searchField());  // focus moving inside the Browser
		QCOMPARE(actions->previewing(), wav);
		emit qApp->focusChanged(search->results(), outside);  // focus leaving it
		QVERIFY(actions->previewing().isEmpty());
	}

	void aResultRowForMissingMediaShowsPlaceholdersInsteadOfZeroAnd1970()
	{
		const QString gone = m_dir.filePath(QStringLiteral("not-here.wav"));
		QVERIFY(!QFileInfo::exists(gone));
		// Project Media marks a missing reference's unknown metadata with -1 rather than QFileInfo's 0 / epoch.
		QTreeWidgetItem* row = browserresults::rowOf(
			{{QStringLiteral("name"), QFileInfo(gone).fileName()}, {QStringLiteral("kind"), QStringLiteral("missing")},
				{QStringLiteral("path"), gone}, {QStringLiteral("size_bytes"), qint64(-1)}, {QStringLiteral("modified_ms"), qint64(-1)}});
		QCOMPARE(row->text(browserresults::Size), QStringLiteral("-"));       // not "0" for an unknown size
		QCOMPARE(row->text(browserresults::Modified), QStringLiteral("-"));  // not 1970-01-01
		QVERIFY(!row->text(browserresults::Name).isEmpty());                  // the rest of the row is untouched
	}

	void aRefusedCollectionRemovalSaysWhyOnTheStatusLine()  // 0.5.0 BUG 12: the store's refusal is shown, not dropped
	{
		qputenv("ZENE_BROWSER_COLLECTIONS", m_dir.filePath(QStringLiteral("collections.json")).toLocal8Bit());
		QVERIFY(run(QStringLiteral("browser.collection_save"), {{QStringLiteral("name"), QStringLiteral("Snares")},
			{QStringLiteral("text"), QStringLiteral("snare")}}).ok);
		BrowserSearchView view(nullptr);  // lists "Snares"
		// Removed behind the list's back (another window, a script): the menu's Remove on the stale row is refused.
		QVERIFY(run(QStringLiteral("browser.collection_remove"), {{QStringLiteral("name"), QStringLiteral("Snares")}}).ok);
		view.removeCollection(QStringLiteral("Snares"));
		QVERIFY2(labelShows(&view, QStringLiteral("no collection 'Snares'")), "a refused removal showed nothing");
		// A damaged store is refused too, and the collection file is left as it is.
		QFile store(m_dir.filePath(QStringLiteral("collections.json")));
		QVERIFY(store.open(QIODevice::WriteOnly));
		store.write("[\"not an object\"]");
		store.close();
		view.removeCollection(QStringLiteral("Snares"));
		QVERIFY2(labelShows(&view, QStringLiteral("is not a JSON object")), "a damaged store's refusal showed nothing");
	}

private:
	QTemporaryDir m_dir;
};

QTEST_MAIN(LibraryMenusTest)
#include "LibraryMenusTest.moc"
