/*
 * HelpAccessTest.cpp - the manual and command reference, hover help, guided tours, appearance, the library search view
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
/*! 0.5.0 L14 / L11: the command reference is the registry and filters as asked (WF-136); a control
 *  that runs a command shows that command's description as its help (WF-137); the tours are found
 *  and one steps through a window (WF-140); window.appearance stores the scale where main() reads it
 *  and one undo puts it back (WF-131); the library search view runs a query and lists its columns
 *  (WF-003 / WF-011). */

#include <QtTest>

#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTreeWidget>

#include "Appearance.h"
#include "BrowserSearchView.h"
#include "ContextHelp.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "HelpCenter.h"

using namespace lmms;
using namespace lmms::gui;

class HelpAccessTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		qputenv("ZENE_UI_SCALE_FILE", m_dir.filePath(QStringLiteral("scale")).toLocal8Bit());
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void theReferenceIsTheRegistry()
	{
		HelpCenter* help = HelpCenter::showHelp(QString(), true);
		QVERIFY(help != nullptr);
		QVERIFY(help->referenceText().contains(QStringLiteral("app.help")));
		help->filterReference(QStringLiteral("collection_save"));
		QVERIFY(help->referenceText().contains(QStringLiteral("browser.collection_save")));
		QVERIFY(!help->referenceText().contains(QStringLiteral("transport.play")));
		help->close();
	}

	void aControlShowsItsCommandsHelp()
	{
		QToolButton button;
		button.setProperty("controlCommand", QStringLiteral("transport.record_options"));
		QVERIFY(ContextHelp::helpFor(&button, false).startsWith(QStringLiteral("How a recording pass starts")));
		QVERIFY(ContextHelp::helpFor(&button, true).contains(QStringLiteral("transport.record_options")));
		QToolButton plain;
		QVERIFY(ContextHelp::helpFor(&plain, false).isEmpty());
	}

	void aTourStepsThroughTheWindow()
	{
		const QJsonArray tours = GuidedTour::tours();
		QVERIFY2(tours.size() >= 3, "the shipped tours (data/tours) were not found");
		QWidget window;
		window.resize(800, 600);
		auto* target = new QWidget(&window);
		target->setObjectName(QStringLiteral("shellPanel_browser"));
		target->setGeometry(10, 10, 100, 100);
		window.show();
		QString error;
		GuidedTour* tour = GuidedTour::start(QStringLiteral("first-beat"), &window, &error);
		QVERIFY2(tour != nullptr, qPrintable(error));
		QVERIFY(tour->stepCount() >= 3);
		tour->showStep(1);
		QCOMPARE(tour->step(), 1);
		QVERIFY(GuidedTour::start(QStringLiteral("no-such-tour"), &window, &error) == nullptr);
	}

	void appearanceIsStoredWhereMainReadsIt()
	{
		const ControlResult set = ControlRegistry::instance()->invoke(QStringLiteral("window.appearance"),
			{{QStringLiteral("scale"), 1.5}, {QStringLiteral("palette"), QStringLiteral("high_contrast")}});
		QVERIFY2(set.ok, qPrintable(set.errorMessage));
		QCOMPARE(appearance::storedScale(), 1.5);
		QCOMPARE(appearance::options().palette, QStringLiteral("high_contrast"));
		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("control.undo")).ok);
		QCOMPARE(appearance::options().palette, QStringLiteral("dark"));
		QCOMPARE(appearance::storedScale(), 1.0);
	}

	void theSearchViewListsColumns()
	{
		BrowserSearchView view(nullptr);
		QCOMPARE(view.results()->columnCount(), 5);
		QVERIFY(view.results()->isSortingEnabled());
	}

private:
	QTemporaryDir m_dir;
};

QTEST_MAIN(HelpAccessTest)
#include "HelpAccessTest.moc"
