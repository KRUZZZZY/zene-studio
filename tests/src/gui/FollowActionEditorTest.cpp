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

#include <QAbstractItemModel>
#include <QAbstractSpinBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFocusEvent>
#include <QJsonArray>
#include <QJsonObject>
#include <QLineEdit>
#include <QSpinBox>
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

//! The row's Jump box (column 4, after Action, Chance, After the clip and Bars): the scene a Jump goes to.
QSpinBox* jumpBoxOf(FollowActionEditor* editor, int row)
{
	return qobject_cast<QSpinBox*>(editor->findChild<QTableWidget*>()->cellWidget(row, 4));
}

//! A one-row chain of a Jump to @a scene, as a file stores it (a whole number, which may lie outside the grid).
QJsonArray jumpChain(double scene)
{
	return QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("jump")},
		{QStringLiteral("chance"), 1.0},
		{QStringLiteral("linked"), true},
		{QStringLiteral("time_bars"), 2.0},
		{QStringLiteral("jump_to"), scene}}};
}

} // namespace

class FollowActionEditorTest : public QObject
{
	Q_OBJECT

private slots:
	void aRowRemovedWhileItsControlsReceiveFocusLeavesTheEditorIntact()
	{
		// Qt releases a removed row's cell widgets while the row is still in the model. A focus-in delivered to one of
		// that row's controls then reaches rowOf with a null cell. The focus-in is sent from the model's
		// rowsAboutToBeRemoved hook (connected after the view's own, so the cells are already gone) rather than by moving
		// real focus, which would also re-enter the editor's focus handler and is a separate defect.
		const QJsonArray chain{QJsonObject{{QStringLiteral("type"), QStringLiteral("next")}},
			QJsonObject{{QStringLiteral("type"), QStringLiteral("stop")}},
			QJsonObject{{QStringLiteral("type"), QStringLiteral("none")}}};
		FollowActionEditor editor(QStringLiteral("session.set_slot"), QJsonObject{}, chain, QStringLiteral("1"));
		auto* table = editor.findChild<QTableWidget*>();
		QVERIFY(table != nullptr);
		QWidget* control = table->cellWidget(2, 1);
		QVERIFY(control != nullptr);
		connect(table->model(), &QAbstractItemModel::rowsAboutToBeRemoved, table,
			[control](const QModelIndex&, int first, int) {
				if (first != 2) { return; }
				QFocusEvent focusIn(QEvent::FocusIn);
				QCoreApplication::sendEvent(control, &focusIn);
			});
		table->removeRow(2);
		QCOMPARE(table->rowCount(), 2);
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

	void aMaximumJumpSceneIsWrittenBackAsLoaded()
	{
		// The largest int is a valid stored scene. Loading it used to add 1 to a signed int (undefined behaviour), then the
		// box clamped to its 512 and OK wrote 511 over the stored value.
		FollowActionEditor editor(QStringLiteral("session.set_slot"), QJsonObject{}, jumpChain(2147483647.0), QStringLiteral("1"));
		editor.show();
		QCOMPARE(editor.chain().at(0).toObject().value(QStringLiteral("jump_to")).toDouble(), 2147483647.0);
		QCOMPARE(jumpBoxOf(&editor, 0)->value(), 512);  // the nearest scene the box can show
	}

	void aMalformedNegativeJumpSceneKeepsTheEditorApplicable()
	{
		// SessionClip restores a malformed saved jump target as -1; set_slot rejects it, so preserve the old zero fallback.
		FollowActionEditor editor(QStringLiteral("session.set_slot"), QJsonObject{}, jumpChain(-1.0), QStringLiteral("1"));
		QCOMPARE(editor.chain().at(0).toObject().value(QStringLiteral("jump_to")).toDouble(), 0.0);
	}

	void aJumpSceneTheBoxCannotShowIsKeptUntilTyped()
	{
		// Scene 600 is past the grid's 512 scenes, so the box shows 512. Left alone, the row keeps 600. Typing 512 is an edit
		// and writes scene 511 (the number the box shows).
		FollowActionEditor editor(QStringLiteral("session.set_slot"), QJsonObject{}, jumpChain(600.0), QStringLiteral("1"));
		editor.show();
		QSpinBox* jump = jumpBoxOf(&editor, 0);
		QVERIFY(jump != nullptr);
		QCOMPARE(editor.chain().at(0).toObject().value(QStringLiteral("jump_to")).toDouble(), 600.0);

		jump->setFocus();
		jump->clear();
		QTest::keyClicks(jump, "512");
		QTest::keyClick(jump, Qt::Key_Return);
		QCOMPARE(editor.chain().at(0).toObject().value(QStringLiteral("jump_to")).toDouble(), 511.0);
	}

	void aStepOfAJumpBoxIsAnEdit()
	{
		FollowActionEditor editor(QStringLiteral("session.set_slot"), QJsonObject{}, jumpChain(600.0), QStringLiteral("1"));
		editor.show();
		QSpinBox* jump = jumpBoxOf(&editor, 0);
		QVERIFY(jump != nullptr);
		jump->setFocus();
		QTest::keyClick(jump, Qt::Key_Down);  // from the 512 shown to 511: written, not the loaded 600
		QCOMPARE(editor.chain().at(0).toObject().value(QStringLiteral("jump_to")).toDouble(), 510.0);
	}

	void anInRangeJumpSceneIsWrittenAsLoaded()
	{
		// The grid's first and last scenes (0 and 511) are written back unchanged, as they always were.
		QJsonArray chain = jumpChain(0.0);
		chain.append(jumpChain(511.0).at(0));
		FollowActionEditor editor(QStringLiteral("session.set_slot"), QJsonObject{}, chain, QStringLiteral("1"));
		editor.show();
		QCOMPARE(editor.chain().at(0).toObject().value(QStringLiteral("jump_to")).toDouble(), 0.0);
		QCOMPARE(editor.chain().at(1).toObject().value(QStringLiteral("jump_to")).toDouble(), 511.0);
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
