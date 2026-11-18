/*
 * AutomatableModelTest.cpp
 *
 * Copyright (c) 2019-2020 Johannes Lorenz <j.git$$$lorenz-ho.me, $$$=@>
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


#include <QtTest>
#include <QMutex>
#include <QRegularExpression>

#include <memory>

#include "AudioEngine.h"
#include "AutomatableModel.h"
#include "AutomationRamp.h"
#include "ComboBoxModel.h"
#include "Engine.h"
#include "LadspaBase.h"
#include "LadspaControl.h"
#include "LinkedModelGroups.h"
#include "TempoSyncKnobModel.h"

class AutomatableModelTest : public QObject
{
	Q_OBJECT
public:
	bool m1Changed, m2Changed;
	void resetChanged() { m1Changed = m2Changed = false; }
	static bool s_rampReadLockHeld;
	static int s_rampReadProbeCalls;
	static void probeRampRead(lmms::AutomatableModel* model)
	{
		QMutex& mutex = model->m_valueBufferMutex;
		s_rampReadLockHeld = !mutex.tryLock(0);
		if (!s_rampReadLockHeld) { mutex.unlock(); }
		++s_rampReadProbeCalls;
	}

private slots: // helper slots
	void onM1Changed() { m1Changed = true; }
	void onM2Changed() { m2Changed = true; }

private slots: // tests
	//! Test that upcast and exact casts work,
	//! but no downcast or any other casts

	void initTestCase()
	{
		using namespace lmms;
		Engine::init(true);
	}

	void cleanupTestCase()
	{
		using namespace lmms;
		Engine::destroy();
	}

	void LadspaToggleValuesUsePositiveAsOn()
	{
		using namespace lmms;

		port_desc_t port{};
		port.name = QStringLiteral("Toggle");
		port.data_type = BufferDataType::Toggled;
		port.min = 0.0f;
		port.max = 1.0f;
		port.def = 0.0f;

		LadspaControl control(nullptr, &port);
		control.setValue(1.0f);
		QVERIFY(control.toggledModel()->value());

		control.setValue(-1.0f);
		QVERIFY(!control.toggledModel()->value());

		control.setValue(0.0f);
		QVERIFY(!control.toggledModel()->value());
	}

	void CastTests()
	{
		using namespace lmms;

		ComboBoxModel comboModel;
		AutomatableModel* amPtr = &comboModel;
		QVERIFY(nullptr == amPtr->dynamicCast<FloatModel>()); // not a parent class
		QCOMPARE(&comboModel, amPtr->dynamicCast<AutomatableModel>()); // parent class
		QCOMPARE(&comboModel, amPtr->dynamicCast<IntModel>()); // parent class
		QCOMPARE(&comboModel, amPtr->dynamicCast<ComboBoxModel>()); // same class

		IntModel intModel;
		IntModel* imPtr = &intModel;
		QVERIFY(nullptr == imPtr->dynamicCast<FloatModel>()); // not a parent class
		QCOMPARE(&intModel, imPtr->dynamicCast<AutomatableModel>()); // parent class
		QCOMPARE(&intModel, imPtr->dynamicCast<IntModel>()); // same class
		QVERIFY(nullptr == imPtr->dynamicCast<ComboBoxModel>()); // child class
	}

	void ComboBoxCurrentDataHandlesEmptyModel()
	{
		using namespace lmms;

		ComboBoxModel comboModel;
		QVERIFY(comboModel.currentData() == nullptr);

		comboModel.addItem(QStringLiteral("choice"));
		comboModel.clear();
		QVERIFY(comboModel.currentData() == nullptr);
	}

	void LinkTests()
	{
		using namespace lmms;

		BoolModel m1(true), m2(false);

		QObject::connect(&m1, SIGNAL(dataChanged()),
			this, SLOT(onM1Changed()));
		QObject::connect(&m2, SIGNAL(dataChanged()),
			this, SLOT(onM2Changed()));

		resetChanged();
		m1.linkToModel(&m1);
		QVERIFY(!m1Changed); // cannot link to itself
		QVERIFY(!m2Changed);
		QVERIFY(m1.countLinks() == 0);

		resetChanged();
		m1.linkToModel(&m2);
		QVERIFY(m1.value() == m2.value()); // since m1 takes the value of m2
		QVERIFY(!m2Changed); // the second model is the source
		QVERIFY(m1.countLinks() == 1);

		resetChanged();
		m1.linkToModel(&m2);
		QVERIFY(!m1Changed); // it's already linked
		QVERIFY(!m2Changed);

		resetChanged();
		BoolModel m3(false);
		m1.setValue(1.f);
		m2.setValue(1.f);
		m1.linkToModel(&m2);
		QVERIFY(m1.value());
		QVERIFY(m2.value());
		QVERIFY(!m3.value());
		m2.linkToModel(&m3); // drag m3, drop on m2
		// m2 should take m3's (0) value
		QVERIFY(m2.value() == m3.value());
		QVERIFY(!m3.value());
		QVERIFY(m1.countLinks() == 2);
	}

	//! Cluster 2647: valueBuffer() fills its period cache from the published
	//! automation ramp without holding the legacy per-model mutex. The probe runs
	//! immediately after that read and uses tryLock(0), so this check has no wait,
	//! thread or wall-clock timing.
	void readingTheAutomationRampTakesNoMutex()
	{
		using namespace lmms;
		const f_cnt_t frames = Engine::audioEngine()->framesPerPeriod();
		FloatModel model(0.f, 0.f, 1.f, 0.001f);
		AutomationRamp ramp;
		QVERIFY(ramp.addKnot(0, 0.f));
		QVERIFY(ramp.addKnot(frames - 1, 1.f));
		model.publishAutomationRamp(ramp);

		s_rampReadLockHeld = false;
		s_rampReadProbeCalls = 0;
		AutomatableModel::setValueBufferRampReadProbeForTest(&AutomatableModelTest::probeRampRead);
		ValueBuffer* const buffer = model.valueBuffer();
		AutomatableModel::setValueBufferRampReadProbeForTest(nullptr);

		QCOMPARE(s_rampReadProbeCalls, 1);
		QVERIFY2(!s_rampReadLockHeld,
			"valueBuffer() held the model mutex across the automation-ramp read (cluster 2647)");
		QVERIFY2(buffer != nullptr, "the published ramp produced no sample-exact buffer");
		QCOMPARE(buffer->length(), frames);
		QVERIFY(buffer->values()[0] < 0.001f);
		QVERIFY(buffer->values()[frames - 1] > 0.999f);
	}

	void customMeterChangeUpdatesSyncOnceAfterReenteringCustom()
	{
		using namespace lmms;

		TempoSyncKnobModel model(1.f, 0.f, 1000.f, 1.f, 1.f, nullptr);
		int updateCount = 0;
		connect(&model, &TempoSyncKnobModel::syncModeChanged,
			[&updateCount](TempoSyncKnobModel::SyncMode) { ++updateCount; });

		model.setSyncMode(TempoSyncKnobModel::SyncMode::Custom);
		model.setSyncMode(TempoSyncKnobModel::SyncMode::None);
		model.setSyncMode(TempoSyncKnobModel::SyncMode::Custom);
		QCOMPARE(updateCount, 3);

		model.getCustomMeterModel().setNumerator(3);
		QCOMPARE(updateCount, 4);
	}

	// new-p9-224: LinkedModelGroup is keyed by model name. A second model under a taken
	// name must not be stored, announced or connected, and must not displace the original.
	void linkedGroupRefusesDuplicateNameAndAnnouncesNothingForIt()
	{
		using namespace lmms;

		LinkedModelGroup group(nullptr);
		QSignalSpy added(&group, &LinkedModelGroup::modelAdded);
		FloatModel original(0.f, 0.f, 1.f, 0.1f);
		FloatModel duplicate(0.f, 0.f, 1.f, 0.1f);
		group.addModel(&original, QStringLiteral("x"));

		QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("refusing a second model named \"x\"")));
		group.addModel(&duplicate, QStringLiteral("x"));

		QCOMPARE(group.modelNum(), std::size_t{1});
		QVERIFY(group.getModel("x") == &original);
		QCOMPARE(added.count(), 1);
	}

	void linkedGroupDestroyingRefusedDuplicateKeepsTheOriginal()
	{
		using namespace lmms;

		LinkedModelGroup group(nullptr);
		QSignalSpy removed(&group, &LinkedModelGroup::modelRemoved);
		FloatModel original(0.f, 0.f, 1.f, 0.1f);
		auto duplicate = std::make_unique<FloatModel>(0.f, 0.f, 1.f, 0.1f);
		group.addModel(&original, QStringLiteral("x"));

		QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("refusing a second model named \"x\"")));
		group.addModel(duplicate.get(), QStringLiteral("x"));
		duplicate.reset();

		QCOMPARE(removed.count(), 0);
		QCOMPARE(group.modelNum(), std::size_t{1});
		QVERIFY(group.getModel("x") == &original);
	}

	void linkedGroupRemoveControlOfRefusedDuplicateKeepsTheOriginal()
	{
		using namespace lmms;

		LinkedModelGroup group(nullptr);
		QSignalSpy removed(&group, &LinkedModelGroup::modelRemoved);
		FloatModel original(0.f, 0.f, 1.f, 0.1f);
		FloatModel duplicate(0.f, 0.f, 1.f, 0.1f);
		group.addModel(&original, QStringLiteral("x"));

		QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("refusing a second model named \"x\"")));
		group.addModel(&duplicate, QStringLiteral("x"));
		group.removeControl(&duplicate);

		QCOMPARE(removed.count(), 0);
		QCOMPARE(group.modelNum(), std::size_t{1});
		QVERIFY(group.getModel("x") == &original);
	}

	// Control: the stored model, once destroyed, is still removed and announced.
	void linkedGroupDestroyingStoredModelStillRemovesIt()
	{
		using namespace lmms;

		LinkedModelGroup group(nullptr);
		QSignalSpy removed(&group, &LinkedModelGroup::modelRemoved);
		auto original = std::make_unique<FloatModel>(0.f, 0.f, 1.f, 0.1f);
		group.addModel(original.get(), QStringLiteral("x"));

		original.reset();

		QCOMPARE(removed.count(), 1);
		QCOMPARE(group.modelNum(), std::size_t{0});
	}
};

bool AutomatableModelTest::s_rampReadLockHeld = false;
int AutomatableModelTest::s_rampReadProbeCalls = 0;

QTEST_GUILESS_MAIN(AutomatableModelTest)
#include "AutomatableModelTest.moc"
