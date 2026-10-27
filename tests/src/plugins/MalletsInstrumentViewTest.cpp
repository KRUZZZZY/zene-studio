/*
 * MalletsInstrumentViewTest.cpp - the BandedWG "Bowed" setting has a control
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#include <QDomDocument>
#include <QDomElement>
#include <QtTest>
#include <memory>

#include "ConfigManager.h"
#include "ComboBox.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "LedCheckBox.h"
#include "NotePlayHandle.h"
#include "Song.h"
#include "Track.h"

#include "Mallets.h"

#ifndef MALLETS_TEST_STK_DIR
#define MALLETS_TEST_STK_DIR ""
#endif

using namespace lmms;

namespace
{

//! The instrument view's "Bowed" checkbox, or nullptr when the view has none.
gui::LedCheckBox* bowedCheckBox(QWidget* view)
{
	for (gui::LedCheckBox* box : view->findChildren<gui::LedCheckBox*>())
	{
		if (box->text() == QStringLiteral("Bowed")) { return box; }
	}
	return nullptr;
}

//! The "strike" attribute the instrument writes into a project: "1" bowed, "0" struck.
QString savedStrike(MalletsInstrument* instrument)
{
	QDomDocument doc;
	QDomElement element = doc.createElement(QStringLiteral("instrument"));
	instrument->saveSettings(doc, element);
	return element.attribute(QStringLiteral("strike"));
}

//! The "preset" attribute the instrument writes into a project: the index the synth plays.
QString savedPreset(MalletsInstrument* instrument)
{
	QDomDocument doc;
	QDomElement element = doc.createElement(QStringLiteral("instrument"));
	instrument->saveSettings(doc, element);
	return element.attribute(QStringLiteral("preset"));
}

} // namespace

class MalletsInstrumentViewTest : public QObject
{
	Q_OBJECT
private slots:
	void initTestCase()
	{
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();
		NotePlayHandleManager::init();
		// The instrument view opens a modal "missing files" dialog when the STK rawwaves are not found,
		// which would block an offscreen test. Point the instrument at the directory the build found.
		ConfigManager::inst()->setSTKDir(QString::fromUtf8(MALLETS_TEST_STK_DIR));
	}

	void cleanupTestCase()
	{
		Engine::getSong()->clearProject();
		Engine::destroy();
	}

	void theBowedCheckBoxIsBoundToTheStrikeSetting()
	{
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		QVERIFY(track != nullptr);
		const std::unique_ptr<MalletsInstrument> instrument{new MalletsInstrument(track)};

		QWidget parent;
		const std::unique_ptr<gui::PluginView> pluginView{instrument->createView(&parent)};
		auto* view = dynamic_cast<gui::MalletsInstrumentView*>(pluginView.get());
		QVERIFY(view != nullptr);
		auto* presets = view->findChild<gui::ComboBox*>();
		QVERIFY(presets != nullptr);
		// The first BandedWG preset is index 10; select it through the instrument's preset control.
		for (int i = 0; i < 10; ++i) { presets->selectNext(); }
		QCOMPARE(presets->model()->value(), 10);
		parent.show();
		view->show();

		gui::LedCheckBox* bowed = bowedCheckBox(view);
		QVERIFY2(bowed != nullptr, "the BandedWG view has no Bowed checkbox, so the setting cannot be changed in the UI");
		QVERIFY2(bowed->isVisible(), "the Bowed checkbox is hidden for the selected BandedWG preset");

		// The setting defaults to bowed, and the checkbox shows it.
		QVERIFY(bowed->model()->value());
		QCOMPARE(savedStrike(instrument.get()), QStringLiteral("1"));

		// A click on the checkbox writes through to the instrument's setting.
		bowed->click();
		QVERIFY(!bowed->model()->value());
		QCOMPARE(savedStrike(instrument.get()), QStringLiteral("0"));

		bowed->click();
		QVERIFY(bowed->model()->value());
		QCOMPARE(savedStrike(instrument.get()), QStringLiteral("1"));

		// The other direction: a struck setting loaded from a project shows on the checkbox.
		QDomDocument doc;
		QDomElement element = doc.createElement(QStringLiteral("instrument"));
		element.setAttribute(QStringLiteral("strike"), QStringLiteral("0"));
		instrument->loadSettings(element);
		QVERIFY2(!bowed->model()->value(), "a loaded Bowed=0 setting does not reach the checkbox");
	}

	void aLoadedPresetOutsideTheScalerTableIsFittedIntoIt()
	{
		// The synth indexes its scaler table with the preset the model holds; the table has
		// the 14 presets 0-13. A project value outside that range must be fitted on load.
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		QVERIFY(track != nullptr);
		const std::unique_ptr<MalletsInstrument> instrument{new MalletsInstrument(track)};

		const struct { const char* loaded; const char* saved; } cases[] = {
			{"-1", "0"}, {"0", "0"}, {"13", "13"}, {"14", "13"}, {"99", "13"},
		};
		for (const auto& c : cases)
		{
			QDomDocument doc;
			QDomElement element = doc.createElement(QStringLiteral("instrument"));
			element.setAttribute(QStringLiteral("preset"), QString::fromUtf8(c.loaded));
			instrument->loadSettings(element);
			QCOMPARE(savedPreset(instrument.get()), QString::fromUtf8(c.saved));
		}
	}
};

QTEST_MAIN(MalletsInstrumentViewTest)

#include "MalletsInstrumentViewTest.moc"
