/*
 * LadspaDescriptionTest.cpp - the LADSPA browser's selection handling
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

#include <QListWidget>
#include <QScrollArea>

#include "Engine.h"
#include "Ladspa2LMMS.h"
#include "LadspaDescription.h"

using namespace lmms;
using namespace lmms::gui;

class LadspaDescriptionTest : public QObject
{
	Q_OBJECT
private slots:
	void initTestCase()
	{
		qputenv("LADSPA_PATH", QByteArray{LADSPA_DESCRIPTION_FIXTURE_DIR});
		Engine::init(true);
		m_key = {QString::fromLatin1(LADSPA_DESCRIPTION_FIXTURE_FILE),
			QStringLiteral("sample_rate_display_defaults")};
		QVERIFY(Engine::getLADSPAManager()->getDescriptor(m_key) != nullptr);
	}

	void cleanupTestCase()
	{
		Engine::destroy();
	}

	// The fixture is the browser's only "Other" plug-in, so the list holds exactly one row and
	// selects it when the description is built.
	void selectedRowIsDescribedAndDoubleClickEmitsIt()
	{
		LadspaDescription description{nullptr, LadspaPluginType::Other};
		auto* list = description.findChild<QListWidget*>();
		QVERIFY(list != nullptr);
		QCOMPARE(list->count(), 1);
		QCOMPARE(list->currentRow(), 0);

		ladspa_key_t emitted;
		bool fired = false;
		connect(&description, &LadspaDescription::doubleClicked, this,
			[&](const ladspa_key_t& key) { emitted = key; fired = true; });
		emit list->itemDoubleClicked(list->item(0));

		QVERIFY(fired);
		QVERIFY(emitted == m_key);
	}

	// Clearing the list makes its current row -1. That is not a plug-in: the description
	// keeps showing the last selected plug-in and double-click still names it.
	void clearingTheRowKeepsTheLastSelection()
	{
		LadspaDescription description{nullptr, LadspaPluginType::Other};
		auto* list = description.findChild<QListWidget*>();
		auto* scrollArea = description.findChild<QScrollArea*>();
		QVERIFY(list != nullptr);
		QVERIFY(scrollArea != nullptr);
		QWidget* shown = scrollArea->widget();
		QVERIFY(shown != nullptr);

		list->clear();
		QCOMPARE(list->currentRow(), -1);
		QCOMPARE(scrollArea->widget(), shown);

		ladspa_key_t emitted;
		bool fired = false;
		connect(&description, &LadspaDescription::doubleClicked, this,
			[&](const ladspa_key_t& key) { emitted = key; fired = true; });
		emit list->itemDoubleClicked(nullptr);

		QVERIFY(fired);
		QVERIFY(emitted == m_key);
	}

	// Rows outside the key list, negative or past the end, are ignored the same way.
	void rowsOutsideTheKeyListAreIgnored()
	{
		LadspaDescription description{nullptr, LadspaPluginType::Other};
		auto* scrollArea = description.findChild<QScrollArea*>();
		QVERIFY(scrollArea != nullptr);
		QWidget* shown = scrollArea->widget();

		QVERIFY(QMetaObject::invokeMethod(&description, "rowChanged", Q_ARG(int, -1)));
		QCOMPARE(scrollArea->widget(), shown);

		QVERIFY(QMetaObject::invokeMethod(&description, "rowChanged", Q_ARG(int, 1)));
		QCOMPARE(scrollArea->widget(), shown);
	}

private:
	ladspa_key_t m_key;
};

QTEST_MAIN(LadspaDescriptionTest)
#include "LadspaDescriptionTest.moc"
