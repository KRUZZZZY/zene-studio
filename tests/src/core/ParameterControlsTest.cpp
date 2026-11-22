/*
 * ParameterControlsTest.cpp - a ParameterControls key must name one parameter in the save file
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

#include <QtTest>

#include <QDomDocument>
#include <QDomElement>

#include "AudioEngine.h"
#include "AutomatableModel.h"
#include "Engine.h"
#include "Model.h"
#include "ParameterControls.h"

using namespace lmms;

namespace
{

// Makes addParameter() callable from the test; the device code is the real subclass.
class StubParameterControls : public ParameterControls
{
public:
	StubParameterControls() : ParameterControls(nullptr, QStringLiteral("StubParameterControls")) {}
	using ParameterControls::addParameter;
	gui::EffectControlDialog* createView() override { return nullptr; }
};

} // namespace

class ParameterControlsTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		// A model registers with the audio engine on construction, so the engine must exist.
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();
	}

	void cleanupTestCase()
	{
		Engine::destroy();
	}

	void distinctKeysBothRegister()
	{
		Model owner(nullptr);
		StubParameterControls controls;
		FloatModel a(0.f, 0.f, 10.f, 0.1f, &owner, QStringLiteral("a"));
		FloatModel b(0.f, 0.f, 10.f, 0.1f, &owner, QStringLiteral("b"));
		controls.addParameter(&a, QStringLiteral("alpha"));
		controls.addParameter(&b, QStringLiteral("beta"));
		QCOMPARE(controls.parameters().size(), std::size_t{2});
		QCOMPARE(controls.parameters()[0].second, QStringLiteral("alpha"));
		QCOMPARE(controls.parameters()[1].second, QStringLiteral("beta"));
	}

	void duplicateKeyIsRejectedAndOtherKeysStillRegister()
	{
		Model owner(nullptr);
		StubParameterControls controls;
		FloatModel first(0.f, 0.f, 10.f, 0.1f, &owner, QStringLiteral("first"));
		FloatModel second(0.f, 0.f, 10.f, 0.1f, &owner, QStringLiteral("second"));
		FloatModel third(0.f, 0.f, 10.f, 0.1f, &owner, QStringLiteral("third"));
		controls.addParameter(&first, QStringLiteral("gain"));
		controls.addParameter(&second, QStringLiteral("gain"));
		controls.addParameter(&third, QStringLiteral("other"));
		QCOMPARE(controls.parameters().size(), std::size_t{2});
		QVERIFY(controls.parameters()[0].first == &first);
		QVERIFY(controls.parameters()[1].first == &third);
		QCOMPARE(controls.parameters()[1].second, QStringLiteral("other"));
	}

	void distinctModelsKeepIndependentValuesThroughSaveAndLoad()
	{
		Model owner(nullptr);
		StubParameterControls controls;
		FloatModel a(0.f, 0.f, 10.f, 0.1f, &owner, QStringLiteral("a"));
		FloatModel b(0.f, 0.f, 10.f, 0.1f, &owner, QStringLiteral("b"));
		controls.addParameter(&a, QStringLiteral("alpha"));
		controls.addParameter(&b, QStringLiteral("beta"));
		a.setValue(3.f);
		b.setValue(7.f);

		QDomDocument doc;
		QDomElement parent = doc.createElement("effect");
		doc.appendChild(parent);
		controls.saveSettings(doc, parent);

		StubParameterControls restored;
		FloatModel ra(0.f, 0.f, 10.f, 0.1f, &owner, QStringLiteral("a"));
		FloatModel rb(0.f, 0.f, 10.f, 0.1f, &owner, QStringLiteral("b"));
		restored.addParameter(&ra, QStringLiteral("alpha"));
		restored.addParameter(&rb, QStringLiteral("beta"));
		restored.loadSettings(parent);
		QCOMPARE(ra.value(), 3.f);
		QCOMPARE(rb.value(), 7.f);
	}

	void duplicateKeyDoesNotLetTheRejectedModelOverwriteTheSavedValue()
	{
		Model owner(nullptr);
		// Two models registered under one key: only the first is in the list, so the save file
		// holds its value under that key, and a load restores it - the rejected model is not written.
		StubParameterControls controls;
		FloatModel first(0.f, 0.f, 10.f, 0.1f, &owner, QStringLiteral("first"));
		FloatModel second(0.f, 0.f, 10.f, 0.1f, &owner, QStringLiteral("second"));
		controls.addParameter(&first, QStringLiteral("gain"));
		controls.addParameter(&second, QStringLiteral("gain"));
		first.setValue(3.f);
		second.setValue(7.f);

		QDomDocument doc;
		QDomElement parent = doc.createElement("effect");
		doc.appendChild(parent);
		controls.saveSettings(doc, parent);

		StubParameterControls restored;
		FloatModel rf(0.f, 0.f, 10.f, 0.1f, &owner, QStringLiteral("first"));
		restored.addParameter(&rf, QStringLiteral("gain"));
		restored.loadSettings(parent);
		QCOMPARE(rf.value(), 3.f);
	}
};

QTEST_MAIN(ParameterControlsTest)
#include "ParameterControlsTest.moc"
