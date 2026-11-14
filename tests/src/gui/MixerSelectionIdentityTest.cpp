/*
 * MixerSelectionIdentityTest.cpp - the mixer keeps the selected channel, by identity, through model changes
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

/*! The selection follows the channel, not its number. A channel removed or moved in the model
 *  renumbers the others, and the strips are rebuilt on the next sync; the selection must still be the
 *  same channel afterwards, or Master when that channel is gone. These cases change the model directly,
 *  the way a script or the TrackContainer does, and then let the mixer's own sync pass catch up. */

#include <QtTest>
#include "MixerModeVisibilityTestSupport.h"

#include <QAbstractButton>
#include <QLabel>
#include <QPushButton>
#include <QTemporaryDir>
#include <QScrollArea>
#include <QScrollBar>

#include "AutomatableButton.h"
#include "ControlRegistry.h"
#include "EffectRackView.h"
#include "Engine.h"
#include "Fader.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "Knob.h"
#include "MainWindow.h"
#include "Model.h"
#include "MacroKnobs.h"
#include "Mixer.h"
#include "MixerChannelView.h"
#include "MixerView.h"
#include "Shell.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

//! The name of the channel the mixer currently has selected.
QString selectedName()
{
	MixerChannelView* view = getGUI()->mixerView()->currentMixerChannel();
	MixerChannel* channel = Engine::mixer()->mixerChannel(view->channelIndex());
	return channel != nullptr ? channel->m_name : QString();
}

//! The half-second sync, run now: a count change rebuilds the strips.
void syncMixer()
{
	getGUI()->mixerView()->syncWithMixer();
	getGUI()->mixerView()->syncWithMixer();
}

//! Index of the channel named @a name, or -1.
int indexOf(const QString& name)
{
	for (int i = 0; i < Engine::mixer()->numChannels(); ++i)
	{
		if (Engine::mixer()->mixerChannel(i)->m_name == name) { return i; }
	}
	return -1;
}

} // namespace

class MixerSelectionIdentityTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		guitest::startGui(m_home);
		QVERIFY(getGUI() != nullptr);
		getGUI()->mainWindow()->show();
		getGUI()->mainWindow()->revealPanel(QStringLiteral("mixer"));
		ControlRegistry::setReady(true);  // the Return button runs bus.create
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	// A channel removed from the model before the selected one: the selected channel renumbers, and the
	// selection must follow it. Its index is stale until the sync, so only its identity can be right.
	void aRemovalBeforeTheSelectedChannelKeepsTheSelection()
	{
		MixerView* mixer = getGUI()->mixerView();
		const int front = Engine::mixer()->createChannel();
		Engine::mixer()->mixerChannel(front)->m_name = QStringLiteral("Front");
		const int chosen = Engine::mixer()->createChannel();
		Engine::mixer()->mixerChannel(chosen)->m_name = QStringLiteral("Chosen");
		Engine::mixer()->mixerChannel(Engine::mixer()->createChannel())->m_name = QStringLiteral("Last");
		syncMixer();
		mixer->setCurrentMixerChannel(chosen);
		QCOMPARE(selectedName(), QStringLiteral("Chosen"));

		Engine::mixer()->deleteChannel(front);
		syncMixer();

		QCOMPARE(selectedName(), QStringLiteral("Chosen"));
		QCOMPARE(mixer->currentMixerChannel()->channelIndex(), indexOf(QStringLiteral("Chosen")));
	}

	// The selected channel is the last one and is removed from the model: nothing is left at its old
	// number, so the selection falls back to Master rather than reading a channel that no longer exists.
	void removingTheSelectedLastChannelFallsBackToMaster()
	{
		MixerView* mixer = getGUI()->mixerView();
		const int doomed = Engine::mixer()->createChannel();
		Engine::mixer()->mixerChannel(doomed)->m_name = QStringLiteral("Doomed");
		syncMixer();
		mixer->setCurrentMixerChannel(doomed);
		QCOMPARE(selectedName(), QStringLiteral("Doomed"));
		QCOMPARE(doomed, static_cast<int>(Engine::mixer()->numChannels()) - 1);

		Engine::mixer()->deleteChannel(doomed);
		syncMixer();

		QCOMPARE(mixer->currentMixerChannel()->channelIndex(), 0);
		QCOMPARE(selectedName(), QStringLiteral("Master"));
	}

	// A same-count reorder moves the selected channel to a new number: the selection must follow the
	// channel, not stay on the number it had.
	void aReorderKeepsTheSelectedChannelSelected()
	{
		MixerView* mixer = getGUI()->mixerView();
		const int first = Engine::mixer()->createChannel();
		Engine::mixer()->mixerChannel(first)->m_name = QStringLiteral("Reorder A");
		const int second = Engine::mixer()->createChannel();
		Engine::mixer()->mixerChannel(second)->m_name = QStringLiteral("Reorder B");
		syncMixer();
		mixer->setCurrentMixerChannel(second);
		QCOMPARE(selectedName(), QStringLiteral("Reorder B"));

		Engine::mixer()->moveChannelLeft(second);  // the count stays the same
		QCOMPARE(static_cast<int>(Engine::mixer()->numChannels()), second + 1);
		syncMixer();

		QCOMPARE(selectedName(), QStringLiteral("Reorder B"));
		QCOMPARE(mixer->currentMixerChannel()->channelIndex(), first);
	}

	// The same reorder, checked on the strips too: after it, every strip drives the channel now at its
	// own number, and the selection is still the moved channel.
	void aReorderRebindsEveryStripAndKeepsTheSelection()
	{
		MixerView* mixer = getGUI()->mixerView();
		const int first = Engine::mixer()->createChannel();
		Engine::mixer()->mixerChannel(first)->m_name = QStringLiteral("Rebind A");
		const int second = Engine::mixer()->createChannel();
		Engine::mixer()->mixerChannel(second)->m_name = QStringLiteral("Rebind B");
		for (int i = first; i <= second; ++i)
		{
			Engine::mixer()->mixerChannel(i)->m_volumeModel.setValue(0.25f + 0.25f * (i - first));
		}
		syncMixer();
		mixer->setCurrentMixerChannel(second);

		Engine::mixer()->moveChannelLeft(second);
		syncMixer();

		QCOMPARE(selectedName(), QStringLiteral("Rebind B"));
		for (int i = 0; i < static_cast<int>(Engine::mixer()->numChannels()); ++i)
		{
			MixerChannel* channel = Engine::mixer()->mixerChannel(i);
			QCOMPARE(mixer->channelView(i)->channelIndex(), i);
			QCOMPARE(mixer->channelView(i)->soloButton()->model(), &channel->m_soloModel);
		}
	}

	// A control receives its own press, not the strip's. Selection must change first, so the next
	// mixer shortcut edits the strip the user just operated.
	void pressingAChildControlSelectsItsStripAndRoutesKeyboardFaderAdjustment()
	{
		MixerView* mixer = getGUI()->mixerView();
		const int targetIndex = Engine::mixer()->createChannel();
		Engine::mixer()->mixerChannel(targetIndex)->m_name = QStringLiteral("Pressed target");
		syncMixer();
		MixerChannelView* masterView = mixer->channelView(0);
		MixerChannelView* targetView = mixer->channelView(targetIndex);
		AutomatableButton* mute = targetView->findChild<AutomatableButton*>(QStringLiteral("btn-mute"));
		QVERIFY(mute != nullptr);

		MixerChannel* master = Engine::mixer()->mixerChannel(0);
		MixerChannel* target = Engine::mixer()->mixerChannel(targetIndex);
		const auto clickAndRoute = [this, mixer, masterView, targetView, master, target](QWidget* control)
		{
			bool selectedWhenActionRan = false;
			QMetaObject::Connection actionConnection;
			QPoint clickPosition = control->rect().center();
			if (qobject_cast<Fader*>(control) != nullptr)
			{
				target->m_volumeModel.setValue(0.5f);
				actionConnection = connect(&target->m_volumeModel, &Model::dataChanged, this, [&]
				{
					selectedWhenActionRan = mixer->currentMixerChannel() == targetView;
				});
				clickPosition.setY(0);  // outside the handle, so the press changes the fader model synchronously
			}
			else if (auto* editor = qobject_cast<FloatModelEditorBase*>(control))
			{
				actionConnection = connect(editor, &FloatModelEditorBase::sliderPressed, this, [&]
				{
					selectedWhenActionRan = mixer->currentMixerChannel() == targetView;
				});
			}
			else if (auto* button = qobject_cast<QAbstractButton*>(control))
			{
				actionConnection = connect(button, &QAbstractButton::clicked, this, [&]
				{
					selectedWhenActionRan = mixer->currentMixerChannel() == targetView;
				});
			}
			QVERIFY(actionConnection);
			mixer->setCurrentMixerChannel(masterView);
			QTest::mouseClick(control, Qt::LeftButton, Qt::NoModifier, clickPosition);
			disconnect(actionConnection);
			QVERIFY(selectedWhenActionRan);
			QCOMPARE(mixer->currentMixerChannel(), targetView);
			QCOMPARE(selectedName(), QStringLiteral("Pressed target"));

			target->m_volumeModel.setValue(0.5f);
			master->m_volumeModel.setValue(0.5f);
			const float targetBefore = target->m_volumeModel.value();
			const float masterBefore = master->m_volumeModel.value();
			QTest::keyClick(mixer, Qt::Key_Up);
			QVERIFY(target->m_volumeModel.value() != targetBefore);
			QCOMPARE(master->m_volumeModel.value(), masterBefore);
		};

		clickAndRoute(targetView->fader());
		clickAndRoute(targetView->panKnob());
		clickAndRoute(mute);

		// MacroKnobs rebuilds after the strip's constructor has installed its initial descendant filters.
		// Selection must already have changed when this dynamically created knob begins its drag action.
		target->m_rack.macros().addMacro(QStringLiteral("Dynamic macro"), 0.5f);
		targetView->macroKnobs()->refresh();
		Knob* macro = targetView->macroKnobs()->findChild<Knob*>();
		QVERIFY(macro != nullptr);
		bool selectedWhenMacroActionRan = false;
		connect(macro, &FloatModelEditorBase::sliderPressed, this, [&]
		{
			selectedWhenMacroActionRan = mixer->currentMixerChannel() == targetView;
		});
		mixer->setCurrentMixerChannel(masterView);
		QTest::mousePress(macro, Qt::LeftButton, Qt::NoModifier, macro->rect().center());
		QVERIFY(selectedWhenMacroActionRan);
		QCOMPARE(mixer->currentMixerChannel(), targetView);
		QTest::mouseRelease(macro, Qt::LeftButton, Qt::NoModifier, macro->rect().center());
	}

	//! gui-1114-tab-mixer-mute-master (FALSE-POSITIVE guard): the Master mute button is bound to the
	//! channel's mute model, and a real press toggles the model and its checked state. The app-wide
	//! selection filter (ShellSelection::eventFilter) also selects the strip, but returns false, so the
	//! press is not swallowed. The finding's press missed the button: in the 300 px bottom region the
	//! strip is clipped, so the button's AT-SPI centre lies on the footer's Devices switch. This test
	//! pins both halves - the control works where it is really on screen, and the filter's non-consuming
	//! return is what keeps it working - and restores the shell before it asserts, so a failure here
	//! cannot change what the next case sees.
	void theMasterMuteButtonTogglesAndDoesNotOpenDevices()
	{
		MainWindow* win = getGUI()->mainWindow();
		MixerView* mixer = getGUI()->mixerView();
		MixerChannel* master = Engine::mixer()->mixerChannel(0);
		MixerChannelView* strip = mixer->channelView(0);
		QVERIFY(strip != nullptr);
		AutomatableButton* mute = strip->findChild<AutomatableButton*>(QStringLiteral("btn-mute"));
		QVERIFY(mute != nullptr);
		QCOMPARE(mute->model(), &master->m_muteModel);

		// Mix view: the mixer is the centre panel, so the whole strip - mute included - is on screen.
		win->shell()->setView(ShellView::Mix);
		win->shell()->revealPanel(QStringLiteral("mixer"));
		QCoreApplication::processEvents();
		QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
		// The default Mix view also shows Devices at the bottom; close it so "the click opened it" is visible.
		if (win->shell()->model().isVisible(QStringLiteral("devices"))) { win->shell()->togglePanel(QStringLiteral("devices")); }
		QCoreApplication::processEvents();

		const QRect whole(mute->mapTo(win, QPoint(0, 0)), mute->size());
		QRect shown = whole;
		for (QWidget* w = mute->parentWidget(); w != nullptr && w != win; w = w->parentWidget())
		{
			shown &= QRect(w->mapTo(win, QPoint(0, 0)), w->size());
		}
		shown &= win->rect();
		const QString shownProblem = shown == whole ? QString()
			: QStringLiteral("only %1x%2 of the %3x%4 mute at %5,%6 is on screen in Mix view")
				.arg(shown.width()).arg(shown.height()).arg(whole.width()).arg(whole.height())
				.arg(whole.x()).arg(whole.y());
		const bool centreReachesMute = win->childAt(mute->mapTo(win, mute->rect().center())) == mute;

		master->m_muteModel.setValue(false);
		QTest::mouseClick(mute, Qt::LeftButton);
		const bool onInMix = master->m_muteModel.value();
		const bool checkedInMix = static_cast<QAbstractButton*>(mute)->isChecked();
		const bool selectedInMix = mixer->currentMixerChannel() == strip;
		const bool devicesFromMixPress = win->shell()->model().isVisible(QStringLiteral("devices"));
		QTest::mouseClick(mute, Qt::LeftButton);
		const bool offInMix = !master->m_muteModel.value();
		const bool uncheckedInMix = !static_cast<QAbstractButton*>(mute)->isChecked();
		const bool devicesFromSecondPress = win->shell()->model().isVisible(QStringLiteral("devices"));

		// The same button in the bottom region of Arrange view, where the finding's sweep clicked. The
		// region can be shorter than the strip (300 px for a 361 px strip today), so only part of the
		// button is on screen; pressing the part that IS visible must still reach the button and toggle
		// the same model. The sweep pressed the centre of the AT-SPI box, which lies below that visible
		// part and on the footer's Devices switch - that is the press that opened Devices.
		win->shell()->setView(ShellView::Arrange);
		win->shell()->revealPanel(QStringLiteral("mixer"));
		QCoreApplication::processEvents();
		QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
		const QRect inRegion(mute->mapTo(win, QPoint(0, 0)), mute->size());
		QRect regionShown = inRegion;
		for (QWidget* w = mute->parentWidget(); w != nullptr && w != win; w = w->parentWidget())
		{
			regionShown &= QRect(w->mapTo(win, QPoint(0, 0)), w->size());
		}
		regionShown &= win->rect();
		const QPoint pressAt = regionShown.center();
		const bool pressPointReachesMute = regionShown.height() > 0 && win->childAt(pressAt) == mute;
		const QPoint local = mute->mapFrom(win, pressAt);
		master->m_muteModel.setValue(false);
		if (pressPointReachesMute && mute->rect().contains(local)) { QTest::mouseClick(mute, Qt::LeftButton, Qt::NoModifier, local); }
		const bool onInBottomRegion = master->m_muteModel.value();
		const bool checkedInBottomRegion = static_cast<QAbstractButton*>(mute)->isChecked();
		const bool devicesFromBottomPress = win->shell()->model().isVisible(QStringLiteral("devices"));

		win->shell()->setView(ShellView::Arrange);
		win->shell()->revealPanel(QStringLiteral("detail"));

		QVERIFY2(shownProblem.isEmpty(), qPrintable(shownProblem));
		QVERIFY2(centreReachesMute, "something else covers the mute button's centre in Mix view");
		QVERIFY2(onInMix, "a real press on Master mute did not turn the mute model on");
		QVERIFY2(checkedInMix, "the Master mute button did not show itself checked");
		QVERIFY2(selectedInMix, "the press did not also select the strip it landed on");
		QVERIFY2(!devicesFromMixPress, "a mute press must not open Devices");
		QVERIFY2(offInMix, "a second press did not turn the mute model off");
		QVERIFY2(uncheckedInMix, "the Master mute button did not show itself unchecked");
		QVERIFY2(!devicesFromSecondPress, "the second mute press opened Devices");
		QVERIFY2(regionShown.height() > 0, "no part of the Master mute button is on screen in the bottom region");
		QVERIFY2(pressPointReachesMute, "the part of the mute button on screen in the bottom region is covered");
		QVERIFY2(mute->rect().contains(local), "the press point is outside the mute button");
		QVERIFY2(onInBottomRegion, "a press on the visible part of the bottom region's mute did not toggle it");
		QVERIFY2(checkedInBottomRegion, "the bottom region's mute button did not show itself checked");
		QVERIFY2(!devicesFromBottomPress, "a mute press in the bottom region opened Devices");
		QVERIFY2(win->childAt(mute->mapTo(win, mute->rect().center())) != mute || regionShown.height() == mute->height(),
			"the sweep's AT-SPI centre is inside the mute button, so the finding needs re-measuring");
	}

	// The Return button creates a bus and refreshes the view at once. The strip it adds must already
	// be labelled Return then: the rebuild of the strips must not skip the type badges, which would
	// show "Channel N" until the half-second timer's next pass.
	void aNewReturnStripIsLabelledReturnAtOnce()
	{
		MixerView* mixer = getGUI()->mixerView();
		syncMixer();
		const int before = static_cast<int>(Engine::mixer()->numChannels());
		auto* button = mixer->findChild<QAbstractButton*>(QStringLiteral("newReturnBtn"));
		QVERIFY(button != nullptr);

		QTest::mouseClick(button, Qt::LeftButton);  // no sync of ours after this: only the click's own

		QCOMPARE(static_cast<int>(Engine::mixer()->numChannels()), before + 1);
		MixerChannelView* strip = mixer->channelView(before);
		QVERIFY(strip != nullptr);
		QVERIFY(Engine::mixer()->mixerChannel(before)->isBus());
		QCOMPARE(strip->typeBadge()->text(), QStringLiteral("Return"));
	}

