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
 *  result stops it; the tab, the query and focus leaving the Browser stop it too, focus moving inside it does not.
 *  Q050-075 (R-workflow-4): the Search file metadata toggle sends browser.query's 'probe' and finds files
 *  by their embedded title, artist and numeric audio metadata; tag: searches still work without it, and the
 *  probe spend on a large directory stops at the command's reported probe_limit. */

#include <QtTest>

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QInputDialog>
#include <QFile>
#include <QApplication>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>

#include "BrowserCatalog.h"
#include "BrowserPanel.h"
#include "BrowserResults.h"
#include "BrowserSearchView.h"
#include "ConfigManager.h"
#include "BrowserTagMenu.h"
#include "ConfigManager.h"
#include "CommandForm.h"
#include "ControlRegistry.h"
#include "EditLocks.h"
#include "Engine.h"
#include "GuiTestApplication.h"
#include "LibraryCommandMenus.h"
#include "MenuTestSupport.h"
#include "ProjectInfo.h"
#include "SampleTrack.h"
#include "SideBarWidget.h"
#include "Song.h"
#include "Track.h"
#include "../core/BrowserTestSupport.h"
#include "../core/TakeLaneTestSupport.h"

using namespace lmms;
using namespace lmms::gui;
using namespace browserfixture;

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

//! The last entry the search view left in the browser's history.
QJsonObject lastHistoryEntry()
{
	const QJsonArray entries = historyEntries();
	return entries.isEmpty() ? QJsonObject() : entries.last().toObject();
}

//! The view's metadata toggle, found the way any client would: by its object name.
QCheckBox* metadataToggle(BrowserSearchView* view)
{
	return view->findChild<QCheckBox*>(QStringLiteral("browserSearchMetadata"));
}

//! The root filter, so a search walks only this test's private samples folder.
bool restrictToPrivateSamples(BrowserSearchView* view)
{
	QComboBox* root = view->findChild<QComboBox*>(QStringLiteral("browserSearchRoot"));
	if (root == nullptr) { return false; }
	root->setCurrentIndex(root->findData(QStringLiteral("samples")));
	return true;
}

//! Whether the results tree lists the file at @a path.
bool resultsList(BrowserSearchView* view, const QString& path)
{
	for (int row = 0; row < view->results()->topLevelItemCount(); ++row)
	{
		if (view->results()->topLevelItem(row)->data(browserresults::Name, browserresults::PathRole).toString()
			== QFileInfo(path).absoluteFilePath())
		{
			return true;
		}
	}
	return false;
}

//! The entry of the submenu under a top-level action of @a menu, by its text
//! (the pattern BrowserTagMenuTest drives the Tags and Category menus with).
QAction* submenuAction(QMenu& menu, const QString& text)
{
	for (QAction* top : menu.actions())
	{
		if (top->menu() == nullptr) { continue; }
		for (QAction* action : top->menu()->actions())
		{
			if (action->text() == text) { return action; }
		}
	}
	return nullptr;
}

