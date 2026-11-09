/*
 * GrooveMenuTest.cpp - the piano roll's Groove menu extracts, applies and quantises
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

/*! Over a real clip of swung notes: Extract groove... puts a named groove in the pool
 *  (groove.list lists it), the rebuilt menu offers it under Apply groove and applying it to a
 *  straight clip at 100 % moves notes, and Quantize with strength... at 100 % puts a late note back
 *  on the grid - each through its groove.* command. A command that REFUSES (an empty clip carries
 *  no note a groove can be measured from) says so: the refusal is shown to the person who asked
 *  for it, not swallowed. The test runs the REAL application, because the refusal is shown the
 *  way the interface shows it - as a floating message over the main window. */

#include <QtTest>

#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QRect>
#include <QTemporaryDir>

#include "CommandForm.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "GrooveMenu.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "InstrumentTrack.h"
#include "MainWindow.h"
#include "MidiClip.h"
#include "Note.h"
#include "Song.h"
#include "TextFloat.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

QAction* named(QMenu* menu, const QString& text)
{
	for (QAction* action : menu->actions())
	{
		if (action->text() == text) { return action; }
		if (action->menu() != nullptr)
		{
			if (QAction* inner = named(action->menu(), text)) { return inner; }
		}
	}
	return nullptr;
}

MidiClip* clipWith(const std::vector<int>& positions)
{
	auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
	auto* clip = dynamic_cast<MidiClip*>(track->createClip(TimePos(0)));
	for (int position : positions) { clip->addNote(Note(TimePos(6), TimePos(position), 60, 100), false); }
	return clip;
}

std::vector<int> positionsOf(const MidiClip* clip)
{
	std::vector<int> out;
	for (const Note* note : clip->notes()) { out.push_back(note->pos().getTicks()); }
	return out;
}

/*! What the floating messages over the main window say - one label's text per line.
 *  They are how the interface tells a person what a command answered, so they are
 *  what the test reads to know the refusal was not swallowed. */
QString messagesShown()
{
	QString text;
	for (TextFloat* message : getGUI()->mainWindow()->findChildren<TextFloat*>())
	{
		for (QLabel* label : message->findChildren<QLabel*>()) { text += label->text() + QLatin1Char('\n'); }
	}
	return text;
}

//! Where the window a floating message floats over sits (the message's own place is its frameGeometry()).
QRect globalRect(const QWidget* widget) { return QRect(widget->mapToGlobal(QPoint(0, 0)), widget->size()); }

//! The first floating message over the main window (they are children of it).
TextFloat* firstMessage() { return getGUI()->mainWindow()->findChild<TextFloat*>(); }

} // namespace

