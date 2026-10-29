/*
 * FollowActionEditor.cpp - a cell's or a scene's follow-action chain, edited as rows
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
#include "FollowActionEditor.h"

#include <QAbstractSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include "CommandForm.h"

namespace lmms::gui
{

namespace
{

struct ActionName
{
	const char* id;  //!< the command's vocabulary (session.set_slot / set_scene 'type')
	const char* label;
	const char* tip;
};

//! The actions in the engine's order, each shown by a name and a one-line explanation, written by its id.
const ActionName kActions[] = {
	{"none", QT_TRANSLATE_NOOP("QObject", "No Action"), QT_TRANSLATE_NOOP("QObject", "Nothing happens; the clip keeps playing")},
	{"stop", QT_TRANSLATE_NOOP("QObject", "Stop"), QT_TRANSLATE_NOOP("QObject", "Stop playing")},
	{"play_again", QT_TRANSLATE_NOOP("QObject", "Play Again"), QT_TRANSLATE_NOOP("QObject", "Play the clip again from its start")},
	{"previous", QT_TRANSLATE_NOOP("QObject", "Previous"), QT_TRANSLATE_NOOP("QObject", "Go to the previous scene (from the first, the last)")},
	{"next", QT_TRANSLATE_NOOP("QObject", "Next"), QT_TRANSLATE_NOOP("QObject", "Go to the next scene (from the last, the first)")},
	{"first", QT_TRANSLATE_NOOP("QObject", "First"), QT_TRANSLATE_NOOP("QObject", "Go to the first scene")},
	{"last", QT_TRANSLATE_NOOP("QObject", "Last"), QT_TRANSLATE_NOOP("QObject", "Go to the last scene")},
	{"any", QT_TRANSLATE_NOOP("QObject", "Any"), QT_TRANSLATE_NOOP("QObject", "Go to a random scene, this one included")},
	{"other", QT_TRANSLATE_NOOP("QObject", "Other"), QT_TRANSLATE_NOOP("QObject", "Go to a random scene other than this one")},
	{"jump", QT_TRANSLATE_NOOP("QObject", "Jump"), QT_TRANSLATE_NOOP("QObject", "Go to the scene in Jump to scene")},
};

QComboBox* actionBox(const QString& type, QWidget* parent)
{
	auto* box = new QComboBox(parent);
	for (const ActionName& action : kActions)
	{
		box->addItem(QObject::tr(action.label), QLatin1String(action.id));
		box->setItemData(box->count() - 1, QObject::tr(action.tip), Qt::ToolTipRole);
	}
	box->setCurrentIndex(box->findData(type));  // an unknown type shows blank and is refused on OK, never rewritten
	return box;
}

//! Jump to scene only matters for a jump, Bars only when the action is not timed by the clip.
void wireRow(QComboBox* type, QCheckBox* linked, QWidget* bars, QWidget* jump)
{
	const auto sync = [type, linked, bars, jump] {
		jump->setEnabled(type->currentData().toString() == QLatin1String("jump"));
		bars->setEnabled(!linked->isChecked());
	};
	QObject::connect(type, QOverload<int>::of(&QComboBox::currentIndexChanged), type, sync);
	QObject::connect(linked, &QCheckBox::toggled, linked, sync);
	sync();
}

enum Column { Type, Chance, Linked, Bars, Jump, ColumnCount };

//! A number box showing @a value. The box rounds what it shows to its two decimals and its range, so
//! the value loaded is kept too and given back exactly while the box is left alone (valueOf).
QDoubleSpinBox* numberBox(double low, double high, double value, QWidget* parent)
{
	auto* box = new QDoubleSpinBox(parent);
	box->setRange(low, high);
	box->setValue(value);
	box->setProperty("loaded", value);
	box->setProperty("shown", box->value());
	// The user typed into the box. What it holds is then what they asked for, even when that is the very
	// number it was loaded with - a round trip through the display is an edit, not a return to the value
	// that was there before. Only typing counts: editingFinished also fires on a passive focus loss
	// (Tab, or OK taken with the box focused), which must leave an untouched box at its loaded value.
	if (auto* text = box->findChild<QLineEdit*>()) // the spin box's own editor (lineEdit() is protected)
	{
		QObject::connect(text, &QLineEdit::textEdited, box, [box] { box->setProperty("edited", true); });
	}
	return box;
}

//! What @a widget (a numberBox) holds: the value loaded while it still shows that, else what was typed.
double valueOf(QWidget* widget)
{
	const auto* box = qobject_cast<QDoubleSpinBox*>(widget);
	if (box->property("edited").toBool()) { return box->value(); }
	return box->value() == box->property("shown").toDouble() ? box->property("loaded").toDouble() : box->value();
}

} // namespace

FollowActionEditor::FollowActionEditor(const QString& command, const QJsonObject& where, const QJsonArray& chain,
	const QString& title, QWidget* parent) :
	QDialog(parent),
	m_command(command),
	m_where(where),
	m_rows(new QTableWidget(0, ColumnCount, this))
{
	setWindowTitle(QObject::tr("Follow Actions - %1").arg(title));
	setAttribute(Qt::WA_DeleteOnClose);
	setProperty("controlCommand", command);
	m_rows->setHorizontalHeaderLabels({QObject::tr("Action"), QObject::tr("Chance"), QObject::tr("After the clip"),
		QObject::tr("Bars"), QObject::tr("Jump to scene")});
	const QStringList tips{QObject::tr("What happens when the follow action fires"),
		QObject::tr("This row's weight, 0 to 1: one row is picked at random in proportion to the weights"),
		QObject::tr("Fire after the clip's own length (on) or after Bars (off)"),
		QObject::tr("With After the clip off: the bars before the action fires"), QObject::tr("The scene a Jump goes to")};
	for (int column = 0; column < ColumnCount; ++column) { m_rows->horizontalHeaderItem(column)->setToolTip(tips.at(column)); }
	m_rows->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
	m_rows->setAccessibleName(QObject::tr("Follow action chain"));
	for (const QJsonValue& action : chain) { addRow(action.toObject()); }
	auto* hint = new QLabel(QObject::tr("One row is picked at random, weighted by its Chance. The first row's timing "
		"(After the clip or Bars) applies to the whole chain."), this);
	hint->setWordWrap(true);
	auto* add = new QPushButton(QObject::tr("Add"), this);
	auto* remove = new QPushButton(QObject::tr("Remove"), this);
	remove->setEnabled(false);  // until a row is current
	connect(add, &QPushButton::clicked, this, [this] {
		addRow({{QStringLiteral("type"), QStringLiteral("next")}});
		m_rows->setCurrentCell(m_rows->rowCount() - 1, Type);
		m_rows->cellWidget(m_rows->rowCount() - 1, Type)->setFocus();
	});
	connect(remove, &QPushButton::clicked, this, [this] { m_rows->removeRow(m_rows->currentRow()); nameRows(); });
	connect(m_rows, &QTableWidget::currentCellChanged, remove, [remove](int row) { remove->setEnabled(row >= 0); });
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	connect(buttons, &QDialogButtonBox::accepted, this, [this] { apply(); });
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	auto* rowButtons = new QHBoxLayout;
	rowButtons->addWidget(add);
	rowButtons->addWidget(remove);
	rowButtons->addStretch();
	auto* layout = new QVBoxLayout(this);
	layout->addWidget(m_rows, 1);
	layout->addWidget(hint);
	layout->addLayout(rowButtons);
	layout->addWidget(buttons);
	resize(560, 260);
}

void FollowActionEditor::addRow(const QJsonObject& action)
{
	const int row = m_rows->rowCount();
	m_rows->insertRow(row);
	QComboBox* type = actionBox(action.value(QStringLiteral("type")).toString(QStringLiteral("next")), m_rows);
	QDoubleSpinBox* chance = numberBox(0.0, 1.0, action.value(QStringLiteral("chance")).toDouble(1.0), m_rows);
	chance->setSingleStep(0.1);
	auto* linked = new QCheckBox(m_rows);
	linked->setChecked(action.value(QStringLiteral("linked")).toBool(true));
	QDoubleSpinBox* bars = numberBox(0.25, 256.0, action.value(QStringLiteral("time_bars")).toDouble(1.0), m_rows);
	bars->setSingleStep(0.25);
	auto* jump = new QSpinBox(m_rows);
	jump->setRange(1, 512);
	jump->setValue(action.value(QStringLiteral("jump_to")).toInt(0) + 1);
	m_rows->setCellWidget(row, Type, type);
	m_rows->setCellWidget(row, Chance, chance);
	m_rows->setCellWidget(row, Linked, linked);
	m_rows->setCellWidget(row, Bars, bars);
	m_rows->setCellWidget(row, Jump, jump);
	wireRow(type, linked, bars, jump);
	for (int column = 0; column < ColumnCount; ++column) { m_rows->cellWidget(row, column)->installEventFilter(this); }
	nameRows();
}

bool FollowActionEditor::eventFilter(QObject* watched, QEvent* event)
{
	// A click or Tab into a row's own controls makes that row current, so Remove takes the row being edited.
	if (event->type() == QEvent::FocusIn)
	{
		if (const int row = rowOf(qobject_cast<QWidget*>(watched)); row >= 0) { m_rows->setCurrentCell(row, Type); }
	}
	return QDialog::eventFilter(watched, event);
}

void FollowActionEditor::nameRows()
{
	// Each control says which column and row it is, for a screen reader (the cells have no text of their own).
	for (int row = 0; row < m_rows->rowCount(); ++row)
	{
		for (int column = 0; column < ColumnCount; ++column)
		{
			m_rows->cellWidget(row, column)->setAccessibleName(QObject::tr("%1, row %2")
				.arg(m_rows->horizontalHeaderItem(column)->text()).arg(row + 1));
		}
	}
}

int FollowActionEditor::rowOf(const QWidget* widget) const
{
	for (int row = 0; widget != nullptr && row < m_rows->rowCount(); ++row)
	{
		for (int column = 0; column < ColumnCount; ++column)
		{
			const QWidget* cell = m_rows->cellWidget(row, column);
			// A row being removed has released its cells while it is still in the model, so a cell can be null here.
			if (cell != nullptr && (cell == widget || cell->isAncestorOf(widget))) { return row; }
		}
	}
	return -1;
}

QJsonArray FollowActionEditor::chain() const
{
	QJsonArray out;
	for (int row = 0; row < m_rows->rowCount(); ++row)
	{
		out.append(QJsonObject{{QStringLiteral("type"), qobject_cast<QComboBox*>(m_rows->cellWidget(row, Type))->currentData().toString()},
			{QStringLiteral("chance"), valueOf(m_rows->cellWidget(row, Chance))},
			{QStringLiteral("linked"), qobject_cast<QCheckBox*>(m_rows->cellWidget(row, Linked))->isChecked()},
			{QStringLiteral("time_bars"), valueOf(m_rows->cellWidget(row, Bars))},
			{QStringLiteral("jump_to"), qobject_cast<QSpinBox*>(m_rows->cellWidget(row, Jump))->value() - 1}});
	}
	return out;
}

void FollowActionEditor::apply()
{
	QJsonObject args = m_where;
	args.insert(QStringLiteral("follow_actions"), chain());
	if (runCommand(this, CommandCall{m_command, args, {}, {}, false}).ok) { accept(); }
}

} // namespace lmms::gui
