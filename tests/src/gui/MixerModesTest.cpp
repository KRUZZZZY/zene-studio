/*
 * MixerModesTest.cpp - 0.5.0 theme 16: the mixer's modes, channel types and routing, in words
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

/*! Theme 16 (approved as recommended, adjusted by the research): explicit Mix and Routing views and,
 *  independent of them, Compact and Expanded densities, set by window.mixer_view and by a mode bar that
 *  names every mode (what the faders drive, solo mode, listen, the master's output and format); each strip
 *  wears a type badge (with its surround format), names its routes by channel name - never an id - and
 *  shows the modes it is in; the routing matrix keeps only the sends that exist. Needs the real
 *  application (a Fader cannot be built without it). */

#include <QtTest>

#include <QAccessible>
#include <QApplication>
#include <QComboBox>
#include <QEvent>
#include <QContextMenuEvent>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QMenu>
#include <QPushButton>
#include <QSet>
#include <QTimer>
#include <QToolButton>

#include "AutomatableButton.h"
#include "AutomatableModel.h"
#include "ColorChooser.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Fader.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "Knob.h"
#include "MacroKnobs.h"
#include "MainWindow.h"
#include "Mixer.h"
#include "PinConnector.h"
#include "MixerChannelLcdSpinBox.h"
#include "MixerChannelView.h"
#include "MixerModeBar.h"
#include "MixerRoutingMatrix.h"
#include "MixerStripInfo.h"
#include "MixerView.h"
#include "SendButtonIndicator.h"
#include "MixerViewOptions.h"
#include "Song.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

class TestAudioPortsModel : public AudioPortsModel
{
public:
	using AudioPortsModel::AudioPortsModel;

protected:
	void bufferPropertiesChanging(ch_cnt_t, ch_cnt_t, f_cnt_t) override {}
};

QString ch(int index) { return control::channelIdOf(Engine::mixer()->mixerChannel(index)); }

//! The view's half-second sync, now: a count change rebuilds the strips, the next pass applies the modes.
void syncMixer()
{
	getGUI()->mixerView()->syncWithMixer();
	getGUI()->mixerView()->syncWithMixer();
}

MixerChannelView* strip(int index) { return getGUI()->mixerView()->channelView(index); }
MixerModeBar* bar() { return getGUI()->mixerView()->modeBar(); }

//! What a screen reader hears for @a widget, through Qt's own accessibility layer.
QString announcedName(QWidget* widget)
{
	QAccessibleInterface* face = QAccessible::queryAccessibleInterface(widget);
	return face != nullptr ? face->text(QAccessible::Name) : widget->accessibleName();
}

QAccessible::Role announcedRole(QWidget* widget)
{
	QAccessibleInterface* face = QAccessible::queryAccessibleInterface(widget);
	return face != nullptr ? face->role() : QAccessible::NoRole;
}

//! Keyboard focus only lives in an active window; a test earlier may have opened a menu.
bool activateMainWindow()
{
	QWidget* window = getGUI() != nullptr ? getGUI()->mainWindow() : nullptr;
	if (window == nullptr) { return false; }
	window->activateWindow();
	return QTest::qWaitForWindowActive(window);
}

AutomatableButton* muteButtonOf(MixerChannelView* view)
{
	return view != nullptr ? view->findChild<AutomatableButton*>(QStringLiteral("btn-mute")) : nullptr;
}

} // namespace

