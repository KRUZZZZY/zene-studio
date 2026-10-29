/*
 * VstEffectControlsTest.cpp - parameter-model lifetime regression coverage
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

#include "../../../plugins/VstBase/VstPlugin.h"
#include "../../../plugins/VstEffect/VstEffectControls.h"

#include <QDomDocument>
#include <QMap>
#include <QtTest>

#include "Engine.h"

namespace lmms
{

namespace
{

const VstEffectControls* testControls = nullptr;
QMap<QString, QString> testParameterDump;

} // namespace

const QMap<QString, QString>* vstEffectControlsTestParameterDump(const VstEffectControls* controls)
{
	return controls == testControls ? &testParameterDump : nullptr;
}

class VstEffectControlsTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase();
	void cleanupTestCase();
	void repeatedSettingsLoadsDoNotRetainOldModels();
	void labelContainingColonKeepsValueField();
	void missingParameterEntryKeepsSavedValue();
	void savedRestoreKeepsColonLabelsAndSkipsMalformedEntries();
};

void VstEffectControlsTest::initTestCase()
{
	Engine::init(true);
	Engine::audioEngine()->audioDev()->stopProcessing();
}

void VstEffectControlsTest::cleanupTestCase()
{
	Engine::destroy();
}

void VstEffectControlsTest::repeatedSettingsLoadsDoNotRetainOldModels()
{
	VstEffectControls controls(nullptr);
	QDomDocument document;
	const auto settings = document.createElement("vsteffectcontrols");
	testControls = &controls;
	testParameterDump = {
		{"param0", "0:first:0.25"},
		{"param1", "1:second:0.75"}
	};

	controls.loadSettings(settings);
	QCOMPARE(controls.findChildren<FloatModel*>().size(), 2);

	controls.loadSettings(settings);
	QCOMPARE(controls.findChildren<FloatModel*>().size(), 2);
	testControls = nullptr;
	testParameterDump.clear();
}

void VstEffectControlsTest::labelContainingColonKeepsValueField()
{
	// A dump entry is "index:label:value"; a label may contain ':' itself,
	// so the value must still be read from the last field.
	VstEffectControls controls(nullptr);
	QDomDocument document;
	const auto settings = document.createElement("vsteffectcontrols");
	testControls = &controls;
	testParameterDump = {
		{"param0", "0:Gain: 3 dB:0.25"}
	};

	controls.loadSettings(settings);
	const auto models = controls.findChildren<FloatModel*>();
	QVERIFY(models.size() == 1);
	QVERIFY(qAbs(models.at(0)->value() - 0.25f) < 1e-4f);
	testControls = nullptr;
	testParameterDump.clear();
}

void VstEffectControlsTest::missingParameterEntryKeepsSavedValue()
{
	// param1 is absent from the dump, so there is no value to apply; the
	// project's saved value for that knob must survive the load.
	VstEffectControls controls(nullptr);
	QDomDocument document;
	auto settings = document.createElement("vsteffectcontrols");
	settings.setAttribute("param1", "0.6");
	testControls = &controls;
	testParameterDump = {
		{"param0", "0:first:0.25"},
		{"param2", "2:third:0.5"}
	};

	controls.loadSettings(settings);
	const auto models = controls.findChildren<FloatModel*>();
	QVERIFY(models.size() == 2);
	QVERIFY(qAbs(models.at(0)->value() - 0.25f) < 1e-4f);
	QVERIFY(qAbs(models.at(1)->value() - 0.6f) < 1e-4f);
	testControls = nullptr;
	testParameterDump.clear();
}

void VstEffectControlsTest::savedRestoreKeepsColonLabelsAndSkipsMalformedEntries()
{
	// The no-chunk restore reads the entries saved by VstPlugin::saveSettings,
	// which are the same "index:label:value" text as the dump. It must keep a
	// colon-bearing label's value and skip the entries it cannot read.
	QDomDocument document;
	auto saved = document.createElement("vstplugin");
	saved.setAttribute("numparams", 4);
	saved.setAttribute("param0", "0:Gain: 3 dB:0.25");
	saved.setAttribute("param1", "1:Freq:0.5");
	saved.setAttribute("param2", "2:Mix:abc");
	// param3 is absent from the project

	const auto values = savedParameterValues(saved);
	QVERIFY(values.size() == 2);
	QCOMPARE(values[0].index, 0);
	QVERIFY(qAbs(values[0].value - 0.25f) < 1e-4f);
	QCOMPARE(values[1].index, 1);
	QVERIFY(qAbs(values[1].value - 0.5f) < 1e-4f);
}

} // namespace lmms

QTEST_GUILESS_MAIN(lmms::VstEffectControlsTest)

#include "VstEffectControlsTest.moc"
