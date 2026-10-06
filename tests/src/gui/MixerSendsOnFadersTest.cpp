/*
 * MixerSendsOnFadersTest.cpp - 0.5.0 theme 6: one send flipped across every fader, named and labelled
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

/*! The owner's decision on theme 6 (Pro Tools / Logic style): window.mixer_view's sends_on_faders flips
 *  one chosen send across every fader; a banner across the mixer names it; every fader says what it
 *  drives; a channel with no send there keeps its fader in place - dimmed, still, reading "No send" -
 *  where it used to lose it. The banner's picker and button go through the same command. Needs the real
 *  application (a Fader cannot be built without it). */

#include <QtTest>

#include <QComboBox>
#include <QGraphicsEffect>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

#include "ControlMixerSupport.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Fader.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "MainWindow.h"
#include "Mixer.h"
#include "MixerOverlays.h"
#include "MixerSendsBanner.h"
#include "MixerView.h"
#include "Song.h"
#include "Track.h"
#include "TrackFolder.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

QString ch(int index) { return control::channelIdOf(Engine::mixer()->mixerChannel(index)); }

//! A return bus named @a name: bus.create names its own ("Bus 1"), so the name is set on the channel after.
int createReturn(const QString& name)
{
	const ControlResult created = run(QStringLiteral("bus.create"));
	const int index = created.ok ? created.result.value(QStringLiteral("index")).toInt(-1) : -1;
	if (index > 0) { Engine::mixer()->mixerChannel(index)->m_name = name; }
	return index;
}

} // namespace