class MixerModesTest : public QObject
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
		m_sender = Engine::mixer()->createChannel();
		Engine::mixer()->mixerChannel(m_sender)->m_name = QStringLiteral("Lead");
		const ControlResult created = run(QStringLiteral("bus.create"));
		m_return = created.result.value(QStringLiteral("index")).toInt(-1);
		QVERIFY(m_return > 0);
		Engine::mixer()->mixerChannel(m_return)->m_name = QStringLiteral("Verb");
		QVERIFY(run(QStringLiteral("mixer.send_to"), {{QStringLiteral("channel"), ch(m_sender)}, {QStringLiteral("to"), ch(m_return)},
			{QStringLiteral("amount"), 0.5}}).ok);
		QVERIFY(run(QStringLiteral("window.mixer_view"), {{QStringLiteral("view"), QStringLiteral("mix")},
			{QStringLiteral("density"), QStringLiteral("expanded")}}).ok);
		syncMixer();
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void aMixerChannelContextMenuWorksWithoutTrackView()
	{
		MixerChannelLcdSpinBox spinBox(2, nullptr, QStringLiteral("Mixer channel"));
		spinBox.show();

		bool menuOpened = false;
		QTimer::singleShot(0, &spinBox, [&] {
			if (QMenu* menu = spinBox.findChild<QMenu*>())
			{
				menuOpened = true;
				menu->close();
			}
		});

		QContextMenuEvent event(QContextMenuEvent::Mouse, QPoint(0, 0), QPoint(0, 0));
		QApplication::sendEvent(&spinBox, &event);
		QVERIFY(menuOpened);
	}

	void everyStripWearsItsTypeInWords()
	{
		QCOMPARE(strip(0)->typeBadge()->text(), QStringLiteral("Master"));
		QCOMPARE(strip(m_return)->typeBadge()->text(), QStringLiteral("Return"));
		QVERIFY(strip(m_sender)->typeBadge()->isVisibleTo(getGUI()->mixerView()));
		// The strip says it all in sentences for a screen reader.
		QVERIFY(strip(m_return)->accessibleDescription().startsWith(QStringLiteral("Return")));
	}

	void theSurroundFormatJoinsTheBadge()
	{
		QVERIFY(run(QStringLiteral("mixer.channel_format"), {{QStringLiteral("channel"), ch(m_return)},
			{QStringLiteral("format"), QStringLiteral("5.1")}}).ok);
		syncMixer();
		QCOMPARE(strip(m_return)->typeBadge()->toolTip(), QStringLiteral("Return 5.1"));
		QVERIFY(run(QStringLiteral("mixer.channel_format"), {{QStringLiteral("channel"), ch(m_return)},
			{QStringLiteral("format"), QStringLiteral("stereo")}}).ok);
		syncMixer();
		QCOMPARE(strip(m_return)->typeBadge()->toolTip(), QStringLiteral("Return"));
	}

	void routesAreNamedByChannelNeverById()
	{
		const QString route = strip(m_sender)->routingLine()->toolTip();
		QVERIFY2(route.contains(QStringLiteral("Verb")), qPrintable(route));
		QVERIFY2(!route.contains(QStringLiteral("ch-")), qPrintable(route));
		// The answer an agent reads names the same things.
		const QJsonArray strips = run(QStringLiteral("window.mixer_view")).result.value(QStringLiteral("strips")).toArray();
		QCOMPARE(strips.size(), Engine::mixer()->numChannels());
		const QJsonObject sender = strips.at(m_sender).toObject();
		QCOMPARE(sender.value(QStringLiteral("type")).toString(), QStringLiteral("channel"));
		bool found = false;
		for (const QJsonValue& output : sender.value(QStringLiteral("outputs")).toArray())
		{
			found = found || output.toObject().value(QStringLiteral("to")).toString() == QStringLiteral("Verb");
		}
		QVERIFY(found);
		QCOMPARE(strips.at(m_return).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("return"));
	}

	void theRoutingViewShowsWhereSignalComesFrom()
	{
		const ControlResult set = run(QStringLiteral("window.mixer_view"), {{QStringLiteral("view"), QStringLiteral("routing")}});
		QVERIFY(set.ok);
		QCOMPARE(set.result.value(QStringLiteral("view")).toString(), QStringLiteral("routing"));
		syncMixer();
		const QString route = strip(m_return)->routingLine()->toolTip();
		QVERIFY2(route.contains(QStringLiteral("Lead")), qPrintable(route));  // Lead sends into Verb
		QVERIFY(bar()->viewButton(QStringLiteral("routing"))->isChecked());
		QVERIFY(!strip(m_sender)->insertList()->isVisibleTo(getGUI()->mixerView()));  // processing is the mix view's
	}

	void theCompactDensityKeepsTheEssentials()
	{
		QVERIFY(run(QStringLiteral("window.mixer_view"), {{QStringLiteral("view"), QStringLiteral("mix")},
			{QStringLiteral("density"), QStringLiteral("compact")}}).ok);
		syncMixer();
		QVERIFY(bar()->densityButton(QStringLiteral("compact"))->isChecked());
		QVERIFY(!strip(m_sender)->macroKnobs()->isVisibleTo(getGUI()->mixerView()));
		QVERIFY(!strip(m_sender)->routingLine()->text().contains(QLatin1Char('\n')));  // one line
		QVERIFY(strip(m_sender)->fader()->isVisibleTo(getGUI()->mixerView()));
		// Density never narrows a strip: the name keeps strip_width's width.
		QCOMPARE(strip(m_sender)->nameEdit()->width(), 56);
	}

	void theModeBarSetsTheViewThroughTheCommand()
	{
		bar()->densityButton(QStringLiteral("expanded"))->click();
		QCOMPARE(mixerviewoptions::options().density, QStringLiteral("expanded"));
		bar()->viewButton(QStringLiteral("routing"))->click();
		QCOMPARE(mixerviewoptions::options().view, QStringLiteral("routing"));
		bar()->viewButton(QStringLiteral("mix"))->click();
		QCOMPARE(mixerviewoptions::options().view, QStringLiteral("mix"));
		QComboBox* filter = bar()->filterBox();
		const int returns = filter->findData(QStringLiteral("returns"));
		QVERIFY(returns >= 0);
		emit filter->activated(returns);
		QCOMPARE(mixerviewoptions::options().filter, QStringLiteral("returns"));
		emit filter->activated(filter->findData(QStringLiteral("all")));
		QCOMPARE(mixerviewoptions::options().filter, QStringLiteral("all"));
	}

	//! Acceptance 1: the fader and the mute/solo (LED) buttons are Tab stops, answer their keys, and
	//! are in the accessibility walk with the channel's name and the right role.
	void mixerControlsTakeFocusAndAnswerTheKeys()
	{
		QVERIFY(activateMainWindow());
		syncMixer();
		MixerView* mixerView = getGUI()->mixerView();
		MixerChannelView* view = strip(m_sender);
		mixerView->setCurrentMixerChannel(m_sender);
		const QString channelName = Engine::mixer()->mixerChannel(m_sender)->m_name;

		QWidget* mute = muteButtonOf(view);
		QWidget* solo = view->soloButton();
		QVERIFY(mute != nullptr);
		QVERIFY(solo != nullptr);
		Fader* fader = view->fader();
		Knob* pan = view->panKnob();
		QCOMPARE(mute->focusPolicy(), Qt::TabFocus);
		QCOMPARE(solo->focusPolicy(), Qt::TabFocus);
		QCOMPARE(fader->focusPolicy(), Qt::TabFocus);

		// The same walk AccessibilityWalkTest makes: every visible Tab stop, with a name.
		QSet<QWidget*> walked;
		for (QWidget* widget : view->findChildren<QWidget*>())
		{
			if ((widget->focusPolicy() & Qt::TabFocus) == 0 || !widget->isVisibleTo(mixerView)) { continue; }
			walked.insert(widget);
			QVERIFY2(!announcedName(widget).isEmpty(),
				qPrintable(QStringLiteral("unnamed focusable %1").arg(QString::fromLatin1(widget->metaObject()->className()))));
		}
		QVERIFY(walked.contains(fader));
		QVERIFY(walked.contains(mute));
		QVERIFY(walked.contains(solo));
		QVERIFY(walked.contains(pan));

		// Qt's bridge says what each control is, and names it for its channel.
		QCOMPARE(announcedRole(fader), QAccessible::Slider);
		QCOMPARE(announcedRole(pan), QAccessible::Dial);
		QCOMPARE(announcedRole(mute), QAccessible::Button);
		QCOMPARE(announcedRole(solo), QAccessible::Button);
		QCOMPARE(announcedName(fader), QStringLiteral("Fader %1").arg(channelName));
		QCOMPARE(announcedName(mute), QStringLiteral("Mute %1").arg(channelName));
		QCOMPARE(announcedName(solo), QStringLiteral("Solo %1").arg(channelName));
		QCOMPARE(announcedName(pan), QStringLiteral("Pan %1").arg(channelName));
		QCOMPARE(view->accessibleName(), channelName);  // the strip's name is the channel context
		QVERIFY(announcedName(fader).contains(channelName));

		// The fader answers the slider keys - the same path the keyboard already used.
		FloatModel* volume = &Engine::mixer()->mixerChannel(m_sender)->m_volumeModel;
		const float original = volume->value();
		fader->setFocus();
		QTRY_VERIFY(fader->hasFocus());
		QTest::keyClick(fader, Qt::Key_End);
		QCOMPARE(volume->value(), volume->maxValue());
		QTest::keyClick(fader, Qt::Key_Home);
		QCOMPARE(volume->value(), volume->minValue());
		QTest::keyClick(fader, Qt::Key_PageUp);
		QVERIFY(volume->value() > volume->minValue());
		QTest::keyClick(fader, Qt::Key_Up);
		QVERIFY(volume->value() > volume->minValue());
		volume->setValue(original);

		// Space presses the model buttons.
		BoolModel* muteModel = &Engine::mixer()->mixerChannel(m_sender)->m_muteModel;
		const bool wasMuted = muteModel->value();
		mute->setFocus();
		QTRY_VERIFY(mute->hasFocus());
		QTest::keyClick(mute, Qt::Key_Space);
		QCOMPARE(muteModel->value(), !wasMuted);
		QTest::keyClick(mute, Qt::Key_Space);
		QCOMPARE(muteModel->value(), wasMuted);
	}

	//! A send knob's model is the selected source's send to this strip; focusing it must not select
	//! its destination and hide the control before the keyboard can adjust it.
	void sendKnobKeepsItsSourceWhileFocused()
	{
		QVERIFY(activateMainWindow());
		syncMixer();
		MixerView* mixerView = getGUI()->mixerView();
		MixerChannelView* destination = strip(m_return);
		mixerView->setCurrentMixerChannel(m_sender);
		destination->soloButton()->setFocus();
		QTRY_VERIFY(destination->soloButton()->hasFocus());
		QCOMPARE(mixerView->currentMixerChannel(), destination);

		// The send belongs to the selected source, so restoring that source shows the knob on its target.
		mixerView->setCurrentMixerChannel(m_sender);
		FloatModel* sendModel = Engine::mixer()->channelSendModel(m_sender, m_return);
		QVERIFY(sendModel != nullptr);
		const QList<QWidget*> controls = destination->keyboardFocusOrder();
		QVERIFY(!controls.isEmpty());
		auto* sendKnob = qobject_cast<Knob*>(controls.first());
		QVERIFY(sendKnob != nullptr);
		QCOMPARE(announcedName(sendKnob), QStringLiteral("Send to Verb"));
		QVERIFY(sendKnob->isVisibleTo(mixerView));

		const float original = sendModel->value();
		sendKnob->setFocus();
		QTRY_VERIFY(sendKnob->hasFocus());
		QCOMPARE(mixerView->currentMixerChannel(), strip(m_sender));
		QVERIFY(sendKnob->isVisibleTo(mixerView));
		QTest::keyClick(sendKnob, Qt::Key_Up);
		QVERIFY(sendModel->value() > original);
		sendModel->setValue(original);

		// Keep the source selected while Ctrl+Tab follows the same send to an adjacent destination.
		const ControlResult created = run(QStringLiteral("bus.create"));
		const int adjacentIndex = created.result.value(QStringLiteral("index")).toInt(-1);
		QCOMPARE(adjacentIndex, m_return + 1);
		QVERIFY(run(QStringLiteral("mixer.send_to"), {{QStringLiteral("channel"), ch(m_sender)},
			{QStringLiteral("to"), ch(adjacentIndex)}, {QStringLiteral("amount"), 0.25}}).ok);
		syncMixer();
		mixerView->setCurrentMixerChannel(m_sender);
		destination = strip(m_return);
		const QList<QWidget*> refreshedControls = destination->keyboardFocusOrder();
		QVERIFY(!refreshedControls.isEmpty());
		sendKnob = qobject_cast<Knob*>(refreshedControls.first());
		QVERIFY(sendKnob != nullptr);
		MixerChannelView* adjacent = strip(adjacentIndex);
		const QList<QWidget*> adjacentControls = adjacent->keyboardFocusOrder();
		QVERIFY(!adjacentControls.isEmpty());
		auto* adjacentSendKnob = qobject_cast<Knob*>(adjacentControls.first());
		QVERIFY(adjacentSendKnob != nullptr);
		const QString adjacentName = Engine::mixer()->mixerChannel(adjacentIndex)->m_name;
		QCOMPARE(announcedName(adjacentSendKnob), QStringLiteral("Send to %1").arg(adjacentName));

		QTest::keyClick(sendKnob, Qt::Key_Tab, Qt::ControlModifier);
		QTRY_COMPARE(QApplication::focusWidget(), adjacentSendKnob);
		QCOMPARE(mixerView->currentMixerChannel(), strip(m_sender));
		QTest::keyClick(adjacentSendKnob, Qt::Key_Backtab, Qt::ControlModifier);
		QTRY_COMPARE(QApplication::focusWidget(), sendKnob);
		QCOMPARE(mixerView->currentMixerChannel(), strip(m_sender));
		mixerView->deleteChannel(adjacentIndex);
		syncMixer();
	}

	//! Acceptance 2: Tab and Shift+Tab walk the strip's controls in order and wrap inside it.
	void tabMovesThroughOneStripsControls()
	{
		QVERIFY(activateMainWindow());
		syncMixer();
		MixerView* mixerView = getGUI()->mixerView();
		mixerView->setCurrentMixerChannel(m_sender);
		MixerChannelView* view = strip(m_sender);
		const QString channelName = Engine::mixer()->mixerChannel(m_sender)->m_name;
		AutomatableButton* mute = muteButtonOf(view);
		AutomatableButton* solo = view->soloButton();
		QVERIFY(mute != nullptr);
		QVERIFY(solo != nullptr);

		// Walk forward from Solo until Tab comes back to it. The strip's own controls must all be
		// met on the way - and nothing outside the strip - before the wrap.
		const auto walk = [&](Qt::Key key, QList<QWidget*>* seen) {
			seen->append(solo);
			solo->setFocus();
			QTRY_COMPARE(QApplication::focusWidget(), solo);
			for (int i = 0; i < 32; ++i)
			{
				QWidget* current = QApplication::focusWidget();
				QVERIFY(current != nullptr);
				QTest::keyClick(current, key);
				QWidget* next = QApplication::focusWidget();
				QVERIFY(next != nullptr);
				if (next == solo) { return; }
				QVERIFY2(!seen->contains(next), "Tab left the strip or repeated a control before wrapping");
				QVERIFY2(view->isAncestorOf(next), "Tab left the strip");
				seen->append(next);
			}
			QFAIL("Tab did not wrap back to the first control");
		};

		QList<QWidget*> forward;
		walk(Qt::Key_Tab, &forward);
		QTRY_COMPARE(QApplication::focusWidget(), solo);
		QVERIFY(forward.size() >= 4);
		QVERIFY(forward.contains(view->fader()));
		QVERIFY(forward.contains(mute));
		QVERIFY(forward.contains(view->panKnob()));
		for (QWidget* control : forward)
		{
			QVERIFY(!announcedName(control).isEmpty());
			// The strip stays the selected channel while the keyboard moves through it.
			QCOMPARE(mixerView->currentMixerChannel(), view);
		}
		QVERIFY(announcedName(view->fader()).contains(channelName));

		// Shift+Tab walks the same set backwards (the last control comes first).
		QList<QWidget*> reverse;
		walk(Qt::Key_Backtab, &reverse);
		QTRY_COMPARE(QApplication::focusWidget(), solo);
		QCOMPARE(reverse.size(), forward.size());
		QSet<QWidget*> forwardSet;
		for (QWidget* control : forward) { forwardSet.insert(control); }
		QSet<QWidget*> reverseSet;
		for (QWidget* control : reverse) { reverseSet.insert(control); }
		QCOMPARE(reverseSet, forwardSet);
		QCOMPARE(reverse.at(1), forward.last());
	}

	//! Acceptance 2 and 3: Ctrl+Tab and Ctrl+Shift+Tab reach the same control on the adjacent
	//! channel, forward and backward, and leave the selection with the focus.
	void ctrlTabMovesTheSameControlAcrossChannels()
	{
		QVERIFY(activateMainWindow());
		syncMixer();
		MixerView* mixerView = getGUI()->mixerView();
		MixerChannelView* first = strip(0);
		MixerChannelView* second = strip(1);
		const QString firstName = Engine::mixer()->mixerChannel(0)->m_name;
		const QString secondName = Engine::mixer()->mixerChannel(1)->m_name;

		AutomatableButton* firstMute = muteButtonOf(first);
		AutomatableButton* secondMute = muteButtonOf(second);
		QVERIFY(firstMute != nullptr);
		QVERIFY(secondMute != nullptr);
		firstMute->setFocus();
		QTRY_VERIFY(firstMute->hasFocus());
		QTest::keyClick(firstMute, Qt::Key_Tab, Qt::ControlModifier);
		QTRY_COMPARE(QApplication::focusWidget(), secondMute);
		QCOMPARE(mixerView->currentMixerChannel(), second);
		QCOMPARE(announcedName(secondMute), QStringLiteral("Mute %1").arg(secondName));

		QTest::keyClick(secondMute, Qt::Key_Tab, Qt::ControlModifier | Qt::ShiftModifier);
		QTRY_COMPARE(QApplication::focusWidget(), firstMute);
		QCOMPARE(mixerView->currentMixerChannel(), first);
		QCOMPARE(announcedName(firstMute), QStringLiteral("Mute %1").arg(firstName));

		// A fader is the same fader on the next channel; reverse comes back to this one.
		first->fader()->setFocus();
		QTRY_VERIFY(first->fader()->hasFocus());
		QTest::keyClick(first->fader(), Qt::Key_Tab, Qt::ControlModifier);
		QTRY_COMPARE(QApplication::focusWidget(), second->fader());
		QCOMPARE(announcedName(second->fader()), QStringLiteral("Fader %1").arg(secondName));
		QTest::keyClick(second->fader(), Qt::Key_Backtab, Qt::ControlModifier);
		QTRY_COMPARE(QApplication::focusWidget(), first->fader());
	}

	//! Acceptance 3: at the first and last channel the command keeps the focus and the selection -
	//! after proving it still moves between the channels inside those ends.
	void ctrlTabDoesNotWrapAtTheMixerEnds()
	{
		QVERIFY(activateMainWindow());
		syncMixer();
		MixerView* mixerView = getGUI()->mixerView();
		MixerChannelView* master = strip(0);
		MixerChannelView* second = strip(1);
		second->soloButton()->setFocus();
		QTRY_VERIFY(second->soloButton()->hasFocus());
		QTest::keyClick(second->soloButton(), Qt::Key_Tab, Qt::ControlModifier | Qt::ShiftModifier);
		QTRY_COMPARE(QApplication::focusWidget(), master->soloButton());
		QCOMPARE(mixerView->currentMixerChannel(), master);
		// From the first channel, backwards stays here.
		master->soloButton()->setFocus();
		QTRY_VERIFY(master->soloButton()->hasFocus());
		QTest::keyClick(master->soloButton(), Qt::Key_Tab, Qt::ControlModifier | Qt::ShiftModifier);
		QTRY_VERIFY(master->soloButton()->hasFocus());
		QCOMPARE(mixerView->currentMixerChannel(), master);

		const int last = static_cast<int>(Engine::mixer()->numChannels()) - 1;
		QVERIFY(last > 1);
		MixerChannelView* previous = strip(last - 1);
		MixerChannelView* end = strip(last);
		previous->fader()->setFocus();
		QTRY_VERIFY(previous->fader()->hasFocus());
		QTest::keyClick(previous->fader(), Qt::Key_Tab, Qt::ControlModifier);
		QTRY_COMPARE(QApplication::focusWidget(), end->fader());
		QCOMPARE(mixerView->currentMixerChannel(), end);
		// From the last channel, forwards stays here.
		end->fader()->setFocus();
		QTRY_VERIFY(end->fader()->hasFocus());
		QTest::keyClick(end->fader(), Qt::Key_Tab, Qt::ControlModifier);
		QTRY_VERIFY(end->fader()->hasFocus());
		QCOMPARE(mixerView->currentMixerChannel(), end);
	}

	void ctrlRightClickDoesNotSelectAnotherStrip()
	{
		MixerView* mixerView = getGUI()->mixerView();
		mixerView->setCurrentMixerChannel(m_sender);
		MixerChannelView* target = strip(m_return);

		QTest::mousePress(target, Qt::RightButton, Qt::ControlModifier, target->rect().center());

		QCOMPARE(mixerView->currentMixerChannel()->channelIndex(), m_sender);
	}

	void listeningShowsOnTheStripAndTheBar()
	{
		QVERIFY(run(QStringLiteral("mixer.listen"), {{QStringLiteral("channel"), ch(m_sender)}}).ok);
		syncMixer();
		QVERIFY(strip(m_sender)->stateLine()->toolTip().contains(QStringLiteral("AFL")));
		QVERIFY(bar()->listenButton()->text().contains(QStringLiteral("Lead")));
		QVERIFY(bar()->listenButton()->property("active").toBool());
		QVERIFY(run(QStringLiteral("mixer.listen_mode"), {{QStringLiteral("clear"), true}}).ok);
		syncMixer();
		QVERIFY(!strip(m_sender)->stateLine()->isVisibleTo(getGUI()->mixerView()));
		QCOMPARE(bar()->listenButton()->text(), QStringLiteral("Listen: Off"));
	}

	void theSoloModeIsNamedOnTheBarAndEveryStrip()
	{
		QVERIFY(run(QStringLiteral("mixer.solo_mode"), {{QStringLiteral("mode"), QStringLiteral("additive")}}).ok);
		syncMixer();
		QCOMPARE(bar()->soloButton()->text(), QStringLiteral("Solo: Additive"));
		QVERIFY(strip(m_sender)->soloButton()->toolTip().contains(QStringLiteral("additive")));
		QVERIFY(run(QStringLiteral("mixer.solo_mode"), {{QStringLiteral("mode"), QStringLiteral("exclusive")}}).ok);
		syncMixer();
		QCOMPARE(bar()->soloButton()->text(), QStringLiteral("Solo: Exclusive"));
	}

	void theBarNamesWhatTheFadersDriveAndTheMastersOutput()
	{
		QCOMPARE(bar()->fadersButton()->text(), QStringLiteral("Faders: Volume"));
		QVERIFY(run(QStringLiteral("window.mixer_view"), {{QStringLiteral("sends_on_faders"), ch(m_return)}}).ok);
		syncMixer();
		QCOMPARE(bar()->fadersButton()->text(), QStringLiteral("Faders: Sends to Verb"));
		QVERIFY(run(QStringLiteral("window.mixer_view"), {{QStringLiteral("sends_on_faders"), QString()}}).ok);
		syncMixer();
		QVERIFY(bar()->masterButton()->text().startsWith(QStringLiteral("Master: ")));
		QVERIFY(!bar()->masterButton()->text().contains(QStringLiteral("ch-")));
	}

	void sendIndicatorOnlyTogglesOnLeftPress()
	{
		MixerView* view = getGUI()->mixerView();
		view->setCurrentMixerChannel(m_sender);
		Mixer* mixer = Engine::mixer();
		Song* song = Engine::getSong();
		QVERIFY(mixer->channelSendModel(m_sender, m_return) != nullptr);
		song->noteProjectSaved(QStringLiteral("send-indicator-clean.mmpz"));
		QVERIFY(!song->isModified());

		SendButtonIndicator indicator{nullptr, strip(m_return), view};
		QMouseEvent rightPress{QEvent::MouseButtonPress, QPointF{1, 1}, QPointF{1, 1}, QPointF{1, 1},
			Qt::RightButton, Qt::RightButton, Qt::NoModifier};
		QApplication::sendEvent(&indicator, &rightPress);
		QVERIFY(mixer->channelSendModel(m_sender, m_return) != nullptr);
		QVERIFY(!song->isModified());

		QMouseEvent middlePress{QEvent::MouseButtonPress, QPointF{1, 1}, QPointF{1, 1}, QPointF{1, 1},
			Qt::MiddleButton, Qt::MiddleButton, Qt::NoModifier};
		QApplication::sendEvent(&indicator, &middlePress);
		QVERIFY(mixer->channelSendModel(m_sender, m_return) != nullptr);
		QVERIFY(!song->isModified());

		QMouseEvent leftPress{QEvent::MouseButtonPress, QPointF{1, 1}, QPointF{1, 1}, QPointF{1, 1},
			Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
		QApplication::sendEvent(&indicator, &leftPress);
		QVERIFY(mixer->channelSendModel(m_sender, m_return) == nullptr);
		QVERIFY(song->isModified());

		song->noteProjectSaved(QStringLiteral("send-indicator-clean.mmpz"));
		QMouseEvent createPress{QEvent::MouseButtonPress, QPointF{1, 1}, QPointF{1, 1}, QPointF{1, 1},
			Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
		QApplication::sendEvent(&indicator, &createPress);
		QVERIFY(mixer->channelSendModel(m_sender, m_return) != nullptr);
		QVERIFY(song->isModified());

		QMouseEvent cleanupPress{QEvent::MouseButtonPress, QPointF{1, 1}, QPointF{1, 1}, QPointF{1, 1},
			Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
		QApplication::sendEvent(&indicator, &cleanupPress);
		song->noteProjectSaved(QStringLiteral("send-indicator-clean.mmpz"));
	}

	void renamingFromTheStripIsOneUndoStep()
	{
		// Through mixer.rename_channel (0.5.0 wave 3), so Ctrl+Z and an agent's control.undo take it back.
		MixerChannelView* view = strip(m_sender);
		view->renameChannel();
		view->nameEdit()->setText(QStringLiteral("Lead 2"));
		emit view->nameEdit()->editingFinished();
		QCOMPARE(Engine::mixer()->mixerChannel(m_sender)->m_name, QStringLiteral("Lead 2"));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(Engine::mixer()->mixerChannel(m_sender)->m_name, QStringLiteral("Lead"));
	}

	void theRoutingMatrixKeepsOnlyTheSendsThatExist()
	{
		// A thousand channels and three sends: three cells kept, not a million (the research's 8 MB).
		QJsonArray channels;
		QStringList types;
		for (int i = 0; i < 1000; ++i)
		{
			QJsonArray sends;
			if (i > 0 && i <= 3) { sends.append(QJsonObject{{QStringLiteral("to"), QStringLiteral("ch-0")}, {QStringLiteral("amount"), 0.25}}); }
			channels.append(QJsonObject{{QStringLiteral("channel"), QStringLiteral("ch-%1").arg(i)},
				{QStringLiteral("name"), i == 0 ? QStringLiteral("Master") : QStringLiteral("Ch %1").arg(i)}, {QStringLiteral("sends"), sends}});
			types << (i == 0 ? QStringLiteral("Master") : QStringLiteral("Channel"));
		}
		RoutingMatrixModel model;
		model.setChannels(channels, types);
		QCOMPARE(model.rowCount(), 1000);
		QCOMPARE(model.columnCount(), 1000);
		QCOMPARE(model.storedCells(), 3);
		QCOMPARE(model.data(model.index(1, 0)).toString(), QStringLiteral("0.25"));
		QVERIFY(!model.data(model.index(5, 0)).isValid());
		QCOMPARE(model.headerData(0, Qt::Horizontal).toString(), QStringLiteral("Master (Master)"));
		QCOMPARE(model.data(model.index(5, 0), Qt::AccessibleTextRole).toString(), QStringLiteral("Ch 5 to Master: no send (Enter makes one)"));
		QVERIFY(model.flags(model.index(7, 7)) == Qt::NoItemFlags);  // no channel sends to itself
		QCOMPARE(model.channelId(4), QStringLiteral("ch-4"));
	}

// Deleting a channel renumbers the ones behind it. The selection follows the channel itself, not
	// the number it used to sit at: removing one in front of it left it pointing at whatever channel
	// then took that number.
	void deletingAChannelInFrontKeepsTheSelectedChannelSelected()
	{
		MixerView* mixer = getGUI()->mixerView();
		const int front = Engine::mixer()->createChannel();
		Engine::mixer()->mixerChannel(front)->m_name = QStringLiteral("Wedge");
		const int middle = Engine::mixer()->createChannel();
		Engine::mixer()->mixerChannel(middle)->m_name = QStringLiteral("Room");
		const int last = Engine::mixer()->createChannel();
		Engine::mixer()->mixerChannel(last)->m_name = QStringLiteral("Plate");
		mixer->syncWithMixer();
		// A channel with channels behind it, so the stale number names a real neighbour.
		mixer->setCurrentMixerChannel(middle);
		QCOMPARE(mixer->currentMixerChannel()->channelIndex(), middle);

		mixer->deleteChannel(front);
		QCOMPARE(Engine::mixer()->numChannels(), last);  // one fewer
		// The same channel, renumbered: its strip, and the selection with it.
		QCOMPARE(mixer->currentMixerChannel()->channelIndex(), middle - 1);
		QCOMPARE(Engine::mixer()->mixerChannel(middle - 1)->m_name, QStringLiteral("Room"));
	}

// A channel added while the mixer was open rebuilds the strips. The selection then jumped to Master
	// and the user's channel was lost, whatever the rebuild was for.
	void addingAChannelKeepsTheSelectedChannelSelected()
	{
		MixerView* mixer = getGUI()->mixerView();
		const int selected = Engine::mixer()->createChannel();
		Engine::mixer()->mixerChannel(selected)->m_name = QStringLiteral("Selected");
		mixer->syncWithMixer();
		mixer->setCurrentMixerChannel(selected);
		QCOMPARE(Engine::mixer()->mixerChannel(mixer->currentMixerChannel()->channelIndex())->m_name,
			QStringLiteral("Selected"));

		// A channel appears from outside the mixer: a script, or the TrackContainer above.
		QVERIFY(Engine::mixer()->createChannel() > 0);
		syncMixer();

		QCOMPARE(Engine::mixer()->mixerChannel(mixer->currentMixerChannel()->channelIndex())->m_name,
			QStringLiteral("Selected"));
	}

// mixer.order_by_tracks and friends reorder the channels behind the mixer's back: the count never
	// changes, so nothing rebuilt the strips and each one kept driving the channel that used to sit
	// at its number.
	void aReorderWithTheSameCountRebindsEveryStrip()
	{
		const int first = Engine::mixer()->createChannel();
		const int second = Engine::mixer()->createChannel();
		for (int i = first; i <= second; ++i)
		{
			Engine::mixer()->mixerChannel(i)->m_name = QStringLiteral("Reordered %1").arg(i);
			Engine::mixer()->mixerChannel(i)->m_volumeModel.setValue(0.25f + 0.25f * (i - first));
		}
		syncMixer();
		QCOMPARE(strip(first)->fader()->model(), &Engine::mixer()->mixerChannel(first)->m_volumeModel);

		// The same channel count, a different order: the mixer moves the channels itself.
		Engine::mixer()->moveChannelLeft(second);
		QCOMPARE(Engine::mixer()->numChannels(), second + 1);  // the count never changed
		syncMixer();

		// Every strip's controls drive the channel now at its own number, not the one it was built for.
		for (int i = 0; i < Engine::mixer()->numChannels(); ++i)
		{
			MixerChannel* channel = Engine::mixer()->mixerChannel(i);
			QCOMPARE(strip(i)->channelIndex(), i);
			QCOMPARE(strip(i)->soloButton()->model(), &channel->m_soloModel);
		}
	}

	void rightClickDoesNotChangePinRouting()
	{
		TestAudioPortsModel model{2, 2, true};
		auto* connector = new PinConnector{&model};
		QPointer<PinConnector> connectorGuard{connector};

		connector->toggleVisibility();
		QList<QWidget*> matrices;
		for (QWidget* child : connector->findChildren<QWidget*>())
		{
			if (child->toolTip().contains(QStringLiteral("input(s)"))
				|| child->toolTip().contains(QStringLiteral("output(s)")))
			{
				matrices.append(child);
			}
		}
		const bool matrixCountValid = matrices.size() == 2;
		const bool matricesVisible = matrixCountValid
			&& std::all_of(matrices.cbegin(), matrices.cend(), [](const auto* matrix) { return matrix->isVisible(); });
		const bool inputPinInitiallyDisabled = !model.in().enabled(0, 0);
		const bool outputPinInitiallyEnabled = model.out().enabled(0, 0);
		if (matricesVisible)
		{
			for (auto* matrix : matrices)
			{
				QTest::mouseClick(matrix, Qt::RightButton, Qt::NoModifier, QPoint{8, 8});
			}
		}
		const bool pinsUnchanged = !model.in().enabled(0, 0) && model.out().enabled(0, 0);
		connector->closeWindow();
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
		const bool connectorClosed = connectorGuard.isNull();

		QVERIFY(matrixCountValid);
		QVERIFY(matricesVisible);
		QVERIFY(inputPinInitiallyDisabled);
		QVERIFY(outputPinInitiallyEnabled);
		QVERIFY(connectorClosed);
		QVERIFY(pinsUnchanged);
	}

	void defaultPaletteRestoresAfterTrackPalette()
	{
		QVector<QColor> original (48);
		for (int i = 0; i < original.size(); ++i)
		{
			original[i] = QColorDialog::standardColor(i);
		}

		ColorChooser chooser;
		chooser.setPalette(ColorChooser::Palette::Track);
		chooser.setPalette(ColorChooser::Palette::Default);
		QVector<QColor> restored (48);
		for (int i = 0; i < restored.size(); ++i)
		{
			restored[i] = QColorDialog::standardColor(i);
		}

		for (int i = 0; i < original.size(); ++i)
		{
			QColorDialog::setStandardColor(i, original[i]);
		}
		QCOMPARE(restored, original);
	}

private:
	QTemporaryDir m_home;
	int m_sender = -1;
	int m_return = -1;
};

QTEST_MAIN(MixerModesTest)
#include "MixerModesTest.moc"
