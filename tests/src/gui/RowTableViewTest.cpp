/*
 * RowTableViewTest.cpp - clearing the model from a row table view
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

#include <QtTest>

#include <QStandardItemModel>

#include "RowTableView.h"

using lmms::gui::RowTableView;

class RowTableViewTest : public QObject
{
	Q_OBJECT

private slots:
	void clearingModelIsSafe()
	{
		QStandardItemModel model(1, 1);
		RowTableView view;
		view.setModel(&model);
		QVERIFY(view.model() == &model);

		view.setModel(nullptr);
		QVERIFY(view.model() == nullptr);
	}

	void insertedRowsUseTheRowDelegate()
	{
		QStandardItemModel model(1, 1);
		RowTableView view;
		view.setModel(&model);

		const QAbstractItemDelegate* existingRowDelegate = view.itemDelegateForIndex(model.index(0, 0));
		QVERIFY(existingRowDelegate);
		QVERIFY(model.insertRow(1));
		QCOMPARE(view.itemDelegateForIndex(model.index(1, 0)), existingRowDelegate);
	}

	void tabAndBacktabWithoutModelAreSafe()
	{
		RowTableView view;
		QVERIFY(view.model() == nullptr);

		QTest::keyClick(&view, Qt::Key_Tab);
		QTest::keyClick(&view, Qt::Key_Backtab);
		QVERIFY(view.model() == nullptr);
	}

	void tabAndBacktabAfterClearingModelAreSafe()
	{
		QStandardItemModel model(1, 2);
		RowTableView view;
		view.setModel(&model);
		view.setModel(nullptr);

		QTest::keyClick(&view, Qt::Key_Tab);
		QTest::keyClick(&view, Qt::Key_Backtab);
		QVERIFY(view.model() == nullptr);
	}
};

QTEST_MAIN(RowTableViewTest)
#include "RowTableViewTest.moc"
