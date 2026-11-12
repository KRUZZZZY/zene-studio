/*
 * MixerZonesTest.cpp - Q050-227.1: the mixer's dockable regions, their separators, and the layout
 *                      the project keeps of them
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

/*! The mixer body is a splitter of dockable regions (master, channels, VCA strips, effect rack), and
 *  every boundary between them is a separator a drag resizes. Three acceptance criteria:
 *   1. at least two regions dock in supported positions and a separator resizes in the UI;
 *   2. saving the project layout and reopening the project restores the docks and the sizes;
 *   3. a project (and a personal layout) without the new fields opens with the default arrangement.
 *  Each case drives the GUI the way a person does - a menu entry on the mode bar, or a real
 *  QSplitterHandle drag - and reads the same state an agent reads through window.mixer_zones. */

#include <QtTest>
#include "MixerModeVisibilityTestSupport.h"

#include <QApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QSplitter>
#include <QToolButton>
#include <QSplitterHandle>
#include <QTemporaryDir>

#include "CollabCommandTap.h"
#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "MainWindow.h"
#include "MenuTestSupport.h"
#include "Mixer.h"
#include "MixerModeBar.h"
#include "EffectRackView.h"
#include "MixerView.h"
#include "MixerZones.h"
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

QJsonObject zonesOf(const ControlResult& result)
{
	return result.result.value(QStringLiteral("zones")).toObject();
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

} // namespace

