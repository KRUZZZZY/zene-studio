/*
 * ItemListButtonsTest.cpp - the List buttons on the canvases' toolbars
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
 *  editable item list - the note area, the controller lane, the arranger's clips, the XY pad and the take lanes' comp
 *  strip - carries a List button that runs window.item_list on that canvas, opening its list and closing it again. */

#include <QtTest>

#include <memory>
#include <vector>

#include <QTemporaryDir>
#include <QToolButton>

#include "CompSwipeStrip.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "ItemList.h"
#include "Mixer.h"
#include "NoteCanvas.h"
#include "NoteEditor.h"
#include "SongEditor.h"
#include "TakeLanesPanel.h"
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

private:
	QTemporaryDir m_home;
};

QTEST_MAIN(ItemListButtonsTest)
#include "ItemListButtonsTest.moc"
