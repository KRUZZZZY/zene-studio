/*
 * FollowActionEditorTest.cpp - the follow-action chain's rows, and what OK writes from them
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

/*! The two-decimal number boxes give back the value that was loaded while they are left alone, so a
 *  loaded chance keeps its precision. What the user actually types is what gets written: typing the
 *  number the box already shows is an edit, not a return to the untouched value. */

#include <QtTest>

#include <QAbstractSpinBox>
#include <QDoubleSpinBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>

#include "FollowActionEditor.h"

using namespace lmms::gui;

namespace
{

//! The row's chance box: the chain's 0-to-1 box, as told from a Bars box (0.25 to 256).
QDoubleSpinBox* chanceBoxOf(FollowActionEditor* editor)
{
	for (QDoubleSpinBox* box : editor->findChildren<QDoubleSpinBox*>())
	{
		if (box->minimum() == 0.0 && box->maximum() == 1.0) { return box; }
	}
	return nullptr;
}

} // namespace

class FollowActionEditorTest : public QObject
{
	Q_OBJECT

private slots:
		//! Every row keeps all of its cell widgets: addRow sets them all before the row is used, so rowOf, nameRows
		//! and chain never meet a missing one on the paths covered here. Remove is not driven: it re-enters the
		//! event filter while the row is being taken out, a separate defect this test does not pin.
		void everyRowKeepsAllItsCellWidgets()
		{
			const QJsonArray chain{QJsonObject{{QStringLiteral("type"), QStringLiteral("next")}},
				QJsonObject{{QStringLiteral("type"), QStringLiteral("stop")}}};
			FollowActionEditor editor(QStringLiteral("session.set_slot"), QJsonObject{}, chain, QStringLiteral("1"));
			editor.show();
			auto* table = editor.findChild<QTableWidget*>();
			QVERIFY(table != nullptr);
			const auto verifyComplete = [table] {
				for (int row = 0; row < table->rowCount(); ++row)
				{
					for (int column = 0; column < table->columnCount(); ++column) { QVERIFY(table->cellWidget(row, column) != nullptr); }
				}
			};
			QCOMPARE(table->rowCount(), 2);
			verifyComplete();
			QPushButton* add = nullptr;
			for (QPushButton* button : editor.findChildren<QPushButton*>()) { if (button->text() == QLatin1String("Add")) { add = button; } }
			QVERIFY(add != nullptr);
			QTest::mouseClick(add, Qt::LeftButton);
			QCOMPARE(table->rowCount(), 3);
			verifyComplete();
		}


	void anExplicitChanceEditIsWritten()
	{
		const QJsonArray chain{QJsonObject{{QStringLiteral("type"), QStringLiteral("next")},
			{QStringLiteral("chance"), 0.1234},
			{QStringLiteral("linked"), true},
			{QStringLiteral("time_bars"), 2.0},
			{QStringLiteral("jump_to"), 0}}};
		FollowActionEditor editor(QStringLiteral("session.set_slot"), QJsonObject{}, chain, QStringLiteral("1"));
		editor.show();
		QDoubleSpinBox* chance = chanceBoxOf(&editor);
		QVERIFY(chance != nullptr);
		QCOMPARE(chance->value(), 0.12);  // what the two decimals show

		// Left alone, the row gives back the value that was loaded, not the rounded one.
		QCOMPARE(editor.chain().at(0).toObject().value(QStringLiteral("chance")).toDouble(), 0.1234);

		// Typing the very number the box already shows is still an edit.
		chance->setFocus();
		chance->clear();
		QTest::keyClicks(chance, "0.12");
		QTest::keyClick(chance, Qt::Key_Return);
		QCOMPARE(editor.chain().at(0).toObject().value(QStringLiteral("chance")).toDouble(), 0.12);
	}

	void aPassiveFocusLossKeepsTheLoadedChance()
	{
		const QJsonArray chain{QJsonObject{{QStringLiteral("type"), QStringLiteral("next")},
			{QStringLiteral("chance"), 0.1234},
			{QStringLiteral("linked"), true},
			{QStringLiteral("time_bars"), 2.0},
			{QStringLiteral("jump_to"), 0}}};
		FollowActionEditor editor(QStringLiteral("session.set_slot"), QJsonObject{}, chain, QStringLiteral("1"));
		editor.show();
		QDoubleSpinBox* chance = chanceBoxOf(&editor);
		QVERIFY(chance != nullptr);

		// Tabbing away from the untouched box makes the spin box emit editingFinished without any typing.
		// That passive commit is not an edit: the loaded precision survives it. (Simulated directly: a
		// real focus change in this offscreen run crashes in the D-Bus accessibility bridge.)
		emit chance->editingFinished();
		QCOMPARE(editor.chain().at(0).toObject().value(QStringLiteral("chance")).toDouble(), 0.1234);
	}
};

QTEST_MAIN(FollowActionEditorTest)
#include "FollowActionEditorTest.moc"