class MixerZonesTest : public QObject
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
	}

	//! Criterion 3: a mixer with no stored layout shows the arrangement it always had - the master
	//! left, the channels in the middle, the VCA strips and the rack right.
	void theDefaultArrangementIsTheFixedOneTheMixerHad()
	{
		const mixerzones::Layout defaults = mixerzones::defaultLayout();
		QCOMPARE(defaults.docks.value(QStringLiteral("master")), QStringLiteral("left"));
		QCOMPARE(defaults.docks.value(QStringLiteral("channels")), QStringLiteral("middle"));
		QCOMPARE(defaults.docks.value(QStringLiteral("vca")), QStringLiteral("right"));
		QCOMPARE(defaults.docks.value(QStringLiteral("rack")), QStringLiteral("right"));
		const ControlResult state = run(QStringLiteral("window.mixer_zones"));
		QVERIFY2(state.ok, qPrintable(state.errorMessage));
		QCOMPARE(zonesOf(state), (QJsonObject{{QStringLiteral("master"), QStringLiteral("left")},
			{QStringLiteral("channels"), QStringLiteral("middle")}, {QStringLiteral("vca"), QStringLiteral("right")},
			{QStringLiteral("rack"), QStringLiteral("right")}}));

		// ... and it is what the splitter actually shows: master, then the channels, then VCA and rack.
		QVERIFY(zoneIndex(QStringLiteral("master")) < zoneIndex(QStringLiteral("channels")));
		QVERIFY(zoneIndex(QStringLiteral("channels")) < zoneIndex(QStringLiteral("vca")));
		QVERIFY(zoneIndex(QStringLiteral("vca")) < zoneIndex(QStringLiteral("rack")));
	}

	//! A document without the new fields (a project or a personal layout from before this feature)
	//! loads the default arrangement rather than failing or collapsing the mixer.
	void aLayoutWithoutTheNewFieldsOpensWithTheDefault()
	{
		QVERIFY(run(QStringLiteral("window.mixer_zones"), {{QStringLiteral("dock"), QStringLiteral("master=right")}}).ok);
		Shell* shell = getGUI()->mainWindow()->shell();
		QJsonObject legacy = shell->snapshot();
		legacy.remove(QStringLiteral("mixer_zones"));
		shell->restoreSnapshot(legacy);
		const ControlResult state = run(QStringLiteral("window.mixer_zones"));
		QVERIFY2(state.ok, qPrintable(state.errorMessage));
		QCOMPARE(zonesOf(state).value(QStringLiteral("master")).toString(), QStringLiteral("left"));
		QCOMPARE(sizesOf(state).value(QStringLiteral("rack")).toInt(), mixerzones::defaultSize(QStringLiteral("rack")));
		// A corrupt/partial object is tolerant too: unknown ids are ignored, a bad size falls back.
		const QJsonObject partial{
			{QStringLiteral("zones"), QJsonObject{{QStringLiteral("no-such-region"), QStringLiteral("left")},
				{QStringLiteral("channels"), QStringLiteral("left")}}},
			{QStringLiteral("sizes"), QJsonObject{{QStringLiteral("rack"), -5}}}};
		shell->restoreSnapshot(QJsonObject{{QStringLiteral("mixer_zones"), partial}});
		const ControlResult tolerant = run(QStringLiteral("window.mixer_zones"));
		QVERIFY2(tolerant.ok, qPrintable(tolerant.errorMessage));
		QCOMPARE(zonesOf(tolerant).value(QStringLiteral("channels")).toString(), QStringLiteral("middle"));
		QCOMPARE(sizesOf(tolerant).value(QStringLiteral("rack")).toInt(), mixerzones::defaultSize(QStringLiteral("rack")));
	}

	//! Criterion 1: two regions dock in supported positions, through the GUI and through the command,
	//! and the splitter really re-orders them.
	void twoRegionsDockInSupportedPositions()
	{
		MixerView* view = getGUI()->mixerView();
		QVERIFY(view->zoneWidget(QStringLiteral("master")) != nullptr);
		QVERIFY(view->zoneWidget(QStringLiteral("rack")) != nullptr);

		// The mode bar's Zones menu is the GUI entry point; its actions declare the one command.
		QMenu menu;
		view->modeBar()->fillZones(&menu);
		QAction* masterLeft = menu.actions().at(0)->menu()->actions().at(0);
		QVERIFY(masterLeft != nullptr);
		QCOMPARE(masterLeft->property("controlCommand").toString(), QStringLiteral("window.mixer_zones"));
		QVERIFY(masterLeft->isChecked());

		// Triggering the menu's "right" action is a person's click: the same registered command runs.
		QAction* masterRight = menu.actions().at(0)->menu()->actions().at(1);
		QVERIFY(masterRight != nullptr);
		masterRight->trigger();
		QCOMPARE(zonesOf(run(QStringLiteral("window.mixer_zones"))).value(QStringLiteral("master")).toString(),
			QStringLiteral("right"));
		QVERIFY(zoneIndex(QStringLiteral("channels")) < zoneIndex(QStringLiteral("master")));
		QVERIFY(zoneIndex(QStringLiteral("master")) < zoneIndex(QStringLiteral("vca")));

		// The rack docks left; both regions are on supported sides and the command agrees.
		const ControlResult docked = run(QStringLiteral("window.mixer_zones"), {{QStringLiteral("dock"), QStringLiteral("rack=left")}});
		QVERIFY2(docked.ok, qPrintable(docked.errorMessage));
		QCOMPARE(docked.result.value(QStringLiteral("mixer_zones")).toObject().value(QStringLiteral("zones")).toObject()
			.value(QStringLiteral("rack")).toString(), QStringLiteral("left"));
		QVERIFY(zoneIndex(QStringLiteral("rack")) < zoneIndex(QStringLiteral("channels")));
		QVERIFY(zoneIndex(QStringLiteral("channels")) < zoneIndex(QStringLiteral("master")));

		// A dock the mixer cannot support is refused, typed, and changes nothing.
		const ControlResult refused = run(QStringLiteral("window.mixer_zones"), {{QStringLiteral("dock"), QStringLiteral("channels=left")}});
		QVERIFY(!refused.ok);
		QCOMPARE(refused.errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(zonesOf(run(QStringLiteral("window.mixer_zones"))).value(QStringLiteral("channels")).toString(),
			QStringLiteral("middle"));
	}

	//! Criterion 1: dragging a real separator resizes the region, the new width is what the state (and so
	//! the saved layout) reports, AND the drag reached that state through the registered
	//! window.mixer_zones command - the same one an agent calls - not by writing the state directly.
	void aSeparatorDragResizesAZone()
	{
		MixerView* view = getGUI()->mixerView();
		QSplitter* splitter = view->zonesSplitter();
		QVERIFY(splitter->count() >= 3);
		QVERIFY(splitter->handle(1) != nullptr);

		const int before = splitter->sizes().at(1);  // the channels, right of the master's separator
		const int index = zoneIndex(QStringLiteral("master"));
		QVERIFY(index >= 0);
		QTRY_VERIFY2(splitter->sizes().at(index) > 0, "the master region has no width to drag");
		const int masterBefore = splitter->sizes().at(index);

		// The registry's own top-level tap: it is told of EVERY command it ran, after the handler. So the
		// record below cannot be produced by a widget writing MixerZoneState directly - only by the
		// command being the thing that ran.
		QVector<QJsonObject> ran;
		collab::CommandTap::install({}, [&ran](const QString& id, const QJsonObject& args, bool, bool) {
			if (id == QStringLiteral("window.mixer_zones")) { ran.append(args); }
		});
		dragHandle(index, 40);
		QCoreApplication::processEvents();
		collab::CommandTap::clear();

		QVERIFY2(splitter->sizes().at(index) > masterBefore,
			qPrintable(QStringLiteral("master kept its width %1 after the drag").arg(masterBefore)));
		QVERIFY(splitter->sizes().at(index + 1) < before);
		// The drag went through the command, as a live width report, with the width the splitter now has.
		QVERIFY2(!ran.isEmpty(), "the separator drag never invoked window.mixer_zones");
		const QJsonObject report = ran.last();
		QCOMPARE(report.value(QStringLiteral("live")).toBool(), true);
		for (const QJsonObject& call : ran) { QCOMPARE(call.value(QStringLiteral("live")).toBool(), true); }
		QCOMPARE(report.value(QStringLiteral("sizes")).toObject().value(QStringLiteral("master")).toInt(),
			splitter->sizes().at(index));
		QCOMPARE(sizesOf(run(QStringLiteral("window.mixer_zones"))).value(QStringLiteral("master")).toInt(),
			splitter->sizes().at(index));

		// And the agent's own size argument goes through the identical handler: same id, same state, same
		// answer shape - a dragged width and a commanded one are one path with two callers.
		QVERIFY(run(QStringLiteral("window.mixer_zones"), {{QStringLiteral("sizes"),
			QJsonObject{{QStringLiteral("master"), 220}}}}).ok);
		QCOMPARE(splitter->sizes().at(zoneIndex(QStringLiteral("master"))), 220);
	}
	//! 'live' is the drag's report only: it refuses a dock or a reset, so it can never re-lay the
	//! splitter in the middle of a gesture.
	void aLiveReportRefusesAnythingButSizes()
	{
		const ControlResult refused = run(QStringLiteral("window.mixer_zones"), {{QStringLiteral("live"), true},
			{QStringLiteral("dock"), QStringLiteral("master=right")}});
		QVERIFY(!refused.ok);
		QCOMPARE(refused.errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(zonesOf(run(QStringLiteral("window.mixer_zones"))).value(QStringLiteral("master")).toString(),
			QStringLiteral("left"));
		// A live report with no sizes is a no-op that still answers, so a stray splitterMoved cannot fail.
		const ControlResult empty = run(QStringLiteral("window.mixer_zones"), {{QStringLiteral("live"), true}});
		QVERIFY2(empty.ok, qPrintable(empty.errorMessage));
	}

	//! Criterion 2: the project's own layout carries the docks and the widths. Save the layout with the
	//! project, reopen it, and both are back on screen as well as in window.mixer_zones.
	void savingAndReopeningTheProjectRestoresTheDocksAndTheSizes()
	{
		MixerView* view = getGUI()->mixerView();
		QSplitter* splitter = view->zonesSplitter();
		QVERIFY(run(QStringLiteral("window.mixer_zones"), {{QStringLiteral("dock"), QStringLiteral("master=right")},
			{QStringLiteral("sizes"), QJsonObject{{QStringLiteral("rack"), 300}, {QStringLiteral("master"), 140}}}}).ok);
		QVERIFY2(zoneIndex(QStringLiteral("channels")) < zoneIndex(QStringLiteral("master")), "the master did not dock right");
		const ControlResult remembered = run(QStringLiteral("window.layout_project_save"));
		QVERIFY2(remembered.ok, qPrintable(remembered.errorMessage));
		QVERIFY(remembered.result.value(QStringLiteral("captures")).toArray().contains(QStringLiteral("mixer_zones")));

		const QString path = m_home.filePath(QStringLiteral("mixer-zones.mmp"));
		QVERIFY2(Engine::getSong()->saveProjectFile(path), qPrintable(Engine::getSong()->saveRefusal()));

		// Another document, and the mixer back on its default: the project, opening, is what restores it.
		QVERIFY(run(QStringLiteral("window.mixer_zones"), {{QStringLiteral("reset"), true}}).ok);
		QCOMPARE(zonesOf(run(QStringLiteral("window.mixer_zones"))).value(QStringLiteral("master")).toString(), QStringLiteral("left"));
		Engine::getSong()->loadProject(path);
		QCoreApplication::processEvents();

		const ControlResult restored = run(QStringLiteral("window.mixer_zones"));
		QVERIFY2(restored.ok, qPrintable(restored.errorMessage));
		QCOMPARE(zonesOf(restored).value(QStringLiteral("master")).toString(), QStringLiteral("right"));
		QCOMPARE(sizesOf(restored).value(QStringLiteral("rack")).toInt(), 300);
		QCOMPARE(sizesOf(restored).value(QStringLiteral("master")).toInt(), 140);
		// The splitter itself carries the restored arrangement, not just the state object.
		QVERIFY2(zoneIndex(QStringLiteral("channels")) < zoneIndex(QStringLiteral("master")), "the restored master is not right");
		QCOMPARE(splitter->sizes().at(zoneIndex(QStringLiteral("master"))), 140);
		QCOMPARE(splitter->sizes().at(zoneIndex(QStringLiteral("rack"))), 300);
	}

	//! One undo step per layout edit: the layout save is one true_inverse step, and it puts BOTH the
	//! docks and the sizes back.
	void theLayoutSaveIsOneUndoStepForTheWholeArrangement()
	{
		// Two savable arrangements, so the second save's step has a project layout to put back.
		QVERIFY(run(QStringLiteral("window.mixer_zones"), {{QStringLiteral("dock"), QStringLiteral("vca=left")},
			{QStringLiteral("sizes"), QJsonObject{{QStringLiteral("vca"), 120}}}}).ok);
		QVERIFY(run(QStringLiteral("window.layout_project_save")).ok);
		QVERIFY(run(QStringLiteral("window.mixer_zones"), {{QStringLiteral("dock"), QStringLiteral("vca=right")},
			{QStringLiteral("sizes"), QJsonObject{{QStringLiteral("vca"), 160}}}}).ok);
		QVERIFY(run(QStringLiteral("window.layout_project_save")).ok);
		QVERIFY(run(QStringLiteral("window.mixer_zones"), {{QStringLiteral("reset"), true}}).ok);
		QCOMPARE(zonesOf(run(QStringLiteral("window.mixer_zones"))).value(QStringLiteral("vca")).toString(), QStringLiteral("right"));

		QVERIFY(run(QStringLiteral("control.undo")).ok);
		const ControlResult back = run(QStringLiteral("window.mixer_zones"));
		QVERIFY2(back.ok, qPrintable(back.errorMessage));
		QCOMPARE(zonesOf(back).value(QStringLiteral("vca")).toString(), QStringLiteral("left"));
		QCOMPARE(sizesOf(back).value(QStringLiteral("vca")).toInt(), 120);
	}

	//! The zones menu is the GUI face of the one command: every entry declares it, so a click and an
	//! agent take the identical path (SPEC A11/A15).
	void everyZonesMenuEntryDeclaresTheCommand()
	{
		MixerModeBar* bar = getGUI()->mixerView()->modeBar();
		QVERIFY(bar->zonesButton() != nullptr);
		QVERIFY(!bar->zonesButton()->text().isEmpty());
		QMenu menu;
		bar->fillZones(&menu);
		const QList<QAction*> leaves = test::leafActions(&menu);
		QVERIFY(!leaves.isEmpty());
		for (QAction* action : leaves)
		{
			QCOMPARE(action->property("controlCommand").toString(), QStringLiteral("window.mixer_zones"));
		}
	}

	//! gui-1030: on a 1024x768 screen, Mix view with the Inspector and the Browser open, the New mixer
	//! channel and Return buttons are on screen - the channel strips scroll, the columns beside them do not.
	void theAddButtonsStayOnScreenInANarrowMixView()
	{
		MainWindow* window = getGUI()->mainWindow();
		window->shell()->setView(ShellView::Mix);
		window->revealPanel(QStringLiteral("inspector"));
		window->revealPanel(QStringLiteral("browser"));
		window->resize(1024, 768);
		QCoreApplication::processEvents();
		MixerView* view = getGUI()->mixerView();
		QVERIFY2(window->width() <= 1024, qPrintable(QStringLiteral("the window is forced %1 px wide").arg(window->width())));
		for (const char* name : {"newChannelBtn", "newReturnBtn"})
		{
			auto* button = view->findChild<QWidget*>(QLatin1String(name));
			QVERIFY(button != nullptr);
			// The part of the button its ancestors (the panel, the window) leave on screen.
			QRect shown(button->mapTo(window, QPoint(0, 0)), button->size());
			for (QWidget* w = button->parentWidget(); w != nullptr && w != window; w = w->parentWidget())
			{
				shown &= QRect(w->mapTo(window, QPoint(0, 0)), w->size());
			}
			shown &= window->rect();
			QVERIFY2(shown.width() == button->width(), qPrintable(QStringLiteral("%1: %2 of %3 px on screen")
				.arg(QLatin1String(name)).arg(shown.width()).arg(button->width())));
		}
		window->shell()->setView(ShellView::Arrange);
		window->revealPanel(QStringLiteral("mixer"));
		window->resize(1400, 900);
	}

	//! gui-1030: the Mix panel's minimum width must not force the main window wider than a 1024 px
	//! display (Inspector and Browser at their minimum widths included). The effects chain at the
	//! panel's right edge must then lie inside the window, and the mode bar's controls, which no longer
	//! set the panel's width floor, remain visible and unobscured.
	void theMixerFitsAMainWindowOf1024Pixels()
	{
		MainWindow* window = getGUI()->mainWindow();
		window->resize(1024, 768);
		QCoreApplication::processEvents();
		QVERIFY2(window->width() <= 1024, qPrintable(QStringLiteral("the window could not shrink below %1 px").arg(window->width())));

		EffectRackView* rack = nullptr;
		for (EffectRackView* candidate : getGUI()->mixerView()->findChildren<EffectRackView*>())
		{
			if (candidate->isVisible()) { rack = candidate; }
		}
		QVERIFY(rack != nullptr);
		const int right = rack->mapTo(window, QPoint(0, 0)).x() + rack->width();
		QVERIFY2(right <= window->width(), qPrintable(QStringLiteral("the effects chain ends at %1 of %2").arg(right).arg(window->width())));

		const QString problem = lmms::test::mixerModeVisibilityProblem(getGUI()->mixerView()->modeBar(), window);
		QVERIFY2(problem.isEmpty(), qPrintable(problem));
		window->resize(1400, 900);
		QCoreApplication::processEvents();
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
	MixerZonesTest test;
	return QTest::qExec(&test, argc, argv);
}

#include "MixerZonesTest.moc"
