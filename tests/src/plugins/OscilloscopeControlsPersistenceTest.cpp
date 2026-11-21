/*
 * OscilloscopeControlsPersistenceTest.cpp - cluster new-p23-187
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#include <QtTest>

#include <QDomDocument>

#include "Engine.h"
#include "OscilloscopeControls.h"

using namespace lmms;

class OscilloscopeControlsPersistenceTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
	}

	void cleanupTestCase()
	{
		Engine::destroy();
	}

	void pauseSettingRoundTrips()
	{
		QDomDocument sourceDocument;
		QDomElement source = sourceDocument.createElement("controls");
		sourceDocument.appendChild(source);
		source.setAttribute("pause", "1");
		source.setAttribute("amp", "321.5");
		source.setAttribute("stereo", "1");

		OscilloscopeControls controls(nullptr);
		controls.loadSettings(source);

		QDomDocument savedDocument;
		QDomElement saved = savedDocument.createElement("controls");
		savedDocument.appendChild(saved);
		controls.saveSettings(savedDocument, saved);

		QVERIFY(saved.hasAttribute("pause"));
		QCOMPARE(saved.attribute("pause").toFloat(), 1.0f);
		QCOMPARE(saved.attribute("amp").toFloat(), 321.5f);
		QCOMPARE(saved.attribute("stereo").toFloat(), 1.0f);

		OscilloscopeControls restored(nullptr);
		restored.loadSettings(saved);
		QDomDocument roundTripDocument;
		QDomElement roundTrip = roundTripDocument.createElement("controls");
		roundTripDocument.appendChild(roundTrip);
		restored.saveSettings(roundTripDocument, roundTrip);

		QVERIFY(roundTrip.hasAttribute("pause"));
		QCOMPARE(roundTrip.attribute("pause").toFloat(), 1.0f);
	}

	void legacySettingsDefaultPauseToFalse()
	{
		QDomDocument sourceDocument;
		QDomElement source = sourceDocument.createElement("controls");
		sourceDocument.appendChild(source);
		source.setAttribute("amp", "321.5");
		source.setAttribute("stereo", "1");

		OscilloscopeControls controls(nullptr);
		controls.loadSettings(source);
		QDomDocument savedDocument;
		QDomElement saved = savedDocument.createElement("controls");
		savedDocument.appendChild(saved);
		controls.saveSettings(savedDocument, saved);

		QVERIFY(saved.hasAttribute("pause"));
		QCOMPARE(saved.attribute("pause").toFloat(), 0.0f);
		QCOMPARE(saved.attribute("amp").toFloat(), 321.5f);
		QCOMPARE(saved.attribute("stereo").toFloat(), 1.0f);
	}
};

QTEST_MAIN(OscilloscopeControlsPersistenceTest)

#include "OscilloscopeControlsPersistenceTest.moc"