class GrooveMenuTest : public QObject
{
	Q_OBJECT

private:
	QTemporaryDir m_home;

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		guitest::startGui(m_home);
		ControlRegistry::setReady(true);
		// The offscreen platform IS a display here (the platform plugin answers "offscreen",
		// which is a display a person cannot click on but a message can be shown over), so
		// the registry is told there is somebody to show a refusal to.
		ControlRegistry::instance()->setHeadless(false);
	}

	void cleanupTestCase()
	{
		setGroovePrompts({});
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void extractApplyAndQuantize()
	{
		// Swung sixteenths: every second one late by 4 ticks.
		MidiClip* swung = clipWith({0, 16, 24, 40, 48, 64, 72, 88});
		setGroovePrompts({[] { return std::optional<QString>(QStringLiteral("Swing")); },
			[](const QString&) { return std::optional<double>(100.0); }});
		const MidiClip* current = swung;
		QMenu* menu = makeGrooveMenu([&current] { return current; }, [] { return 12; }, nullptr);
		named(menu, QMenu::tr("Extract groove from this clip..."))->trigger();
		const QJsonArray pool = ControlRegistry::instance()->invoke(QStringLiteral("groove.list"), QJsonObject{})
			.result.value(QStringLiteral("templates")).toArray();
		QCOMPARE(pool.size(), 1);

		MidiClip* straight = clipWith({0, 12, 24, 36, 48, 60, 72, 84});
		current = straight;
		emit menu->aboutToShow();
		QAction* swing = named(menu, QStringLiteral("Swing"));
		QVERIFY2(swing != nullptr, "the pooled groove is not under Apply groove");
		const auto before = positionsOf(straight);
		swing->trigger();
		QVERIFY2(positionsOf(straight) != before, "applying the groove moved no note");

		MidiClip* late = clipWith({0, 15, 24});
		current = late;
		emit menu->aboutToShow();
		named(menu, QMenu::tr("Quantize with strength..."))->trigger();
		QCOMPARE(positionsOf(late), (std::vector<int>{0, 12, 24}));
		delete menu;
	}

	void aRefusedCommandIsShownToTheUser()
	{
		// An empty clip carries no note a groove can be measured from, so groove.extract
		// refuses. The refusal must be shown: the menu closing on a silence is the bug.
		MidiClip* empty = clipWith({});
		setGroovePrompts({[] { return std::optional<QString>(QStringLiteral("Empty")); },
			[](const QString&) { return std::optional<double>(100.0); }});
		const MidiClip* current = empty;
		QMenu* menu = makeGrooveMenu([&current] { return current; }, [] { return 12; }, nullptr);
		named(menu, QMenu::tr("Extract groove from this clip..."))->trigger();
		const QString shown = messagesShown();
		QVERIFY2(!shown.isEmpty(), "the refused groove.extract was shown to nobody");
		QVERIFY2(shown.contains(QStringLiteral("groove")), qPrintable(shown));
		delete menu;
	}

	//! gui-1105: a command's result floated over the main window (Window > Channel Load) was put at the window's
	//! right edge + 2 px - outside the app, so nobody saw it. It must lie over the window it floats over.
	void aResultFloatedOverTheMainWindowLiesOverIt()
	{
		MainWindow* window = getGUI()->mainWindow();
		window->resize(900, 600);
		const QRect app = globalRect(window);
		const TextFloat* message = TextFloat::displayMessage(QStringLiteral("Channel Load"), QStringLiteral("CPU 3 %"),
			QPixmap(), 8000, window);
		QVERIFY(message != nullptr);
		QVERIFY2(app.contains(message->frameGeometry()), qPrintable(QStringLiteral("float %1,%2 %3x%4 is not inside the window %5,%6 %7x%8")
			.arg(message->frameGeometry().x()).arg(message->frameGeometry().y()).arg(message->frameGeometry().width()).arg(message->frameGeometry().height())
			.arg(app.x()).arg(app.y()).arg(app.width()).arg(app.height())));
		delete message;
	}

	//! The same for a message beside a widget that is flush with the window's right edge, and the beside-a-widget
	//! placement is kept where it already lies inside the window.
	void aMessageBesideAWidgetStaysOverTheWindow()
	{
		MainWindow* window = getGUI()->mainWindow();
		window->resize(900, 600);
		const QRect app = globalRect(window);
		QWidget flush(window);
		flush.setGeometry(window->width() - 100, 40, 100, 50);
		const TextFloat* edge = TextFloat::displayMessage(QStringLiteral("t"), QStringLiteral("m"), QPixmap(), 8000, &flush);
		QVERIFY2(app.contains(edge->frameGeometry()), "a message beside a widget at the right edge is outside the window");
		delete edge;

		QWidget inner(window);
		inner.setGeometry(50, 40, 100, 50);
		const TextFloat* beside = TextFloat::displayMessage(QStringLiteral("t"), QStringLiteral("m"), QPixmap(), 8000, &inner);
		QCOMPARE(beside->pos(), inner.mapToGlobal(QPoint(inner.width() + 2, 0)));
		delete beside;
	}

	//! The user's path: Window > Channel Load runs mixer.cpu_load through the menu's host (the main window) with its
	//! result to be shown (WorkspaceMenus.cpp fillWindowMenu) - the readout it floats must lie inside the app.
	void channelLoadFromTheWindowMenuShowsInsideTheApp()
	{
		MainWindow* window = getGUI()->mainWindow();
		window->resize(900, 600);
		qDeleteAll(window->findChildren<TextFloat*>());
		const ControlResult result = runCommand(window, CommandCall{QStringLiteral("mixer.cpu_load"), {}, {}, QString(), false, true});
		QVERIFY2(result.ok, qPrintable(result.errorMessage));
		const TextFloat* message = firstMessage();
		QVERIFY2(message != nullptr, "Channel Load showed no readout");
		QVERIFY2(globalRect(window).contains(message->frameGeometry()), "the Channel Load readout lies outside the app");
	}
};

QTEST_MAIN(GrooveMenuTest)
#include "GrooveMenuTest.moc"