class MixerSendsOnFadersTest : public QObject
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
		m_silent = Engine::mixer()->createChannel();
		m_return = createReturn(QStringLiteral("Verb"));
		QVERIFY(m_return > 0);
		QVERIFY(run(QStringLiteral("mixer.send_to"), {{QStringLiteral("channel"), ch(m_sender)}, {QStringLiteral("to"), ch(m_return)},
			{QStringLiteral("amount"), 0.5}}).ok);
		sync();
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void theChosenSendIsNamedAndEveryFaderLabelled()
	{
		QVERIFY(run(QStringLiteral("window.mixer_view"), {{QStringLiteral("sends_on_faders"), ch(m_return)}}).ok);
		sync();
		MixerView* mixer = getGUI()->mixerView();
		MixerSendsBanner* banner = mixer->sendsBanner();
		QVERIFY(banner->isVisibleTo(mixer));
		QVERIFY(banner->message()->text().contains(QStringLiteral("Verb")));
		QCOMPARE(banner->picker()->currentData().toString(), ch(m_return));

		MixerChannelView* sender = mixer->channelView(m_sender);
		QCOMPARE(sender->fader()->model(), control::findMixerRoute(m_sender, m_return)->amount());
		QVERIFY(sender->fader()->isEnabled());
		QVERIFY(sender->faderTarget()->isVisibleTo(mixer));
		QVERIFY(sender->faderTarget()->text().contains(QStringLiteral("Verb")));
		// The return's own fader and the master's stay their level, and say so.
		for (const int index : {m_return, 0})
		{
			MixerChannelView* view = mixer->channelView(index);
			QCOMPARE(view->fader()->model(), &Engine::mixer()->mixerChannel(index)->m_volumeModel);
			QCOMPARE(view->faderTarget()->text(), QStringLiteral("Volume"));
		}
	}

	void aChannelWithoutTheSendKeepsADimmedStillFader()
	{
		MixerView* mixer = getGUI()->mixerView();
		MixerChannelView* silent = mixer->channelView(m_silent);
		QCOMPARE(mixeroverlays::faderRole(m_silent), mixeroverlays::FaderRole::NoSend);
		QVERIFY(silent->fader()->isVisibleTo(mixer));  // the strip never collapses (it used to hide)
		QVERIFY(!silent->fader()->isEnabled());
		QVERIFY(silent->fader()->graphicsEffect() != nullptr);  // dimmed
		QCOMPARE(silent->fader()->model(), silent->noSendModel());
		QCOMPARE(silent->faderTarget()->text(), QStringLiteral("No send"));
		// The arrow keys move the current strip's fader: not a "No send" one; a send one moves its send.
		mixer->setCurrentMixerChannel(m_silent);
		QTest::keyClick(mixer, Qt::Key_Up);
		QCOMPARE(silent->noSendModel()->value(), 0.f);
		const float before = control::findMixerRoute(m_sender, m_return)->amount()->value();
		mixer->setCurrentMixerChannel(m_sender);
		QTest::keyClick(mixer, Qt::Key_Up);
		QVERIFY(control::findMixerRoute(m_sender, m_return)->amount()->value() > before);
		QCOMPARE(Engine::mixer()->mixerChannel(m_sender)->m_volumeModel.value(), 1.f);  // not the level
	}

	void theBannersPickerMovesToAnotherReturn()
	{
		const int other = createReturn(QStringLiteral("Delay"));
		QVERIFY(other > 0);
		sync();
		MixerView* mixer = getGUI()->mixerView();
		QComboBox* picker = mixer->sendsBanner()->picker();
		const int row = picker->findData(ch(other));
		QVERIFY(row >= 0);
		emit picker->activated(row);
		QCOMPARE(mixeroverlays::sendsOnFaders(), other);
		sync();
		QVERIFY(mixer->sendsBanner()->message()->text().contains(QStringLiteral("Delay")));
		QCOMPARE(mixeroverlays::faderRole(m_sender), mixeroverlays::FaderRole::NoSend);  // it sends to Verb only
		QVERIFY(!mixer->channelView(m_sender)->fader()->isEnabled());
	}

	// A return's strip carries a bus-specific tooltip ("(return - sends into it are
	// pre-fader)"). Renaming the strip rebuilds the tooltip from the name alone, so a
	// renamed bus stopped reading as a return until the mixer was rebuilt.
	void renamingABusKeepsItsReturnTooltip()
	{
		const int index = createReturn(QStringLiteral("Plate"));
		QVERIFY(index > 0);
		sync();
		MixerChannelView* view = getGUI()->mixerView()->channelView(index);
		QVERIFY(view != nullptr);
		QVERIFY(Engine::mixer()->mixerChannel(index)->isBus());
		QVERIFY(view->toolTip().contains(QStringLiteral("Return")));

		view->renameChannel();
		view->nameEdit()->setText(QStringLiteral("Plate Verb"));
		// renameFinished is a private slot (the line edit's editingFinished is its only
		// caller), so drive it the way that signal does - through the meta-object.
		QVERIFY(QMetaObject::invokeMethod(view, "renameFinished"));

		// renameFinished() itself rebuilds the tooltip from the plain name, so check the
		// state the mixer settles into: the next sync pass re-describes the strip.
		sync();
		QVERIFY(view->toolTip().contains(QStringLiteral("Plate Verb")));
		QVERIFY(view->toolTip().contains(QStringLiteral("Return")));
		QVERIFY(view->nameEdit()->text().contains(QStringLiteral("Plate Verb")));
	}

	// A folder inside a folder: collapsedChannels() has to reach the nested folder's own children,
	// or collapsing it while its parent stays expanded leaves its strips on screen.
	void collapsingANestedFolderHidesItsChannels()
	{
		Song* song = Engine::getSong();
		auto* outer = dynamic_cast<TrackFolder*>(Track::create(Track::Type::Folder, song));
		QVERIFY(outer != nullptr);
		auto* inner = dynamic_cast<TrackFolder*>(Track::create(Track::Type::Folder, song));
		QVERIFY(inner != nullptr);
		inner->setParentFolder(outer);
		Track* leaf = Track::create(Track::Type::Sample, song);
		leaf->setParentFolder(inner);
		QCoreApplication::processEvents();

		IntModel* leafChannel = leaf->mixerChannelModel();
		QVERIFY(leafChannel != nullptr);
		// A new track starts on the master (0); give it its own strip so the collapse has one to hide.
		leafChannel->setValue(Engine::mixer()->createChannel());
		const int index = leafChannel->value();
		QVERIFY(index > 0);
		QVERIFY(!mixeroverlays::foldedChannels().contains(index));

		outer->setCollapsed(false);
		inner->setCollapsed(true);
		QVERIFY2(mixeroverlays::foldedChannels().contains(index),
			"a collapsed nested folder does not hide its children's strips");
		QVERIFY(!outer->isCollapsed());

		outer->setCollapsed(true);
		QVERIFY(mixeroverlays::foldedChannels().contains(index));

		sync();
		QVERIFY(!getGUI()->mixerView()->channelView(index)->isVisibleTo(getGUI()->mixerView()));
	}

	void theBannersButtonPutsTheFadersBack()
	{
		MixerView* mixer = getGUI()->mixerView();
		mixer->sendsBanner()->exitButton()->click();
		QCOMPARE(mixeroverlays::sendsOnFaders(), -1);
		sync();
		QVERIFY(!mixer->sendsBanner()->isVisibleTo(mixer));
		for (const int index : {0, m_sender, m_silent, m_return})
		{
			MixerChannelView* view = mixer->channelView(index);
			QCOMPARE(view->fader()->model(), &Engine::mixer()->mixerChannel(index)->m_volumeModel);
			QVERIFY(view->fader()->isEnabled());
			QVERIFY(view->fader()->graphicsEffect() == nullptr);
			QVERIFY(!view->faderTarget()->isVisibleTo(mixer));
		}
	}

private:
	//! The view's half-second sync, now: a count change rebuilds the strips, the next pass applies the modes.
	static void sync()
	{
		getGUI()->mixerView()->syncWithMixer();
		getGUI()->mixerView()->syncWithMixer();
	}

	QTemporaryDir m_home;
	int m_sender = -1;
	int m_silent = -1;
	int m_return = -1;
};

QTEST_MAIN(MixerSendsOnFadersTest)
#include "MixerSendsOnFadersTest.moc"
