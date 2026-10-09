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

#include <QTemporaryDir>

#include "AutomatableButton.h"
#include "Engine.h"
#include "Fader.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "Knob.h"
#include "MainWindow.h"
#include "Mixer.h"
#include "MixerChannelView.h"
#include "MixerView.h"

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
	}

	void cleanupTestCase()
	{
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
		const auto clickAndRoute = [mixer, masterView, targetView, master, target](QWidget* control)
		{
			mixer->setCurrentMixerChannel(masterView);
			QTest::mouseClick(control, Qt::LeftButton, Qt::NoModifier, control->rect().center());
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
	}

private:
	QTemporaryDir m_home;
};

QTEST_MAIN(MixerSelectionIdentityTest)
#include "MixerSelectionIdentityTest.moc"
