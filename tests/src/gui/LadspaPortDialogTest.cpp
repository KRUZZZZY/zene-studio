/*
 * LadspaPortDialogTest.cpp - displayed LADSPA hint ranges and defaults
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

#include <QtTest>

#include <QTableWidget>

#include "AudioEngine.h"
#include "Engine.h"
#include "Ladspa2LMMS.h"
#include "LadspaPortDialog.h"

using namespace lmms;
using namespace lmms::gui;

class LadspaPortDialogTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		qputenv("LADSPA_PATH", QByteArray{LADSPA_PORT_DIALOG_FIXTURE_DIR});
		Engine::init(true);

		m_key = {QString::fromLatin1(LADSPA_PORT_DIALOG_FIXTURE_FILE),
			QStringLiteral("sample_rate_display_defaults")};
		QVERIFY(Engine::getLADSPAManager()->getDescriptor(m_key) != nullptr);
	}

	void cleanupTestCase()
	{
		Engine::destroy();
	}

	void boundDerivedDefaultUsesTheScaledRange()
	{
		auto* manager = Engine::getLADSPAManager();
		const float sampleRate = Engine::audioEngine()->outputSampleRate();
		LadspaPortDialog dialog{m_key};
		const auto* settings = dialog.findChild<QTableWidget*>();
		QVERIFY(settings != nullptr);

		const auto range = settings->item(0, 4)->text().split(QStringLiteral(" < "));
		QCOMPARE(range.size(), 3);
		QCOMPARE(range.at(1), QString::number(manager->getDefaultSetting(m_key, 0) * sampleRate));
	}

	void fixedDefaultKeepsItsDeclaredScale()
	{
		auto* manager = Engine::getLADSPAManager();
		LadspaPortDialog dialog{m_key};
		const auto* settings = dialog.findChild<QTableWidget*>();
		QVERIFY(settings != nullptr);

		const auto range = settings->item(1, 4)->text().split(QStringLiteral(" < "));
		QCOMPARE(range.size(), 3);
		QCOMPARE(manager->getDefaultSetting(m_key, 1), 440.0f);
		QCOMPARE(range.at(1), QString::number(440.0f));
	}

private:
	ladspa_key_t m_key;
};

QTEST_MAIN(LadspaPortDialogTest)
#include "LadspaPortDialogTest.moc"