private slots:
	// gui-1030-tab-mixer-return: on a 1024x768 screen with the Inspector and the Browser open, the Mix view's
	// mixer must still fit - the window's minimum width follows the mixer's, so a mixer that wants more than the
	// centre column leaves its right-hand controls (New mixer channel, Return) beyond the screen.
	void theReturnButtonStaysOnAThousandPixelScreen()
	{
		MainWindow* win = getGUI()->mainWindow();
		MixerView* mixer = getGUI()->mixerView();
		win->shell()->setView(ShellView::Mix);
		win->revealPanel(QStringLiteral("inspector"));
		win->revealPanel(QStringLiteral("browser"));
		win->revealPanel(QStringLiteral("mixer"));
		QCoreApplication::processEvents();  // let the panel moves settle before the window is sized
		QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
		QVERIFY2(win->minimumSizeHint().width() <= 1024,
			qPrintable(QStringLiteral("window minimum width %1").arg(win->minimumSizeHint().width())));

		win->resize(1024, 768);
		QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
		QVERIFY(win->width() <= 1024);

		auto* newReturn = mixer->findChild<QWidget*>(QStringLiteral("newReturnBtn"));
		QVERIFY(newReturn != nullptr);
		QVERIFY(newReturn->isVisible());
		const QRect onWindow(newReturn->mapTo(win, QPoint(0, 0)), newReturn->size());
		QVERIFY2(win->rect().contains(onWindow), qPrintable(QStringLiteral("Return at %1,%2 %3x%4 in a window %5x%6")
			.arg(onWindow.x()).arg(onWindow.y()).arg(onWindow.width()).arg(onWindow.height())
			.arg(win->width()).arg(win->height())));

		// Every mode control, including the last one, remains visible and clickable.
		auto* bar = mixer->findChild<QWidget*>(QStringLiteral("mixerModeBar"));
		QVERIFY(bar != nullptr);
		const QString problem = lmms::test::mixerModeVisibilityProblem(bar, win);
		QVERIFY2(problem.isEmpty(), qPrintable(problem));
		auto* matrix = bar->findChild<QWidget*>(QStringLiteral("mixerModeMatrix"));
		QVERIFY(matrix != nullptr);
		const QRect onBar(matrix->mapTo(bar, QPoint(0, 0)), matrix->size());
		QVERIFY(bar->rect().contains(onBar));
	}

	//! gui-1030-tab-mixer-add-effect: on a 1024x768 screen, Mix view with the Inspector and the Browser
	//! open, the effect rack's Add effect button stays on screen. The window's minimum width follows the
	//! mixer's: the mode bar's rigid row and the channel area's six-strip minimum made it 2000 px wide,
	//! and a window wider than the screen leaves the rack - at the right of the body - beyond the edge.
	void theAddEffectButtonStaysOnAThousandPixelScreen()
	{
		MainWindow* win = getGUI()->mainWindow();
		MixerView* mixer = getGUI()->mixerView();
		win->shell()->setView(ShellView::Mix);
		win->revealPanel(QStringLiteral("inspector"));
		win->revealPanel(QStringLiteral("browser"));
		win->revealPanel(QStringLiteral("mixer"));
		QCoreApplication::processEvents();  // let the panel moves settle before the window is sized
		QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
		QVERIFY2(win->minimumSizeHint().width() <= 1024,
			qPrintable(QStringLiteral("window minimum width %1").arg(win->minimumSizeHint().width())));

		win->resize(1024, 768);
		QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
		QVERIFY(win->width() <= 1024);

		// The selected channel's rack is the page on show; its Add effect sits at the bottom of the rack.
		QPushButton* addEffect = nullptr;
		for (QPushButton* button : mixer->zoneWidget(QStringLiteral("rack"))->findChildren<QPushButton*>())
		{
			if (button->isVisible() && button->text() == EffectRackView::tr("Add effect")) { addEffect = button; }
		}
		QVERIFY2(addEffect != nullptr, "the selected channel's effect rack has no visible Add effect button");
		const QRect whole(addEffect->mapTo(win, QPoint(0, 0)), addEffect->size());
		QRect shown = whole;
		for (QWidget* w = addEffect->parentWidget(); w != nullptr; w = w->parentWidget())
		{
			shown &= QRect(w->mapTo(win, QPoint(0, 0)), w->size());
		}
		shown &= win->rect();
		QVERIFY2(shown == whole, qPrintable(QStringLiteral("Add effect: %1x%2 of %3x%4 px on screen")
			.arg(shown.width()).arg(shown.height()).arg(whole.width()).arg(whole.height())));

		win->shell()->setView(ShellView::Arrange);
		win->revealPanel(QStringLiteral("mixer"));
		win->resize(1400, 900);
	}

private:
	QTemporaryDir m_home;
};

QTEST_MAIN(MixerSelectionIdentityTest)
#include "MixerSelectionIdentityTest.moc"
