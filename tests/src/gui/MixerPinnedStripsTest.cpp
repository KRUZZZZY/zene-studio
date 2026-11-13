/*
 * MixerPinnedStripsTest.cpp - Q050-227.2: pinned mixer strips beside the scrolling channels, and the
 *                             layout the project keeps of them
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
 * You should have received a copy of the GNU General Public License along with
 * this program (see COPYING); if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

/*! A strip pinned from its menu stays put in a pinned area beside the scrolling channels. Three acceptance criteria:
 *   1. a strip is pinned and unpinned from its menu, a resize works, and a pinned strip keeps its place while the
 *      channels scroll;
 *   2. saving the project layout and reopening the project restores the pins, the ids and the widths;
 *   3. the pin command refuses what cannot be pinned, and a layout saved before pins existed opens with none pinned.
 *  Each case drives the GUI the way a person does - a menu entry on a strip, or a real QSplitterHandle drag - and
 *  reads the same state an agent reads through window.mixer_view and window.mixer_zones. */

#include <QtTest>

#include <QAction>
#include <QApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QSplitterHandle>
#include <QTemporaryDir>

#include "CollabCommandTap.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "MainWindow.h"
#include "Mixer.h"
#include "MixerView.h"
#include "Shell.h"
#include "Song.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

QJsonObject sizesOf(const ControlResult& result)
{
	return result.result.value(QStringLiteral("sizes")).toObject();
}

//! The view's half-second sync, now.
void syncMixer()
{
	getGUI()->mixerView()->syncWithMixer();
	getGUI()->mixerView()->syncWithMixer();
}

//! The splitter's index of a region's host, or -1.
int zoneIndex(const QString& region)
{
	MixerView* view = getGUI()->mixerView();
	return view->zonesSplitter()->indexOf(view->zoneWidget(region));
}

