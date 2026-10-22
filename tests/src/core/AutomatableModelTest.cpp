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
#include <QDomDocument>
#include <QDomElement>
#include <QPointer>
#include "AutomatableModel.h"
#include "ComboBoxModel.h"
#include "ControllerConnection.h"
#include "Engine.h"
#include "LadspaBase.h"
#include "LadspaControl.h"
#include "TempoSyncKnobModel.h"

namespace
{
//! QCOMPARE cannot take `model.value<float>()`: the macro parses the angle
//! brackets as relational operators (ModulationAudioViewTest says the same).
float valueOf(const lmms::AutomatableModel& model) { return model.value<float>(); }
} // namespace

class AutomatableModelTest : public QObject
{
	Q_OBJECT
public:
	bool m1Changed, m2Changed;
	void resetChanged() { m1Changed = m2Changed = false; }

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

	//! ED-191 / cluster 5524: the "last touched" model is the one a PERSON (or
	//! a command) changed - not the one a project load, or a model's initial
	//! value, happened to write last. Every write a load makes goes through
	//! loadSettings()/setInitValue() (the attribute form, the element form and
	//! the automation-compat path), so both entry points are covered here.
	void initializationAndLoadingAreNotTouches()
	{
		using namespace lmms;

		FloatModel edited(0.f, 0.f, 100.f, 1.f);
		FloatModel loaded(0.f, 0.f, 100.f, 1.f);

		// A person changes a control: that is the last touched one.
		edited.setValue(5.f);
		QCOMPARE(AutomatableModel::lastTouched(), edited.id());

		// A model's initial value is the state it is BUILT with, not an edit.
		FloatModel initialised(0.f, 0.f, 100.f, 1.f);
		initialised.setInitValue(42.f);
		QCOMPARE(valueOf(initialised), 42.f);
		QCOMPARE(AutomatableModel::lastTouched(), edited.id());

		// The project file writes a value into another model (the attribute
		// form - the shape every non-automated control is saved in).
		QDomDocument doc;
		QDomElement element = doc.createElement(QStringLiteral("control"));
		doc.appendChild(element);
		element.setAttribute(QStringLiteral("value"), QStringLiteral("42"));
		loaded.loadSettings(element, QStringLiteral("value"));
		QCOMPARE(valueOf(loaded), 42.f);
		QCOMPARE(AutomatableModel::lastTouched(), edited.id());

		// The element form (a model saved as <value value="..."/>).
		QDomElement child = doc.createElement(QStringLiteral("value"));
		child.setAttribute(QStringLiteral("id"), QString::number(loaded.id()));
		child.setAttribute(QStringLiteral("value"), QStringLiteral("77"));
		element.appendChild(child);
		loaded.loadSettings(element, QStringLiteral("value"));
		QCOMPARE(valueOf(loaded), 77.f);
		QCOMPARE(AutomatableModel::lastTouched(), edited.id());

		// A load that finds no stored value resets the model; that is still a
		// load, not an edit.
		loaded.loadSettings(doc.createElement(QStringLiteral("empty")), QStringLiteral("value"));
		QCOMPARE(AutomatableModel::lastTouched(), edited.id());

		// A person changes the loaded model: NOW it is the last touched one.
		loaded.setValue(11.f);
		QCOMPARE(AutomatableModel::lastTouched(), loaded.id());
	}

	//! Cluster new-p15-273: replacing a controller connection gives up the old
	//! one - it is disconnected and disposed of, not left alive driving the
	//! model - and a connection that dies later cannot clear the connection
	//! that replaced it.
	void replacingTheControllerConnectionGivesUpTheOldOne()
	{
		using namespace lmms;

		FloatModel model(0.f, 0.f, 100.f, 1.f);
		QPointer<ControllerConnection> first(new ControllerConnection(nullptr));
		model.setControllerConnection(first.data());
		QCOMPARE(model.controllerConnection(), first.data());
		QVERIFY(!first.isNull());

		// Replacing connection A with B disposes of A. Leaving A alive leaked it
		// and kept its valueChanged() wired to the model.
		QPointer<ControllerConnection> second(new ControllerConnection(nullptr));
		model.setControllerConnection(second.data());
		QVERIFY2(first.isNull(), "the replaced controller connection was not disposed of");
		QCOMPARE(model.controllerConnection(), second.data());

		// A connection that is destroyed while another one is in place must not
		// clear the connection the model holds: the old connection's destroyed()
		// handler used to do exactly that.
		ControllerConnection* const stranger = new ControllerConnection(nullptr);
		QObject::connect(stranger, &QObject::destroyed, &model,
			&AutomatableModel::unlinkControllerConnection);
		delete stranger;
		QCOMPARE(model.controllerConnection(), second.data());

		// Giving the connection up disposes of it as well (the model owns it).
		model.setControllerConnection(nullptr);
		QVERIFY2(second.isNull(), "the connection the model gave up was not disposed of");
		QVERIFY(model.controllerConnection() == nullptr);

		// A connection the model still holds when it is destroyed is disposed of
		// (the ownership contract the destructor has always implemented).
		{
			FloatModel owned(0.f, 0.f, 100.f, 1.f);
			QPointer<ControllerConnection> current(new ControllerConnection(nullptr));
			owned.setControllerConnection(current.data());
			QCOMPARE(owned.controllerConnection(), current.data());
		}
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
};

QTEST_GUILESS_MAIN(AutomatableModelTest)
#include "AutomatableModelTest.moc"