//! A one-byte file at @a path, as the browser fixtures here are written.
QString touchFile(const QString& path)
{
	QFile file(path);
	file.open(QIODevice::WriteOnly);
	file.write("x");
	return path;
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
		// Hermetic browser state: the category facet's store, the collections
		// file and the library index live in this test's own temporary tree,
		// never the user's (the rule LibraryProjectsTest and
		// ControlBrowserCommandsTest already follow), and the browser's roots
		// resolve into it - a fixture written under the samples root is then a
		// file the Search view's queries can actually find.
		qputenv("ZENE_BROWSER_COLLECTIONS", m_dir.filePath(QStringLiteral("collections.json")).toLocal8Bit());
		qputenv("ZENE_BROWSER_INDEX", m_dir.filePath(QStringLiteral("index.json")).toLocal8Bit());
		guitest::startGui(m_dir);
		ConfigManager::inst()->setWorkingDir(m_dir.filePath(QStringLiteral("work")));
		QVERIFY(BrowserTagStore::instance().openAt(m_dir.filePath(QStringLiteral("browser-tags.json"))));
		ControlRegistry::setReady(true);
		QVERIFY2(m_dir.isValid(), "no temporary directory for the library fixtures");
		// A private working directory: the user samples this test's searches walk and
		// the tag store it writes are never the developer's own (R-workflow-4).
		ConfigManager::inst()->setWorkingDir(m_dir.filePath(QStringLiteral("work")));
		QVERIFY(QDir().mkpath(samplesRoot()));
		// The embedded-title fixture every browser test reads (browserfixture's own
		// RIFF/WAVE writer, so the probe reads bytes this test wrote itself).
		m_kick = QDir(samplesRoot()).filePath(QStringLiteral("kick_48k.wav"));
		QVERIFY2(writeWave(m_kick, WaveSpec()), "the embedded-title fixture was not written");
		// A second file whose EMBEDDED fields say something its name does not:
		// artist "Drumsmith", 22222 Hz, one channel, 3333 frames.
		WaveSpec pad;
		pad.title = QStringLiteral("Ambient Pad");
		pad.artist = QStringLiteral("Drumsmith");
		pad.sampleRate = 22222;
		pad.channels = 1;
		pad.frames = 3333;
		pad.burstStartFrame = 1600;
		pad.burstFrames = 32;
		m_pad = QDir(samplesRoot()).filePath(QStringLiteral("pad.wav"));
		QVERIFY2(writeWave(m_pad, pad), "the embedded-artist fixture was not written");
	}

	void cleanupTestCase()
	{
		// Leave the store pointed at a file inside the temporary tree, so nothing
		// outside it is read or written by the rest of this process.
		BrowserTagStore::instance().openAt(m_dir.filePath(QStringLiteral("done.json")));
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

	void aSettingsCommandSwitchesToItsDocumentView()
	{
		QVERIFY(run(QStringLiteral("project.settings"), {{QStringLiteral("document_type"), QStringLiteral("production")}}).ok);
		QVERIFY(run(QStringLiteral("window.set_view"), {{QStringLiteral("view"), QStringLiteral("arrange")}}).ok);
		QCOMPARE(run(QStringLiteral("window.get_state")).result.value(QStringLiteral("view")).toString(), QStringLiteral("arrange"));

		const ControlResult changed = run(QStringLiteral("project.settings"), {{QStringLiteral("document_type"), QStringLiteral("mastering")}});
		QVERIFY2(changed.ok, qPrintable(changed.errorMessage));
		QCOMPARE(ProjectInfo::instance().documentType(), QStringLiteral("mastering"));
		QCOMPARE(run(QStringLiteral("window.get_state")).result.value(QStringLiteral("view")).toString(), QStringLiteral("mix"));

		QVERIFY(run(QStringLiteral("project.settings"), {{QStringLiteral("document_type"), QStringLiteral("mastering")}}).ok);
		const ControlResult invalid = run(QStringLiteral("project.settings"), {{QStringLiteral("document_type"), QStringLiteral("portrait")}});
		QVERIFY(!invalid.ok);
		QCOMPARE(ProjectInfo::instance().documentType(), QStringLiteral("mastering"));
		QCOMPARE(run(QStringLiteral("window.get_state")).result.value(QStringLiteral("view")).toString(), QStringLiteral("mix"));
	}

	void loadingDefaultProductionRestoresArrangeView()
	{
		const QString path = m_dir.filePath(QStringLiteral("production.mmp"));
		QVERIFY(run(QStringLiteral("project.settings"), {{QStringLiteral("document_type"), QStringLiteral("production")}}).ok);
		QVERIFY2(Engine::getSong()->guiSaveProjectAs(path), "the production project fixture was not saved");

		QVERIFY(run(QStringLiteral("project.settings"), {{QStringLiteral("document_type"), QStringLiteral("mastering")}}).ok);
		QVERIFY(run(QStringLiteral("window.set_view"), {{QStringLiteral("view"), QStringLiteral("mix")}}).ok);
		QCOMPARE(run(QStringLiteral("window.get_state")).result.value(QStringLiteral("view")).toString(), QStringLiteral("mix"));

		Engine::getSong()->loadProject(path);
		QCOMPARE(ProjectInfo::instance().documentType(), QStringLiteral("production"));
		QCOMPARE(run(QStringLiteral("window.get_state")).result.value(QStringLiteral("view")).toString(), QStringLiteral("arrange"));
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

	/*! Q050-308: Project tools > Sync > Follow MIDI Clock... must open the clock.slave_set FORM, and that form
	 *  must carry the two arguments the docs point a reader at - source_port and the integer drift_bound_ms
	 *  (0-100 ms) - so the documented route is a real GUI path, not the command described as if it were one.
	 *  Drives the menu ACTION (trigger), then the form's own Run, and reads the value back through
	 *  clock.get_state: one command serves the form and the agent, which is the claim the docs make. */
	void theSyncMenuOpensTheFollowClockForm()
	{
		QMenu menu;
		LibraryCommandMenus::populateProject(&menu);
		QAction* follow = lmms::test::menuAction(&menu, {QStringLiteral("Sync"), QStringLiteral("Follow MIDI Clock...")});
		QVERIFY2(follow != nullptr, "the Sync submenu must offer Follow MIDI Clock...");
		QCOMPARE(follow->property("controlCommand").toString(), QStringLiteral("clock.slave_set"));

		follow->trigger();  // the menu entry, not the command: it must open the form
		CommandForm* form = nullptr;
		for (QWidget* widget : QApplication::topLevelWidgets())
		{
			if (auto* candidate = qobject_cast<CommandForm*>(widget); candidate != nullptr && candidate->isVisible())
			{
				form = candidate;
			}
		}
		QVERIFY2(form != nullptr, "Follow MIDI Clock... opened no form");
		QVERIFY2(form->fields().contains(QStringLiteral("source_port")), "the form has no source_port field");
		QVERIFY2(form->fields().contains(QStringLiteral("drift_bound_ms")), "the form has no drift_bound_ms field");
		QVERIFY2(form->fields().contains(QStringLiteral("enabled")), "the form has no enabled field");

		// The two fields the docs name, set the way a form sets them, then Run.
		QVERIFY(form->setField(QStringLiteral("enabled"), true));
		QVERIFY(form->setField(QStringLiteral("source_port"), QStringLiteral("128:0 Sequencer:Ableton Push")));
		QVERIFY(form->setField(QStringLiteral("drift_bound_ms"), 40));
		form->accept();
		QCoreApplication::processEvents();

		// The same registered command an agent calls: clock.get_state reports both values back.
		const QJsonObject slave = run(QStringLiteral("clock.get_state"))
			.result.value(QStringLiteral("slave")).toObject();
		QCOMPARE(slave.value(QStringLiteral("source_port")).toString(),
			QStringLiteral("128:0 Sequencer:Ableton Push"));
		QCOMPARE(slave.value(QStringLiteral("drift_bound_ms")).toDouble(), 40.0);
		QVERIFY(slave.value(QStringLiteral("enabled")).toBool());

		// One form Run is one recorded undo step: the configuration before the form comes back.
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		const QJsonObject after = run(QStringLiteral("clock.get_state"))
			.result.value(QStringLiteral("slave")).toObject();
		QCOMPARE(after.value(QStringLiteral("source_port")).toString(), QString());
		QCOMPARE(after.value(QStringLiteral("drift_bound_ms")).toDouble(), 10.0);  // MidiClock's DefaultDriftBoundMs
		QVERIFY(run(QStringLiteral("clock.slave_set"), {{QStringLiteral("enabled"), false}}).ok);
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

	void enterOnAMissingProjectAssetDoesNotTryToLoadIt()
	{
		const QString gone = m_dir.filePath(QStringLiteral("enter-gone.wav"));
		QVERIFY(!QFileInfo::exists(gone));
		BrowserSearchView view(nullptr);
		// Project Media lists a missing reference with the kind "missing" (BrowserSearchView::showProjectMedia).
		QTreeWidgetItem* row = browserresults::rowOf({{QStringLiteral("name"), QFileInfo(gone).fileName()},
			{QStringLiteral("kind"), QStringLiteral("missing")}, {QStringLiteral("path"), gone},
			{QStringLiteral("size_bytes"), qint64(-1)}, {QStringLiteral("modified_ms"), qint64(-1)}});
		view.results()->addTopLevelItem(row);
		view.results()->setCurrentItem(row);
		Song* song = Engine::getSong();
		const std::size_t tracks = song->tracks().size();
		QSignalSpy failures(view.actions(), &BrowserResultActions::failed);
		QTest::keyClick(view.results(), Qt::Key_Return);  // Enter: nothing to load, so no "not a file" load failure
		QCOMPARE(failures.count(), 0);
		QTest::keyClick(view.results(), Qt::Key_Return, Qt::ShiftModifier);  // Shift+Enter: no sample track either
		QCOMPARE(failures.count(), 0);
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

	//! R-workflow-4: the Search file metadata toggle sends browser.query's 'probe'
	//! and the SAME search then matches the embedded title, the embedded artist and
	//! the numeric audio metadata - each a word that appears in NO file name.
	void theMetadataToggleSendsProbeAndFindsEmbeddedMetadata()
	{
		BrowserPanel panel;
		auto* search = new BrowserSearchView(&panel);
		panel.addSource(search);
		QVERIFY(restrictToPrivateSamples(search));
		QCheckBox* probe = metadataToggle(search);
		QVERIFY(probe != nullptr);
		QVERIFY(!probe->isChecked());

		// Without the toggle, "drum" is in neither kick_48k.wav's name nor its path.
		QTest::keyClicks(panel.searchField(), QStringLiteral("drum"));
		QTRY_COMPARE(lastHistoryEntry().value(QStringLiteral("label")).toString(), QStringLiteral("drum"));
		QCOMPARE(search->resultCount(), 0);
		QVERIFY(!lastHistoryEntry().value(QStringLiteral("args")).toObject().contains(QStringLiteral("probe")));

		// The toggle: the same text now matches the embedded title ("Kick Drum")
		// and the embedded artist ("Drumsmith"), and the query carries probe: true.
		probe->setChecked(true);
		QCOMPARE(search->resultCount(), 2);
		QVERIFY(resultsList(search, m_kick));
		QVERIFY(resultsList(search, m_pad));
		const QJsonObject sent = lastHistoryEntry().value(QStringLiteral("args")).toObject();
		QCOMPARE(sent.value(QStringLiteral("probe")).toBool(), true);
		QCOMPARE(lastHistoryEntry().value(QStringLiteral("command")).toString(), QStringLiteral("browser.query"));

		// The embedded artist alone: "drumsmith" is in no file name or path.
		panel.searchField()->clear();
		QTest::keyClicks(panel.searchField(), QStringLiteral("drumsmith"));
		QTRY_COMPARE(lastHistoryEntry().value(QStringLiteral("label")).toString(), QStringLiteral("drumsmith"));
		QCOMPARE(search->resultCount(), 1);
		QVERIFY(resultsList(search, m_pad));

		// Numeric audio metadata: the sample rate (22222) and the frame count (3333).
		panel.searchField()->clear();
		QTest::keyClicks(panel.searchField(), QStringLiteral("22222"));
		QTRY_COMPARE(lastHistoryEntry().value(QStringLiteral("label")).toString(), QStringLiteral("22222"));
		QCOMPARE(search->resultCount(), 1);
		QVERIFY(resultsList(search, m_pad));
		panel.searchField()->clear();
		QTest::keyClicks(panel.searchField(), QStringLiteral("3333"));
		QTRY_COMPARE(lastHistoryEntry().value(QStringLiteral("label")).toString(), QStringLiteral("3333"));
		QCOMPARE(search->resultCount(), 1);
		QVERIFY(resultsList(search, m_pad));

		// The toggle off again: the same number matches nothing the names can answer.
		probe->setChecked(false);
		QCOMPARE(search->resultCount(), 0);
		QVERIFY(!lastHistoryEntry().value(QStringLiteral("args")).toObject().contains(QStringLiteral("probe")));
	}

	//! R-workflow-4: a history-restored search restores the toggle with it - the
	//! probed entry comes back probed, the plain one plain.
	void theMetadataToggleFollowsTheHistoryEntries()
	{
		BrowserPanel panel;
		auto* search = new BrowserSearchView(&panel);
		panel.addSource(search);
		QVERIFY(restrictToPrivateSamples(search));
		QCheckBox* probe = metadataToggle(search);
		QVERIFY(probe != nullptr);

		QTest::keyClicks(panel.searchField(), QStringLiteral("drum"));
		QTRY_COMPARE(lastHistoryEntry().value(QStringLiteral("label")).toString(), QStringLiteral("drum"));
		QVERIFY(!probe->isChecked());
		QCOMPARE(search->resultCount(), 0);

		probe->setChecked(true);  // the probed search: a new history entry
		QVERIFY(probe->isChecked());
		QCOMPARE(search->resultCount(), 2);

		search->goHistory(-1);  // back to the unprobed entry: the toggle comes back off
		QVERIFY(!probe->isChecked());
		QCOMPARE(search->resultCount(), 0);

		search->goHistory(1);  // forward to the probed one: on again
		QVERIFY(probe->isChecked());
		QCOMPARE(search->resultCount(), 2);

		// The index holds no embedded metadata: Whole library disables the toggle
		// and the query it runs sends no probe (browser.index_query takes none).
		QCheckBox* indexed = search->findChild<QCheckBox*>(QStringLiteral("browserSearchIndexed"));
		QVERIFY(indexed != nullptr);
		indexed->setChecked(true);
		QVERIFY(!probe->isEnabled());
		QVERIFY(!lastHistoryEntry().value(QStringLiteral("args")).toObject().contains(QStringLiteral("probe")));
		indexed->setChecked(false);
		QVERIFY(probe->isEnabled());
	}

	//! R-workflow-4: tag: stays a plain expression in the Search field - it needs
	//! no probe, sends none, and the placeholder still teaches it. The store is
	//! emptied in memory after the tag is written, so only a re-read of its file
	//! (which browser.query now performs) can answer the search: a fresh session
	//! is the rule, not the accident.
	void tagSearchStaysInTheSearchField()
	{
		QVERIFY(run(QStringLiteral("browser.tag.add"),
			{{QStringLiteral("path"), m_kick}, {QStringLiteral("tag"), QStringLiteral("drums")}}).ok);
		BrowserTagStore::instance().clear();

		BrowserPanel panel;
		auto* search = new BrowserSearchView(&panel);
		panel.addSource(search);
		QVERIFY(restrictToPrivateSamples(search));
		QVERIFY(!metadataToggle(search)->isChecked());
		QVERIFY(panel.searchField()->placeholderText().contains(QStringLiteral("tag:drums")));

		QTest::keyClicks(panel.searchField(), QStringLiteral("tag:drums"));
		QTRY_COMPARE(lastHistoryEntry().value(QStringLiteral("label")).toString(), QStringLiteral("tag:drums"));
		QCOMPARE(search->resultCount(), 1);
		QVERIFY(resultsList(search, m_kick));
		const QJsonObject args = lastHistoryEntry().value(QStringLiteral("args")).toObject();
		QVERIFY(!args.contains(QStringLiteral("probe")));
		QCOMPARE(args.value(QStringLiteral("text")).toString(), QStringLiteral("tag:drums"));

		QVERIFY(run(QStringLiteral("browser.tag.remove"),
			{{QStringLiteral("path"), m_kick}, {QStringLiteral("tag"), QStringLiteral("drums")}}).ok);
	}

	//! R-workflow-4: the probe spend through the GUI never exceeds the probe_limit
	//! the command reports - a directory holding more audio files than the budget
	//! opens exactly probe_limit of them and says so.
	void metadataProbingStaysWithinTheCommandsProbeLimit()
	{
		m_flood = QDir(samplesRoot()).filePath(QStringLiteral("flood"));
		QVERIFY(QDir().mkpath(m_flood));
		WaveSpec quiet;
		quiet.title = QString();  // no embedded title: nothing here matches "zzznothing"
		quiet.frames = 64;
		quiet.burstStartFrame = 0;
		quiet.burstFrames = 8;
		for (int i = 0; i <= BrowserMaxProbes; ++i)
		{
			const QString name = QStringLiteral("f%1.wav").arg(i, 3, 10, QLatin1Char('0'));
			QVERIFY2(writeWave(QDir(m_flood).filePath(name), quiet), qPrintable(name));
		}

		BrowserPanel panel;
		auto* search = new BrowserSearchView(&panel);
		panel.addSource(search);
		QVERIFY(restrictToPrivateSamples(search));
		metadataToggle(search)->setChecked(true);
		QTest::keyClicks(panel.searchField(), QStringLiteral("zzznothing"));
		QTRY_COMPARE(lastHistoryEntry().value(QStringLiteral("label")).toString(), QStringLiteral("zzznothing"));

		// The browse answer the GUI showed, read back through the history step the
		// Back button uses: the probe stops AT the limit and the limit is reported.
		const ControlResult again = run(QStringLiteral("browser.history_go"), {{QStringLiteral("step"), 0}});
		QVERIFY2(again.ok, qPrintable(again.errorMessage));
		QCOMPARE(again.result.value(QStringLiteral("probe")).toBool(), true);
		QCOMPARE(again.result.value(QStringLiteral("probe_limit")).toInt(), BrowserMaxProbes);
		const int probed = again.result.value(QStringLiteral("probed")).toInt();
		QCOMPARE(probed, BrowserMaxProbes);  // the budget is spent, never exceeded
		QVERIFY(probed <= again.result.value(QStringLiteral("probe_limit")).toInt());
		QCOMPARE(search->resultCount(), 0);  // and nothing was found, so the probes were honest
	}

	void cleanup()
	{
		if (!m_flood.isEmpty()) { QDir(m_flood).removeRecursively(); m_flood.clear(); }
	}

	//! The category facet in the Search view (0.5.0 L11, WF-007): the filter
	//! sits beside Kind and Where to look, its choices are the catalog's own
	//! categories plus "Any category", and selecting one narrows the shown
	//! list in BOTH query modes - walking the folders and over the whole-library
	//! index - while "Any category" restores the full result set.
	void theCategoryFilterSitsBesideKindAndSourceAndNarrowsBothQueryModes()
	{
		const QString samples = ConfigManager::inst()->userSamplesDir();
		QVERIFY(QDir().mkpath(samples));
		const QString kick = touchFile(samples + QStringLiteral("/catfacet-kick.wav"));
		const QString snare = touchFile(samples + QStringLiteral("/catfacet-snare.wav"));
		QVERIFY(run(QStringLiteral("browser.category.set"),
			{{QStringLiteral("path"), kick}, {QStringLiteral("category"), QStringLiteral("Drums")}}).ok);
		QVERIFY(run(QStringLiteral("browser.category.set"),
			{{QStringLiteral("path"), snare}, {QStringLiteral("category"), QStringLiteral("Loops")}}).ok);
		// The index this test queries is its own (ZENE_BROWSER_INDEX above), built
		// over this fixture folder so the indexed mode answers about these files.
		QVERIFY2(run(QStringLiteral("browser.reindex"),
			{{QStringLiteral("path"), m_dir.path()}, {QStringLiteral("features"), false},
				{QStringLiteral("wait"), true}}).ok, "reindex refused");

		BrowserSearchView view(nullptr);
		QComboBox* category = view.categoryFilter();
		QVERIFY2(category != nullptr, "the Search view exposes no category filter");
		QCOMPARE(category->accessibleName(), QStringLiteral("Category"));

		// Beside Kind and Where to look: the same row, adjacent in that order.
		QVERIFY(category->parentWidget() != nullptr);
		QLayout* column = category->parentWidget()->layout();
		QVERIFY(column != nullptr);
		QLayout* row = nullptr;
		for (int i = 0; i < column->count() && row == nullptr; ++i)
		{
			QLayout* candidate = column->itemAt(i)->layout();
			if (candidate == nullptr) { continue; }
			for (int j = 0; j < candidate->count(); ++j)
			{
				if (candidate->itemAt(j)->widget() == category) { row = candidate; break; }
			}
		}
		QVERIFY2(row != nullptr, "the category filter is not in the top row");
		QStringList facets;
		for (int i = 0; i < row->count(); ++i)
		{
			auto* box = qobject_cast<QComboBox*>(row->itemAt(i)->widget());
			if (box != nullptr) { facets << box->accessibleName(); }
		}
		QCOMPARE(facets, QStringList({QStringLiteral("Kind"), QStringLiteral("Where to look"),
			QStringLiteral("Category")}));

		// The vocabulary is the catalog's own: "Any category" first, then the
		// two categories the assignments above put in the store.
		QCOMPARE(category->count(), 3);
		QCOMPARE(category->itemText(0), QStringLiteral("Any category"));
		QVERIFY(category->itemData(0).toString().isEmpty());
		QVERIFY(category->findData(QStringLiteral("Drums")) > 0);
		QVERIFY(category->findData(QStringLiteral("Loops")) > 0);

		// Direct mode: the query's own text scopes the walk (the folder also
		// carries this test's store, index and collections files).
		auto* field = view.findChild<QLineEdit*>(QStringLiteral("browserSearch"));
		QVERIFY(field != nullptr);
		QTest::keyClicks(field, QStringLiteral("catfacet"));
		QTest::keyClick(field, Qt::Key_Return);
		QCOMPARE(view.resultCount(), 2);  // Any category: both files

		category->setCurrentIndex(category->findData(QStringLiteral("Drums")));
		QCOMPARE(view.resultCount(), 1);
		QCOMPARE(view.results()->topLevelItem(0)->text(browserresults::Name), QStringLiteral("catfacet-kick.wav"));
		category->setCurrentIndex(category->findData(QStringLiteral("Loops")));
		QCOMPARE(view.resultCount(), 1);
		QCOMPARE(view.results()->topLevelItem(0)->text(browserresults::Name), QStringLiteral("catfacet-snare.wav"));
		category->setCurrentIndex(0);  // Any category
		QCOMPARE(view.resultCount(), 2);

		// Indexed mode: the same facet answers the same way over the index.
		QCheckBox* indexed = view.findChild<QCheckBox*>();
		QVERIFY(indexed != nullptr);
		QCOMPARE(indexed->text(), QStringLiteral("Whole library"));
		indexed->setChecked(true);
		QCOMPARE(view.resultCount(), 2);
		category->setCurrentIndex(category->findData(QStringLiteral("Drums")));
		QCOMPARE(view.resultCount(), 1);
		QCOMPARE(view.results()->topLevelItem(0)->text(browserresults::Name), QStringLiteral("catfacet-kick.wav"));
		category->setCurrentIndex(0);  // Any category restores the full set here too
		QCOMPARE(view.resultCount(), 2);
		indexed->setChecked(false);  // back to walking the folders
		QCOMPARE(view.resultCount(), 2);

		// Leave the store without categories for the tests that follow.
		QVERIFY(run(QStringLiteral("browser.category.clear"), {{QStringLiteral("path"), kick}}).ok);
		QVERIFY(run(QStringLiteral("browser.category.clear"), {{QStringLiteral("path"), snare}}).ok);
	}

	//! The GUI regression for the facet (0.5.0 L11, WF-007): a category is
	//! assigned through the Category submenu the Search view's row menu opens,
	//! the view filters to it, the filtered search is saved as a collection
	//! through the view's own button, and running that collection later answers
	//! the same category-filtered question - the category survived the round trip.
	void aCategoryFilteredSearchSavesAndRunsAsACollection()
	{
		const QString samples = ConfigManager::inst()->userSamplesDir();
		QVERIFY(QDir().mkpath(samples));
		const QString kick = touchFile(samples + QStringLiteral("/catfacet-kick.wav"));
		touchFile(samples + QStringLiteral("/catfacet-snare.wav"));

		// 1. Assignment through the GUI half (the same submenu showRowMenu builds).
		setBrowserCategoryPrompt([] { return std::optional<QString>(QStringLiteral("Drums")); });
		{
			QMenu menu;
			addBrowserCategoryMenu(&menu, kick);
			QAction* fresh = submenuAction(menu, QMenu::tr("New category..."));
			QVERIFY2(fresh != nullptr, "the Category submenu offers no New category...");
			fresh->trigger();
		}
		setBrowserCategoryPrompt({});
		QCOMPARE(BrowserTagStore::instance().categoryOf(kick), QStringLiteral("Drums"));

		// 2. The view offers the assigned category and filters to it.
		BrowserSearchView view(nullptr);
		QComboBox* category = view.categoryFilter();
		QVERIFY(category != nullptr);
		QVERIFY2(category->findData(QStringLiteral("Drums")) > 0, "the assigned category is not offered");
		auto* field = view.findChild<QLineEdit*>(QStringLiteral("browserSearch"));
		QVERIFY(field != nullptr);
		QTest::keyClicks(field, QStringLiteral("catfacet"));
		QTest::keyClick(field, Qt::Key_Return);
		QCOMPARE(view.resultCount(), 2);  // Any category: both files
		category->setCurrentIndex(category->findData(QStringLiteral("Drums")));
		QCOMPARE(view.resultCount(), 1);
		QCOMPARE(view.results()->topLevelItem(0)->text(browserresults::Name), QStringLiteral("catfacet-kick.wav"));

		// 3. Save THIS filtered search through the view's own button; the name
		// dialog is answered by a timer, as EffectHostingMenuTest answers dialogs.
		QPushButton* save = nullptr;
		for (QPushButton* candidate : view.findChildren<QPushButton*>())
		{
			if (candidate->property("controlCommand").toString() == QLatin1String("browser.collection_save"))
			{
				save = candidate;
			}
		}
		QVERIFY(save != nullptr);
		QTimer::singleShot(0, [] {
			auto* dialog = qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
			if (dialog == nullptr) { return; }
			dialog->setTextValue(QStringLiteral("Q050 170 Drums"));
			dialog->accept();
		});
		save->click();

		// The collections list beside the results now shows it (the view refreshed).
		QListWidget* collections = nullptr;
		for (QListWidget* candidate : view.findChildren<QListWidget*>())
		{
			if (candidate->accessibleName() == QStringLiteral("Collections")) { collections = candidate; }
		}
		QVERIFY(collections != nullptr);
		bool listed = false;
		for (int i = 0; i < collections->count(); ++i)
		{
			listed = listed || collections->item(i)->text() == QLatin1String("Q050 170 Drums");
		}
		QVERIFY2(listed, "the saved collection is not listed in the view");

		// The stored query carries the category - that is what a later run will answer.
		const QJsonArray saved = run(QStringLiteral("browser.collections")).result
			.value(QStringLiteral("collections")).toArray();
		bool carriesCategory = false;
		for (const QJsonValue& value : saved)
		{
			const QJsonObject entry = value.toObject();
			if (entry.value(QStringLiteral("name")).toString() != QLatin1String("Q050 170 Drums")) { continue; }
			carriesCategory = entry.value(QStringLiteral("query")).toObject()
				.value(QStringLiteral("category")).toString() == QLatin1String("Drums");
		}
		QVERIFY2(carriesCategory, "the saved collection does not carry its category");

		// 4. Running it later from the list (the click the user makes) shows the
		// same category-filtered result set.
		for (int i = 0; i < collections->count(); ++i)
		{
			if (collections->item(i)->text() != QLatin1String("Q050 170 Drums")) { continue; }
			emit collections->itemClicked(collections->item(i));
		}
		QCOMPARE(view.resultCount(), 1);
		QCOMPARE(view.results()->topLevelItem(0)->text(browserresults::Name), QStringLiteral("catfacet-kick.wav"));

		QVERIFY(run(QStringLiteral("browser.collection_remove"),
			{{QStringLiteral("name"), QStringLiteral("Q050 170 Drums")}}).ok);
		QVERIFY(run(QStringLiteral("browser.category.clear"), {{QStringLiteral("path"), kick}}).ok);
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

	void theScanProgressDoesNotHideTheSearchResult()  // cluster new-p12-342
	{
		BrowserPanel panel;
		auto* search = new BrowserSearchView(&panel);
		panel.addSource(search);
		panel.searchField()->setText(QStringLiteral("zq-no-such-sound-4711"));
		search->runSearch();  // a search writes the status line
		const QString afterSearch = search->statusText();
		QVERIFY(!afterSearch.isEmpty());
		search->refreshIndexStatus();  // the scan's state, polled while a scan runs
		QCOMPARE(search->statusText(), afterSearch);  // the search's own count is still there
		QVERIFY(!search->scanStatusText().isEmpty());  // the scan has a line of its own
	}

private:
	QString samplesRoot() const { return ConfigManager::inst()->userSamplesDir(); }

	QTemporaryDir m_dir;
	QString m_kick;
	QString m_pad;
	QString m_flood;
};

QTEST_MAIN(LibraryMenusTest)
#include "LibraryMenusTest.moc"
