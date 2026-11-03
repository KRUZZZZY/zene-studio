/*
 * MixerBusFreezeMenuTest.cpp - the bus strip's Freeze bus / Unfreeze bus entry (Q050-124)
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

/*! The strip's context menu is what a person uses: right-click the bus strip, pick "Freeze bus", and the bus is
 *  frozen through freeze.bus - read back through bus.list, the agent's view; the same menu then offers "Unfreeze
 *  bus" (freeze.unfreeze_bus). A channel that is not a bus offers neither. The child render is replaced by a hook
 *  writing a short take (the QtTest binary is not the application). */

#include <QtTest>

#include <QApplication>
#include <QContextMenuEvent>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QTemporaryDir>
#include <QTimer>

#include <memory>
#include <vector>

#include <sndfile.h>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "MenuTestSupport.h"
#include "Mixer.h"
#include "MixerBusFreeze.h"
#include "MixerBusFreezeMenu.h"
#include "MixerChannelView.h"
#include "MixerView.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

int s_renders = 0;

bool writeShortTake(MixerChannel*, const QString& stagedPath, int rate, QString*)
{
	++s_renders;
	std::vector<float> frames(2 * 4096, 0.25f);
	SF_INFO info{};
	info.samplerate = rate;
	info.channels = 2;
	info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
	SNDFILE* file = sf_open(QFile::encodeName(stagedPath).constData(), SFM_WRITE, &info);
	if (file == nullptr) { return false; }
	const bool written = sf_writef_float(file, frames.data(), 4096) == 4096;
	return sf_close(file) == 0 && written;
}

QJsonObject busEntry(const QString& channel)
{
	const QJsonArray buses = ControlRegistry::instance()->invoke(QStringLiteral("bus.list")).result
		.value(QStringLiteral("buses")).toArray();
	for (const QJsonValue& value : buses)
	{
		if (value.toObject().value(QStringLiteral("id")).toString() == channel) { return value.toObject(); }
	}
	return {};
}

//! What one strip-menu pick saw; shared with the polling timers so a late timer never touches a dead frame.
struct Pick
{
	QString entry;
	bool offered = false;
	bool done = false;
	QString command;
};

void pollForMenu(const std::shared_ptr<Pick>& pick, int triesLeft)
{
	QTimer::singleShot(10, [pick, triesLeft] {
		if (pick->done) { return; }
		auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
		if (menu == nullptr)
		{
			if (triesLeft > 0) { pollForMenu(pick, triesLeft - 1); }
			return;
		}
		if (QAction* action = lmms::test::menuAction(menu, {pick->entry}))
		{
			pick->offered = true;
			pick->command = action->property("controlCommand").toString();
			action->trigger();
		}
		menu->close();
	});
}

//! Opens @a view's context menu the way a right-click does and, once it is up, triggers the entry named @a entry
//! (when it is there) and closes the menu. Returns the entry's command; @a offered says whether it was there.
QString pickFromStripMenu(MixerChannelView* view, const QString& entry, bool* offered)
{
	auto pick = std::make_shared<Pick>();
	pick->entry = entry;
	pollForMenu(pick, 300);
	QContextMenuEvent event(QContextMenuEvent::Mouse, QPoint(5, 5), view->mapToGlobal(QPoint(5, 5)));
	QApplication::sendEvent(view, &event);
	pick->done = true;
	*offered = pick->offered;
	return pick->command;
}

} // namespace

class MixerBusFreezeMenuTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		guitest::startGui(m_home);
		QVERIFY(getGUI() != nullptr);
		Engine::audioEngine()->audioDev()->stopProcessing();
		ControlRegistry::setReady(true);
		busfreeze::setRenderHookForTest(&writeShortTake);

		Mixer* mixer = Engine::mixer();
		m_source = mixer->createChannel();
		m_bus = mixer->createBusChannel();
		mixer->createChannelSend(m_source, m_bus, 1.0f);
		getGUI()->mixerView()->refreshDisplay();
		m_busId = control::channelIdOf(mixer->mixerChannel(m_bus));
	}

	void cleanupTestCase()
	{
		busfreeze::setRenderHookForTest(nullptr);
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! Right-click the bus strip, Freeze bus: freeze.bus ran (bus.list says frozen); the menu now unfreezes it.
	void theBusStripMenuFreezesAndUnfreezesTheBus()
	{
		MixerChannelView* strip = getGUI()->mixerView()->channelView(m_bus);
		QVERIFY(strip != nullptr);
		bool offered = false;
		QCOMPARE(pickFromStripMenu(strip, QStringLiteral("Freeze bus"), &offered), QStringLiteral("freeze.bus"));
		QVERIFY(offered);
		QCOMPARE(s_renders, 1);
		QCOMPARE(busEntry(m_busId).value(QStringLiteral("frozen")).toBool(), true);
		QCOMPARE(busEntry(m_busId).value(QStringLiteral("frozen_audio_ready")).toBool(), true);

		QCOMPARE(pickFromStripMenu(strip, QStringLiteral("Unfreeze bus"), &offered), QStringLiteral("freeze.unfreeze_bus"));
		QVERIFY(offered);
		QCOMPARE(busEntry(m_busId).value(QStringLiteral("frozen")).toBool(), false);
		// One undo step per edit: undo the unfreeze, and the bus is frozen again.
		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("control.undo")).ok);
		QCOMPARE(busEntry(m_busId).value(QStringLiteral("frozen")).toBool(), true);
		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("freeze.unfreeze_bus"),
			{{QStringLiteral("channel"), m_busId}}).ok);
	}

	//! A channel that is not a bus has no freeze entry (freeze.track freezes its track).
	void aChannelStripOffersNoBusFreeze()
	{
		bool offered = true;
		pickFromStripMenu(getGUI()->mixerView()->channelView(m_source), QStringLiteral("Freeze bus"), &offered);
		QVERIFY(!offered);
		QMenu menu;
		QCOMPARE(addBusFreezeAction(&menu, Engine::mixer()->mixerChannel(m_source)), static_cast<QAction*>(nullptr));
		QVERIFY(addBusFreezeAction(&menu, Engine::mixer()->mixerChannel(m_bus)) != nullptr);
	}

private:
	QTemporaryDir m_home;
	int m_source = 0;
	int m_bus = 0;
	QString m_busId;
};

QTEST_MAIN(MixerBusFreezeMenuTest)
#include "MixerBusFreezeMenuTest.moc"
