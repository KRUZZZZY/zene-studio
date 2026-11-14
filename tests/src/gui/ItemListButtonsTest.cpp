/*
 * ItemListButtonsTest.cpp - the List buttons on the canvases' toolbars, and View > Screen Reader Announcements
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

/*! In the real application (themes 10 and 17, the two gaps KNOWN-LIMITATIONS listed): every canvas that has an
 *  editable item list - the note area, the controller lane, the arranger's clips, the automation editor's points, the
 *  XY pad and the take lanes' comp strip - carries a List button that runs window.item_list on that canvas, opening
 *  its list and closing it again, the focus returning to the canvas;
 *  and the View menu sets view.announcements, its three levels checkable, the current one checked. */

#include <QtTest>

#include <memory>
#include <vector>

#include <QApplication>
#include <QMenuBar>
#include <QTableView>
#include <QTemporaryDir>
#include <QToolButton>

#include "AccessibleWidgets.h"
#include "AutomationClip.h"
#include "AutomationEditor.h"
#include "AutomationTrack.h"
#include "CompSwipeStrip.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "InstrumentTrack.h"
#include "ItemList.h"
#include "MainWindow.h"
#include "Mixer.h"
#include "NoteCanvas.h"
#include "NoteEditor.h"
#include "Song.h"
#include "SongEditor.h"
#include "TakeLanesPanel.h"
#include "Track.h"
#include "XyPad.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

ControlResult invoke(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! The List button for @a surface somewhere under @a root, or nullptr.
QToolButton* listButton(QWidget* root, const QString& surface)
{
	for (QToolButton* button : root->findChildren<QToolButton*>())
	{
		if (button->property("controlCommand").toString() == QLatin1String("window.item_list")
			&& button->property("itemListSurface").toString() == surface)
		{
			return button;
		}
	}
	return nullptr;
}

bool listShown(QWidget* host, const QString& surface)
{
	const ItemListPanel* panel = ItemListPanel::find(host, surface);
	return panel != nullptr && panel->isVisible();
}

//! Click @a surface's List button under @a root twice: the list of @a host opens, then closes.
void clickOpensAndCloses(QWidget* root, QWidget* host, const QString& surface)
{
	QToolButton* button = listButton(root, surface);
	QVERIFY2(button != nullptr, qPrintable(QStringLiteral("no List button for ") + surface));
	QVERIFY2(!button->accessibleName().isEmpty(), qPrintable(surface));
	QVERIFY2(!button->icon().isNull(), qPrintable(surface));
	QVERIFY(!listShown(host, surface));
	button->click();
	QVERIFY2(listShown(host, surface), qPrintable(surface + QStringLiteral(": the list did not open")));
	button->click();  // as Ctrl+Shift+L: a second press closes it
	QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
	QVERIFY2(!listShown(host, surface), qPrintable(surface + QStringLiteral(": the list did not close")));
}

QMenu* viewMenu()
{
	for (QAction* top : getGUI()->mainWindow()->menuBar()->actions())
	{
		if (top->menu() != nullptr && top->text().remove(QLatin1Char('&')) == QLatin1String("View")) { return top->menu(); }
	}
	return nullptr;
}

QMenu* announcementsMenu(QMenu* view)
{
	for (QAction* action : view->actions())
	{
		if (action->menu() != nullptr && action->menu()->objectName() == QLatin1String("announcementsMenu")) { return action->menu(); }
	}
	return nullptr;
}

//! The level whose entry is checked, or "" when none or several are.
QString checkedLevel(QMenu* menu)
{
	QStringList checked;
	for (QAction* action : menu->actions())
	{
		if (action->isChecked()) { checked << action->data().toString(); }
	}
	return checked.size() == 1 ? checked.first() : QString();
}

} // namespace

class ItemListButtonsTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		guitest::startGui(m_home);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void theNoteEditorsToolbarListsTheNotes()
	{
		NoteEditor* editor = getGUI()->noteEditor();
		QVERIFY(editor != nullptr);
		QWidget* toolbar = editor->findChild<QWidget*>(QStringLiteral("noteEditorToolbar"));
		QVERIFY(toolbar != nullptr);
		QVERIFY2(listButton(toolbar, QStringLiteral("notes")) != nullptr, "the List button is not on the toolbar");
		clickOpensAndCloses(editor, editor->canvas(), QStringLiteral("notes"));
	}

	void theControllerLaneHasItsOwnListButton()
	{
		NoteEditor* editor = getGUI()->noteEditor();
		QToolButton* button = listButton(editor, QStringLiteral("controller"));
		QVERIFY2(button != nullptr, "no List button for the controller lane");
		QVERIFY2(button->isHidden(), "shown while the lane shows a note expression, not a controller");
		editor->setControllerLane(QStringLiteral("cc:7"));
		QVERIFY(!button->isHidden());
		QWidget* lane = editor->findChild<QWidget*>(QStringLiteral("noteControllerLane"));
		QVERIFY(lane != nullptr);
		clickOpensAndCloses(editor, lane, QStringLiteral("controller"));
		editor->setControllerLane(QString());
		QVERIFY(button->isHidden());
	}

	void theArrangersToolbarListsTheClips()
	{
		SongEditorWindow* window = getGUI()->songEditor();
		QVERIFY(window != nullptr);
		clickOpensAndCloses(window, window->m_editor, QStringLiteral("clips"));
	}

	void theXyPadListsItsAxes()
	{
		while (Engine::mixer()->numChannels() <= 1) { Engine::mixer()->createChannel(); }
		XyPadDialog pad(control::channelIdOf(Engine::mixer()->mixerChannel(1)));
		pad.show();
		QVERIFY(QTest::qWaitForWindowExposed(&pad));
		clickOpensAndCloses(&pad, &pad, QStringLiteral("xy"));
	}

	void theTakeLanesListTheirComp()
	{
		const QString id = invoke(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("sample")}})
			.result.value(QStringLiteral("track")).toString();
		QVERIFY(!id.isEmpty());
		TakeLanesPanel panel(id);
		panel.show();
		QVERIFY(QTest::qWaitForWindowExposed(&panel));
		auto* strip = panel.findChild<CompSwipeStrip*>();
		QVERIFY(strip != nullptr);
		// listButton() matches on the BUTTON's controlCommand: TakeLanesPanelTest's rule that every button on the panel
		// declares the command it runs holds for this one too.
		clickOpensAndCloses(&panel, strip, QStringLiteral("comp"));
	}

	void eachTakeLanesPanelListsItsOwnComp()
	{
		// Two panels open: a press lists the strip of the panel it was pressed on, whichever panel was active before.
		// (The strip is no ancestor of the panel's other widgets, so the button must hand it the focus first.)
		std::vector<std::unique_ptr<TakeLanesPanel>> panels;
		for (int i = 0; i < 2; ++i)
		{
			const QString id = invoke(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("sample")}})
				.result.value(QStringLiteral("track")).toString();
			panels.push_back(std::make_unique<TakeLanesPanel>(id));
			panels.back()->show();
			QVERIFY(QTest::qWaitForWindowExposed(panels.back().get()));
		}
		for (std::size_t pressed = 0; pressed < panels.size(); ++pressed)
		{
			TakeLanesPanel* panel = panels[pressed].get();
			panel->activateWindow();  // what the press itself does to its window
			if (!QTest::qWaitForWindowActive(panel)) { QSKIP("this platform does not activate windows"); }
			listButton(panel, QStringLiteral("comp"))->click();
			for (std::size_t other = 0; other < panels.size(); ++other)
			{
				auto* strip = panels[other]->findChild<CompSwipeStrip*>();
				QCOMPARE(listShown(strip, QStringLiteral("comp")), other == pressed);
			}
			listButton(panel, QStringLiteral("comp"))->click();
			QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
		}
	}

	void theAutomationToolbarListsThePoints()
	{
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		auto* automation = dynamic_cast<AutomationTrack*>(Track::create(Track::Type::Automation, Engine::getSong()));
		QVERIFY(track != nullptr && automation != nullptr);
		auto* clip = dynamic_cast<AutomationClip*>(automation->createClip(TimePos(0)));
		QVERIFY(clip != nullptr);
		clip->addObject(track->volumeModel(), false);  // seeds one point at 0
		clip->putValue(TimePos(48), 40.f, false);

		getGUI()->automationEditor()->open(clip);
		AutomationEditorWindow* window = getGUI()->automationEditor();
		QVERIFY(window != nullptr);
		clickOpensAndCloses(window, window->m_editor, QStringLiteral("automation"));

		getGUI()->mainWindow()->show();
		QVERIFY(QTest::qWaitForWindowExposed(getGUI()->mainWindow()));
		window->activateWindow();
		if (!QTest::qWaitForWindowActive(window->window())) { QSKIP("this platform does not activate windows"); }
		QToolButton* button = listButton(window, QStringLiteral("automation"));
		QVERIFY(button != nullptr);
		auto focusIsOnTheCanvas = [](QWidget* canvas) {
			QWidget* focus = QApplication::focusWidget();
			return focus != nullptr && (focus == canvas || canvas->isAncestorOf(focus));
		};

		// Escape closes the list and the keyboard focus returns to the automation editor.
		button->setFocus();
		QTest::keyClick(button, Qt::Key_Space);
		QVERIFY2(listShown(window->m_editor, QStringLiteral("automation")), "Space on the button did not open the list");
		ItemListPanel* panel = ItemListPanel::find(window->m_editor, QStringLiteral("automation"));
		QVERIFY(panel != nullptr);
		QTest::keyClick(panel->table(), Qt::Key_Escape);
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
		QVERIFY(!listShown(window->m_editor, QStringLiteral("automation")));
		QTRY_VERIFY(focusIsOnTheCanvas(window->m_editor));

		// A second Space on the button closes it too, with the same focus return.
		button->setFocus();
		QTest::keyClick(button, Qt::Key_Space);
		QVERIFY2(listShown(window->m_editor, QStringLiteral("automation")), "Space on the button did not open the list");
		QTest::keyClick(button, Qt::Key_Space);
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
		QVERIFY(!listShown(window->m_editor, QStringLiteral("automation")));
		QTRY_VERIFY(focusIsOnTheCanvas(window->m_editor));

		// The agent's command and the button work on the same list.
		button->click();
		QVERIFY(listShown(window->m_editor, QStringLiteral("automation")));
		QVERIFY(invoke(QStringLiteral("window.item_list"), {{QStringLiteral("surface"), QStringLiteral("automation")}}).ok);
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
		QVERIFY(!listShown(window->m_editor, QStringLiteral("automation")));

		// No clip: the window's own rule disables the button, and the command refuses the surface.
		window->m_editor->setCurrentClip(nullptr);
		QVERIFY(!button->isEnabled());
		QCOMPARE(invoke(QStringLiteral("window.item_list"), {{QStringLiteral("surface"), QStringLiteral("automation")}}).errorKind,
			ControlErrorKind::NotFound);
		QCOMPARE(invoke(QStringLiteral("window.item_list"), {{QStringLiteral("surface"), QStringLiteral("bogus")}}).errorKind,
			ControlErrorKind::InvalidArgs);
		window->m_editor->setCurrentClip(clip);
	}

	void theViewMenuSetsTheAnnouncementLevel()
	{
		QMenu* view = viewMenu();
		QVERIFY(view != nullptr);
		emit view->aboutToShow();  // MainWindow rebuilds the View menu as it opens
		QMenu* menu = announcementsMenu(view);
		QVERIFY2(menu != nullptr, "no Screen Reader Announcements entry in the View menu");
		QCOMPARE(menu->actions().size(), a11y::announceLevelNames().size());
		for (QAction* action : menu->actions())
		{
			QVERIFY(action->isCheckable());
			QVERIFY(action->isEnabled());
			QCOMPARE(action->property("controlCommand").toString(), QStringLiteral("view.announcements"));
		}
		QCOMPARE(checkedLevel(menu), QStringLiteral("all"));  // the default

		QAction* errors = nullptr;
		for (QAction* action : menu->actions())
		{
			if (action->data().toString() == QLatin1String("errors")) { errors = action; }
		}
		QVERIFY(errors != nullptr);
		errors->trigger();
		QCOMPARE(a11y::announceLevel(), a11y::AnnounceLevel::Errors);
		QCOMPARE(checkedLevel(menu), QStringLiteral("errors"));

		// Set elsewhere (the socket, undo), the entry follows when the menu opens again - rebuilt or not.
		QVERIFY(invoke(QStringLiteral("view.announcements"), {{QStringLiteral("level"), QStringLiteral("off")}}).ok);
		emit view->aboutToShow();
		menu = announcementsMenu(view);
		QVERIFY(menu != nullptr);
		emit menu->aboutToShow();
		QCOMPARE(checkedLevel(menu), QStringLiteral("off"));
		QCOMPARE(view->findChildren<QMenu*>(QStringLiteral("announcementsMenu")).size(), 1);  // one menu, not one per opening
		QVERIFY(invoke(QStringLiteral("control.undo")).ok);
		QVERIFY(invoke(QStringLiteral("control.undo")).ok);
		QCOMPARE(a11y::announceLevel(), a11y::AnnounceLevel::All);
		emit menu->aboutToShow();
		QCOMPARE(checkedLevel(menu), QStringLiteral("all"));
	}

private:
	QTemporaryDir m_home;
};

QTEST_MAIN(ItemListButtonsTest)
#include "ItemListButtonsTest.moc"
