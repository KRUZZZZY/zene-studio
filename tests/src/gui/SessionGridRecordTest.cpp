/*
 * SessionGridRecordTest.cpp - the launcher grid's record affordance (0.5.0 L02, WF-055)
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
 */

/*! WF-055 from the interface: an empty cell's right-click menu records audio into it (session.record_slot,
 *  declared on the entry), the cell says it is recording - in its text and to a screen reader - the column's stop
 *  button ends the take on its quantised stop, and the grid lands the take it started as the cell's clip (one undo
 *  step) without being asked again. A take the grid did NOT start (an agent's, on the socket) is left for its
 *  owner to land. A filled cell offers no recording. The engine is the real one on the Dummy device; the test
 *  drives the periods. */

#include <QtTest>

#include <QJsonObject>
#include <QMenu>
#include <QTemporaryDir>
#include <QToolButton>

#include <vector>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "MenuTestSupport.h"
#include "SampleFrame.h"
#include "SessionScheduler.h"
#include "SessionGridView.h"
#include "SessionModel.h"
#include "Song.h"

using namespace lmms;
using namespace lmms::gui;
using lmms::test::menuAction;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = QJsonObject())
{
	return ControlRegistry::instance()->invoke(id, args);
}

QString takeState()
{
	return run(QStringLiteral("session.record_slot_status")).result.value(QStringLiteral("state")).toString();
}

} // namespace

class SessionGridRecordTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();  // the test drives the periods itself
		QVERIFY(m_home.isValid());
		ConfigManager::inst()->setWorkingDir(m_home.path());  // the default take lands in <work>/samples/Recordings
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void init()
	{
		Engine::getSong()->clearProject();
		QVERIFY(run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("sample")}}).ok);
		QVERIFY(run(QStringLiteral("session.set_grid"), {{QStringLiteral("tracks"), 1}, {QStringLiteral("scenes"), 2}}).ok);
		QVERIFY(run(QStringLiteral("session.set_quantisation"), {{QStringLiteral("quantisation"), QStringLiteral("none")}}).ok);
	}

	void anEmptyCellRecordsFromItsMenuAndTheGridLandsTheTake()
	{
		SessionGridView view;
		view.refresh();
		QMenu menu;
		view.populateMenu(&menu, view.cellButton(0, 0));
		QAction* record = menuAction(&menu, {QStringLiteral("Record Audio Into Cell")});
		QVERIFY2(record != nullptr, "an empty cell's menu has no record entry");
		QCOMPARE(record->property("controlCommand").toString(), QStringLiteral("session.record_slot"));
		QVERIFY(record->isEnabled());
		record->trigger();
		for (int i = 0; i < 400 && takeState() != QStringLiteral("recording"); ++i) { period(); }
		QCOMPARE(takeState(), QStringLiteral("recording"));
		view.refresh();
		QVERIFY2(view.cellButton(0, 0)->text().contains(QStringLiteral("Rec")), qPrintable(view.cellButton(0, 0)->text()));
		QVERIFY(view.cellButton(0, 0)->accessibleName().endsWith(QStringLiteral("recording")));

		for (int i = 0; i < 40; ++i) { period(); }
		QTest::mouseClick(view.stopButton(0), Qt::LeftButton);  // the column's stop: the cell's quantised stop
		for (int i = 0; i < 400 && takeState() != QStringLiteral("done"); ++i) { period(); }
		QCOMPARE(takeState(), QStringLiteral("done"));
		view.refresh();  // the grid lands the take it started
		QCOMPARE(takeState(), QStringLiteral("idle"));
		const ClipSlot& slot = Engine::getSong()->sessionModel().slot(0, 0);
		QVERIFY(slot.type() == ClipSlot::Type::Audio);
		QVERIFY2(slot.audioSource().startsWith(ConfigManager::inst()->userSamplesDir()), qPrintable(slot.audioSource()));
		QVERIFY(QFileInfo::exists(slot.audioSource()));
		QVERIFY(view.statusLine().contains(QStringLiteral("landed")));
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // one step takes the cell back
		QVERIFY(Engine::getSong()->sessionModel().slot(0, 0).isEmpty());
	}

	void aTakeTheGridDidNotStartIsLeftForItsOwner()
	{
		SessionGridView view;
		view.refresh();
		QVERIFY(run(QStringLiteral("session.record_slot"), {{QStringLiteral("track"), 0}, {QStringLiteral("scene"), 1},
			{QStringLiteral("bars"), 1}}).ok);
		for (int i = 0; i < 2000 && takeState() != QStringLiteral("done"); ++i) { period(); }
		QCOMPARE(takeState(), QStringLiteral("done"));
		view.refresh();
		QCOMPARE(takeState(), QStringLiteral("done"));  // not landed by the grid
		QVERIFY(run(QStringLiteral("session.record_slot"), {{QStringLiteral("track"), 0}, {QStringLiteral("scene"), 1},
			{QStringLiteral("cancel"), true}}).ok);
	}

	void aFilledCellOffersNoRecording()
	{
		QVERIFY(run(QStringLiteral("session.set_slot"), {{QStringLiteral("track"), 0}, {QStringLiteral("scene"), 0},
			{QStringLiteral("type"), QStringLiteral("audio")}, {QStringLiteral("source"), QStringLiteral("kick.wav")}}).ok);
		SessionGridView view;
		view.refresh();
		QMenu menu;
		view.populateMenu(&menu, view.cellButton(0, 0));
		QAction* record = menuAction(&menu, {QStringLiteral("Record Audio Into Cell")});
		QVERIFY(record != nullptr);
		QVERIFY(!record->isEnabled());
	}

	void landingAMidiSlotIsRefusedWithoutConsumingItsPerformance()
	{
		QVERIFY(run(QStringLiteral("session.set_slot"), {{QStringLiteral("track"), 0},
			{QStringLiteral("scene"), 0}, {QStringLiteral("type"), QStringLiteral("midi")},
			{QStringLiteral("pattern"), 0}}).ok);
		auto& recorder = Engine::getSong()->sessionScheduler().arrangementRecorder();
		recorder.setArmed(true);
		QVERIFY(recorder.recordStart(0, 0, 0));
		QVERIFY(recorder.recordStop(0, 0, 192));

		const ControlResult landed = run(QStringLiteral("session.arrangement_record_land"));
		QVERIFY(!landed.ok);
		QCOMPARE(recorder.pending(), std::size_t(2));
		QCOMPARE(Engine::getSong()->tracks().front()->numOfClips(), 0);
	}

private:
	void period()
	{
		AudioEngine* engine = Engine::audioEngine();
		std::vector<SampleFrame> block(static_cast<std::size_t>(engine->framesPerPeriod()), SampleFrame(0.25f, -0.25f));
		engine->pushInputFrames(block.data(), engine->framesPerPeriod());
		engine->renderNextPeriod();
	}

	QTemporaryDir m_home;
};

QTEST_MAIN(SessionGridRecordTest)
#include "SessionGridRecordTest.moc"
