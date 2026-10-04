/*
 * MixerRoutingMatrix.cpp - the routing matrix window: every channel against every channel, sparse
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
#include "MixerRoutingMatrix.h"

#include <QDialog>
#include <QHeaderView>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>

#include "CommandForm.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "MainWindow.h"
#include "Mixer.h"
#include "MixerStripInfo.h"

namespace lmms::gui
{

void RoutingMatrixModel::setChannels(const QJsonArray& channels, const QStringList& types)
{
	beginResetModel();
	m_ids.clear();
	m_names.clear();
	m_headers.clear();
	m_sends.clear();
	QHash<QString, int> columns;  // id -> column: a send finds its column without walking every channel
	for (const QJsonValue& value : channels)
	{
		const QJsonObject channel = value.toObject();
		columns.insert(channel.value(QStringLiteral("channel")).toString(), static_cast<int>(m_ids.size()));
		m_ids << channel.value(QStringLiteral("channel")).toString();
		m_names << channel.value(QStringLiteral("name")).toString();
		const QString type = types.value(static_cast<int>(m_names.size()) - 1);
		m_headers << (type.isEmpty() ? m_names.last() : QStringLiteral("%1 (%2)").arg(m_names.last(), type));
	}
	for (int row = 0; row < channels.size(); ++row)
	{
		for (const QJsonValue& value : channels.at(row).toObject().value(QStringLiteral("sends")).toArray())
		{
			const QJsonObject send = value.toObject();
			const int column = columns.value(send.value(QStringLiteral("to")).toString(), -1);
			if (column < 0) { continue; }
			m_sends.insert(key(row, column), {send.value(QStringLiteral("amount")).toDouble(),
				send.value(QStringLiteral("pre_fader")).toBool(), send.value(QStringLiteral("enabled")).toBool(true)});
		}
	}
	endResetModel();
}

int RoutingMatrixModel::rowCount(const QModelIndex& parent) const
{
	return parent.isValid() ? 0 : static_cast<int>(m_ids.size());
}

int RoutingMatrixModel::columnCount(const QModelIndex& parent) const
{
	return parent.isValid() ? 0 : static_cast<int>(m_ids.size());
}

QString RoutingMatrixModel::cellText(const Send& send) const
{
	QString text = QString::number(send.amount, 'f', 2);
	if (send.preFader) { text += tr(" pre"); }
	if (!send.enabled) { text += tr(" (off)"); }
	return text;
}

QString RoutingMatrixModel::spokenCell(int row, int column) const
{
	if (row == column) { return tr("A channel cannot send to itself"); }
	const auto send = m_sends.constFind(key(row, column));
	const QString pair = tr("%1 to %2").arg(m_names.value(row), m_names.value(column));
	return send == m_sends.constEnd() ? tr("%1: no send (Enter makes one)").arg(pair) : tr("%1: %2").arg(pair, cellText(*send));
}

QVariant RoutingMatrixModel::data(const QModelIndex& index, int role) const
{
	if (!index.isValid()) { return {}; }
	const int row = index.row();
	if (role == NameRole) { return m_names.value(row); }
	if (role == Qt::ToolTipRole || role == Qt::AccessibleTextRole) { return spokenCell(row, index.column()); }
	const auto send = m_sends.constFind(key(row, index.column()));
	if (send == m_sends.constEnd()) { return {}; }
	if (role == Qt::DisplayRole) { return cellText(*send); }
	return role == AmountRole ? QVariant(send->amount) : QVariant();
}

QVariant RoutingMatrixModel::headerData(int section, Qt::Orientation orientation, int role) const
{
	if (role != Qt::DisplayRole && role != Qt::ToolTipRole) { return {}; }
	const QString header = m_headers.value(section);
	if (role == Qt::DisplayRole) { return header; }
	return orientation == Qt::Vertical ? tr("%1 sends along this row").arg(header) : tr("Sends into %1 down this column").arg(header);
}

Qt::ItemFlags RoutingMatrixModel::flags(const QModelIndex& index) const
{
	if (!index.isValid() || index.row() == index.column()) { return Qt::NoItemFlags; }
	return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

namespace
{

//! The window: a sender filter over the matrix. Reads routing.matrix, the same data an agent reads.
class RoutingMatrixWindow : public QDialog
{
public:
	explicit RoutingMatrixWindow(QWidget* parent) :
		QDialog(parent),
		m_status(new QLabel(this)),
		m_filter(new QLineEdit(this)),
		m_view(new QTableView(this)),
		m_model(new RoutingMatrixModel(this)),
		m_proxy(new QSortFilterProxyModel(this))
	{
		setWindowTitle(tr("Routing Matrix"));
		setAttribute(Qt::WA_DeleteOnClose);
		m_status->setWordWrap(true);
		m_status->hide();
		m_filter->setPlaceholderText(tr("Filter senders by name"));
		m_filter->setAccessibleName(tr("Filter the sending channels by name"));
		m_proxy->setSourceModel(m_model);
		m_proxy->setFilterRole(RoutingMatrixModel::NameRole);
		m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
		m_view->setModel(m_proxy);
		m_view->setAccessibleName(tr("Routing matrix: rows send to columns"));
		m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
		m_view->setProperty("controlCommand", QStringLiteral("routing.matrix"));
		m_view->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);  // never measures every cell
		auto* layout = new QVBoxLayout(this);
		layout->addWidget(m_status);
		layout->addWidget(m_filter);
		layout->addWidget(m_view, 1);
		connect(m_filter, &QLineEdit::textChanged, m_proxy, &QSortFilterProxyModel::setFilterFixedString);
		connect(m_view, &QTableView::activated, this, [this](const QModelIndex& at) { send(m_proxy->mapToSource(at)); });
		fill();
		resize(640, 420);
	}

private:
	//! A failed read says so and keeps the matrix last read, rather than showing an empty project.
	void fill()
	{
		const ControlResult matrix = ControlRegistry::instance()->invoke(QStringLiteral("routing.matrix"));
		m_status->setVisible(!matrix.ok);
		if (!matrix.ok)
		{
			m_status->setText(tr("The routing could not be read: %1").arg(matrix.errorMessage));
			return;
		}
		const QJsonArray channels = matrix.result.value(QStringLiteral("channels")).toArray();
		const mixerstripinfo::Context context = mixerstripinfo::buildContext();
		QStringList types;
		for (int i = 0; i < channels.size() && i < Engine::mixer()->numChannels(); ++i)
		{
			types << mixerstripinfo::typeName(mixerstripinfo::describe(*Engine::mixer(), i, context).type);
		}
		m_model->setChannels(channels, types);
	}

	void send(const QModelIndex& cell)
	{
		if (!cell.isValid() || cell.row() == cell.column()) { return; }
		const QVariant current = cell.data(RoutingMatrixModel::AmountRole);
		runCommand(this, CommandCall{QStringLiteral("mixer.send_to"), {{QStringLiteral("channel"), m_model->channelId(cell.row())},
			{QStringLiteral("to"), m_model->channelId(cell.column())}}, {{QStringLiteral("amount"), current.isValid() ? current.toDouble() : 1.0}},
			tr("Send"), true});
		QTimer::singleShot(200, this, [this] { fill(); });
	}

	QLabel* m_status;
	QLineEdit* m_filter;
	QTableView* m_view;
	RoutingMatrixModel* m_model;
	QSortFilterProxyModel* m_proxy;
};

QPointer<QDialog> s_matrix;

} // namespace

void showRoutingMatrix()
{
	// It belongs to the main window, not to a menu: a strip's context menu is deleted as soon as it closes.
	if (s_matrix.isNull()) { s_matrix = new RoutingMatrixWindow(getGUI() != nullptr ? getGUI()->mainWindow() : nullptr); }
	s_matrix->show();
	s_matrix->raise();
	s_matrix->activateWindow();
}

} // namespace lmms::gui