//! Drag the separator between widgets @a beforeIndex and @a beforeIndex + 1 by @a dx, through the
//! widget events a real drag is made of: a press inside the handle, a move, and the release.
//! QSplitterHandle reads these in its own coordinates, which is what a person's mouse delivers.
//! NOTE the handle's own number: QSplitter::handle(i) is the handle BEFORE widget i, so the
//! separator after widget i is handle(i + 1).
void dragHandle(int beforeIndex, int dx)
{
	QSplitter* splitter = getGUI()->mixerView()->zonesSplitter();
	QSplitterHandle* handle = splitter->handle(beforeIndex + 1);
	const QPoint start = handle->rect().center();
	const QPoint end = start + QPoint(dx, 0);
	QMouseEvent press(QEvent::MouseButtonPress, QPointF(start), QPointF(handle->mapToGlobal(start)),
		Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
	QApplication::sendEvent(handle, &press);
	QMouseEvent move(QEvent::MouseMove, QPointF(end), QPointF(handle->mapToGlobal(end)),
		Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
	QApplication::sendEvent(handle, &move);
	QMouseEvent release(QEvent::MouseButtonRelease, QPointF(end), QPointF(handle->mapToGlobal(end)),
		Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
	QApplication::sendEvent(handle, &release);
}

//! The ids window.mixer_view names as pinned, in the order its answer lists them (mixer order).
QStringList pinnedIn(const ControlResult& result)
{
	QStringList ids;
	for (const QJsonValue& entry : result.result.value(QStringLiteral("pinned_channels")).toArray())
	{
		ids.append(entry.toString());
	}
	return ids;
}

QStringList pinnedOf()
{
	return pinnedIn(run(QStringLiteral("window.mixer_view")));
}

//! The menu entry that pins or unpins the strip at @a index, or nullptr when the strip has none (the master).
QAction* pinActionFor(int index, QMenu* menu)
{
	getGUI()->mixerView()->populatePinAction(menu, index);
	return menu->findChild<QAction*>(QStringLiteral("pinStripAction"));
}

bool inPinnedArea(int index)
{
	MixerView* view = getGUI()->mixerView();
	return view->pinnedArea()->isAncestorOf(view->channelView(index));
}

//! @a widget's rect in the mixer view's own coordinates.
QRect inView(QWidget* widget)
{
	return QRect(widget->mapTo(getGUI()->mixerView(), QPoint(0, 0)), widget->size());
}

} // namespace

class MixerPinnedStripsTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		guitest::startGui(m_home);
		QVERIFY(getGUI() != nullptr);
		ControlRegistry::setReady(true);
		getGUI()->mainWindow()->show();
		getGUI()->mainWindow()->revealPanel(QStringLiteral("mixer"));
		getGUI()->mainWindow()->resize(1400, 900);
		m_channel = Engine::mixer()->createChannel();
		syncMixer();
		QVERIFY(getGUI()->mixerView()->channelView(m_channel) != nullptr);
		// A VCA group gives the VCA region something to show, so it is a dockable region from the start.
		QVERIFY(run(QStringLiteral("vca.create"), {{QStringLiteral("name"), QStringLiteral("Drums")}}).ok);
		syncMixer();
		QCoreApplication::processEvents();
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void cleanup()
	{
		// Every case starts from the default arrangement, as a mixer with no stored layout shows.
		QVERIFY(run(QStringLiteral("window.mixer_zones"), {{QStringLiteral("reset"), true}}).ok);
		QVERIFY(pinnedOf().isEmpty());
	}

	//! Q050-227.2: a strip is pinned from its menu beside the scrolling channels. Pinned, it stays put while the
	//! channels scroll; the pin, a resize and a project save go through the same commands an agent calls, and
	//! reopening the project brings the pin, the id and the width back. Unpinned, the strip scrolls again.
	void pinUnpinResizeSaveAndRestoreTogether()
	{
		MainWindow* window = getGUI()->mainWindow();
		MixerView* view = getGUI()->mixerView();

		// (0) the master separator is dragged at the default size, where the existing separator test drags it.
		const int masterBefore = sizesOf(run(QStringLiteral("window.mixer_zones"))).value(QStringLiteral("master")).toInt();
		dragHandle(zoneIndex(QStringLiteral("master")), 40);
		QCoreApplication::processEvents();
		const int masterDragged = sizesOf(run(QStringLiteral("window.mixer_zones"))).value(QStringLiteral("master")).toInt();
		QVERIFY2(masterDragged > masterBefore, qPrintable(QStringLiteral("master %1 -> %2").arg(masterBefore).arg(masterDragged)));
		// Remember the resized, unpinned layout; the later save groups its pin change into one undo step.
		const ControlResult startingLayout = run(QStringLiteral("window.layout_project_save"));
		QVERIFY2(startingLayout.ok, qPrintable(startingLayout.errorMessage));
		QVERIFY(startingLayout.result.value(QStringLiteral("changed")).toBool());
		// Make a second, independent width change after that save; it must join the pin in the next layout step.
		dragHandle(zoneIndex(QStringLiteral("master")), 20);
		QCoreApplication::processEvents();
		const int masterAfterSecondDrag = sizesOf(run(QStringLiteral("window.mixer_zones")))
			.value(QStringLiteral("master")).toInt();
		QVERIFY2(masterAfterSecondDrag > masterDragged,
			qPrintable(QStringLiteral("second master drag %1 -> %2").arg(masterDragged).arg(masterAfterSecondDrag)));

		// (a) ten more strips, so the channel scroll area really scrolls at 1024 px.
		const int wanted = 12;
		while (static_cast<int>(Engine::mixer()->numChannels()) < wanted) { Engine::mixer()->createChannel(); }
		syncMixer();
		window->resize(1024, 768);
		QCoreApplication::processEvents();
		QVERIFY(view->channelCount() >= wanted);
		QVERIFY(view->channelScrollArea()->horizontalScrollBar()->maximum() > 0);

		// (b) pin the far-right strip through its menu entry; the command it runs is window.mixer_view.
		const int last = view->channelCount() - 1;
		const QString id = control::channelIdOf(Engine::mixer()->mixerChannel(last));
		QVERIFY(!id.isEmpty());
		QMenu menu;
		QAction* pin = pinActionFor(last, &menu);
		QVERIFY(pin != nullptr);
		QCOMPARE(pin->property("controlCommand").toString(), QStringLiteral("window.mixer_view"));
		QCOMPARE(pin->text(), QStringLiteral("Pin Strip"));
		QVector<QJsonObject> ran;
		collab::CommandTap::install({}, [&ran](const QString& command, const QJsonObject& args, bool, bool) {
			if (command == QStringLiteral("window.mixer_view")) { ran.append(args); }
		});
		pin->trigger();
		QCoreApplication::processEvents();
		collab::CommandTap::clear();
		QVERIFY2(!ran.isEmpty(), "the Pin Strip entry never invoked window.mixer_view");
		QCOMPARE(ran.last().value(QStringLiteral("pin")).toString(), id);

		// (c) the pin is in the layout, the strip sits in the pinned area, and that area is shown.
		QCOMPARE(pinnedOf(), QStringList{id});
		QVERIFY(inPinnedArea(last));
		QVERIFY(view->pinnedArea()->isVisible());

		// (d) scrolling: the pinned strip keeps its place; an unpinned strip moves with the scroll.
		QScrollBar* bar = view->channelScrollArea()->horizontalScrollBar();
		QVERIFY(bar->maximum() > 0);
		MixerChannelView* strip = view->channelView(last);
		MixerChannelView* other = view->channelView(1);
		QWidget* channels = view->zoneWidget(QStringLiteral("channels"));
		QVERIFY(channels != nullptr);
		bar->setValue(bar->minimum());
		QCoreApplication::processEvents();
		QVERIFY(strip->isVisible());
		QVERIFY2(inView(channels).contains(inView(strip)), "the pinned strip is outside the channels region at the left");
		const QRect stripAtMin = inView(strip);
		const QRect otherAtMin = inView(other);
		bar->setValue(bar->maximum());
		QCoreApplication::processEvents();
		QVERIFY(strip->isVisible());
		QVERIFY2(inView(channels).contains(inView(strip)), "the pinned strip is outside the channels region at the right");
		QCOMPARE(inView(strip).x(), stripAtMin.x());
		QVERIFY2(inView(other).x() != otherAtMin.x(), "the unpinned strip did not scroll");

		// (e) the width the layout has now, after the resize: this is what the restore must bring back.
		const int masterWidth = sizesOf(run(QStringLiteral("window.mixer_zones"))).value(QStringLiteral("master")).toInt();

		// (f) save the project with the layout (the pins and the widths travel with it).
		const ControlResult remembered = run(QStringLiteral("window.layout_project_save"));
		QVERIFY2(remembered.ok, qPrintable(remembered.errorMessage));
		const QString path = m_home.filePath(QStringLiteral("mixer-pins.mmp"));
		QVERIFY2(Engine::getSong()->saveProjectFile(path), qPrintable(Engine::getSong()->saveRefusal()));

		// (g) change it away: unpin through the menu, which now reads Unpin Strip.
		QMenu unpinMenu;
		QAction* unpin = pinActionFor(last, &unpinMenu);
		QVERIFY(unpin != nullptr);
		QCOMPARE(unpin->text(), QStringLiteral("Unpin Strip"));
		unpin->trigger();
		QCoreApplication::processEvents();
		QVERIFY(pinnedOf().isEmpty());
		QVERIFY(!inPinnedArea(last));
		QVERIFY(view->channelScrollArea()->widget()->isAncestorOf(view->channelView(last)));
		QVERIFY2(inView(view->channelView(last)).x() > inView(view->channelView(last - 1)).x(),
			"the unpinned strip is not after its neighbour in the scrolling channels");
		QVERIFY(run(QStringLiteral("window.mixer_zones"), {{QStringLiteral("reset"), true}}).ok);

		// One undo takes back the saved layout as a whole, including both the pin and the resize.
		const ControlResult undoneLayout = run(QStringLiteral("control.undo"));
		QVERIFY2(undoneLayout.ok, qPrintable(undoneLayout.errorMessage));
		QVERIFY(pinnedOf().isEmpty());
		QCOMPARE(sizesOf(run(QStringLiteral("window.mixer_zones"))).value(QStringLiteral("master")).toInt(), masterDragged);

		// (h) restore: the project brings the pin back on the same id, in the pinned area, with the same width.
		Engine::getSong()->loadProject(path);
		QCoreApplication::processEvents();
		syncMixer();
		QCOMPARE(pinnedOf(), QStringList{id});
		int restoredIndex = -1;
		const int count = static_cast<int>(Engine::mixer()->numChannels());
		for (int i = 0; i < count; ++i)
		{
			auto* channel = Engine::mixer()->mixerChannel(i);
			if (channel != nullptr && control::channelIdOf(channel) == id) { restoredIndex = i; }
		}
		QVERIFY(restoredIndex > 0);
		QVERIFY(inPinnedArea(restoredIndex));
		QCOMPARE(sizesOf(run(QStringLiteral("window.mixer_zones"))).value(QStringLiteral("master")).toInt(), masterWidth);

		// (i) finally unpin it again: back in the scrolling area, and no pins are left.
		QMenu finalMenu;
		QAction* again = pinActionFor(restoredIndex, &finalMenu);
		QVERIFY(again != nullptr);
		QCOMPARE(again->text(), QStringLiteral("Unpin Strip"));
		again->trigger();
		QCoreApplication::processEvents();
		QVERIFY(pinnedOf().isEmpty());
		QVERIFY(!inPinnedArea(restoredIndex));
		QVERIFY(view->channelScrollArea()->widget()->isAncestorOf(view->channelView(restoredIndex)));
		window->resize(1400, 900);
		QCoreApplication::processEvents();
	}

	//! Q050-227.2: the pin command refuses what cannot be pinned and reads a whole list in mixer order; a
	//! layout saved before pins existed opens with none, and garbage in 'pinned' is dropped, not fatal.
	void pinningRefusesWhatCannotBePinnedAndToleratesOldLayouts()
	{
		MixerView* view = getGUI()->mixerView();
		while (static_cast<int>(Engine::mixer()->numChannels()) < 4) { Engine::mixer()->createChannel(); } syncMixer();
		QVERIFY(view->channelCount() >= 4);
		const QString idA = control::channelIdOf(Engine::mixer()->mixerChannel(1));
		const QString idB = control::channelIdOf(Engine::mixer()->mixerChannel(2));
		const QString master = control::channelIdOf(Engine::mixer()->mixerChannel(0));

		QVERIFY(run(QStringLiteral("window.mixer_view"), {{QStringLiteral("pin"), idA}}).ok);
		QCOMPARE(pinnedOf(), QStringList{idA});

		// Refusals: an unknown id is NotFound, the master and a non-array are InvalidArgs; none changes a pin.
		const ControlResult noSuchChannel = run(QStringLiteral("window.mixer_view"), {{QStringLiteral("pin"), QStringLiteral("ch-99999")}});
		QVERIFY(!noSuchChannel.ok);
		QCOMPARE(noSuchChannel.errorKind, ControlErrorKind::NotFound);
		QCOMPARE(pinnedOf(), QStringList{idA});
		const ControlResult masterRefused = run(QStringLiteral("window.mixer_view"), {{QStringLiteral("pin"), master}});
		QVERIFY(!masterRefused.ok);
		QCOMPARE(masterRefused.errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(pinnedOf(), QStringList{idA});
		const ControlResult notAList = run(QStringLiteral("window.mixer_view"), {{QStringLiteral("pinned_channels"), QStringLiteral("x")}});
		QVERIFY(!notAList.ok);
		QCOMPARE(notAList.errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(pinnedOf(), QStringList{idA});

		// The whole list, given reversed, is answered in mixer order.
		const ControlResult whole = run(QStringLiteral("window.mixer_view"), {{QStringLiteral("pinned_channels"), QJsonArray{idB, idA}}});
		QVERIFY2(whole.ok, qPrintable(whole.errorMessage));
		QCOMPARE(pinnedIn(whole), QStringList({idA, idB}));
		QCOMPARE(pinnedOf(), QStringList({idA, idB}));

		// Unpinning a strip that is not pinned is a no-op that still answers.
		const QString unpinned = control::channelIdOf(Engine::mixer()->mixerChannel(3));
		const ControlResult noop = run(QStringLiteral("window.mixer_view"), {{QStringLiteral("unpin"), unpinned}});
		QVERIFY2(noop.ok, qPrintable(noop.errorMessage));
		QCOMPARE(pinnedOf(), QStringList({idA, idB}));

		// The master has no pin entry at all.
		QMenu masterMenu;
		QVERIFY(pinActionFor(0, &masterMenu) == nullptr);

		// Legacy: a snapshot whose mixer_zones has no 'pinned' key opens with none pinned.
		Shell* shell = getGUI()->mainWindow()->shell();
		QJsonObject snapshot = shell->snapshot();
		QJsonObject zones = snapshot.value(QStringLiteral("mixer_zones")).toObject();
		QVERIFY(!zones.value(QStringLiteral("pinned")).toArray().isEmpty());
		zones.remove(QStringLiteral("pinned"));
		snapshot.insert(QStringLiteral("mixer_zones"), zones);
		shell->restoreSnapshot(snapshot);
		QCoreApplication::processEvents();
		syncMixer();
		QVERIFY(pinnedOf().isEmpty());

		// Garbage: non-strings and a repeated id are dropped; the one real channel is pinned once.
		QJsonArray garbage;
		garbage.append(5);
		garbage.append(QStringLiteral("bogus"));
		garbage.append(idA);
		garbage.append(idA);
		zones.insert(QStringLiteral("pinned"), garbage);
		snapshot.insert(QStringLiteral("mixer_zones"), zones);
		shell->restoreSnapshot(snapshot);
		QCoreApplication::processEvents();
		syncMixer();
		QCOMPARE(pinnedOf(), QStringList{idA});

		// Deleting a pinned channel: no crash, and the pins no longer name it.
		const int victim = view->channelCount() - 1;
		const QString victimId = control::channelIdOf(Engine::mixer()->mixerChannel(victim));
		QMenu victimMenu;
		QAction* victimPin = pinActionFor(victim, &victimMenu);
		QVERIFY(victimPin != nullptr);
		victimPin->trigger();
		QCoreApplication::processEvents();
		QVERIFY(pinnedOf().contains(victimId));
		view->deleteChannel(victim);
		QCoreApplication::processEvents();
		syncMixer();
		QVERIFY(!pinnedOf().contains(victimId));
		QCOMPARE(pinnedOf(), QStringList{idA});
	}

private:
	QTemporaryDir m_home;
	int m_channel = 0;
};

int main(int argc, char** argv)
{
	if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")
		&& qEnvironmentVariableIsEmpty("DISPLAY")
		&& qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY"))
	{
		qputenv("QT_QPA_PLATFORM", "offscreen");
	}
	QApplication app(argc, argv);
	MixerPinnedStripsTest test;
	return QTest::qExec(&test, argc, argv);
}

#include "MixerPinnedStripsTest.moc"
