/*
 * MixerRoutingMatrix.h - the routing matrix window: every channel against every channel, sparse
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
#ifndef LMMS_GUI_MIXER_ROUTING_MATRIX_H
#define LMMS_GUI_MIXER_ROUTING_MATRIX_H

#include <QAbstractTableModel>
#include <QHash>
#include <QJsonArray>
#include <QStringList>

#include "lmms_export.h"

namespace lmms::gui
{

/*! MX-055, reworked for 0.5.0 theme 16: the routing matrix's data - rows send to columns, each row and
 *  column headed by its channel's name and type ("Verb (Return)"), never its id. It holds only the sends
 *  that exist: N channels cost N names plus their sends, where the old table widget made an item for every
 *  one of the N x N cells (8 MB at 1000 channels, the research measured). The view asks for the cells it
 *  shows and nothing else. */
class LMMS_EXPORT RoutingMatrixModel : public QAbstractTableModel
{
	Q_OBJECT
public:
	static constexpr int NameRole = Qt::UserRole + 1;    //!< the row's channel name (the sender filter reads it)
	static constexpr int AmountRole = Qt::UserRole + 2;  //!< the send's amount; invalid where there is none

	using QAbstractTableModel::QAbstractTableModel;

	//! Fill from routing.matrix's "channels" (id, name, sends), with each channel's @a types ("Return", ...).
	void setChannels(const QJsonArray& channels, const QStringList& types);

	int rowCount(const QModelIndex& parent = QModelIndex()) const override;
	int columnCount(const QModelIndex& parent = QModelIndex()) const override;
	QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
	QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
	Qt::ItemFlags flags(const QModelIndex& index) const override;

	QString channelId(int section) const { return m_ids.value(section); }
	//! The cells it keeps: one per send, whatever the number of channels.
	int storedCells() const { return static_cast<int>(m_sends.size()); }

private:
	struct Send
	{
		double amount = 1.0;
		bool preFader = false;
		bool enabled = true;
	};

	static quint64 key(int row, int column) { return (static_cast<quint64>(row) << 32) | static_cast<quint32>(column); }
	QString cellText(const Send& send) const;
	QString spokenCell(int row, int column) const;

	QStringList m_ids;
	QStringList m_names;
	QStringList m_headers;
	QHash<quint64, Send> m_sends;
};

/*! Open (or raise) the Routing Matrix window: a filter for the senders by name over the matrix; activating a
 *  cell (double-click or Enter) opens mixer.send_to's form for that send, from its current amount. */
LMMS_EXPORT void showRoutingMatrix();

} // namespace lmms::gui

#endif // LMMS_GUI_MIXER_ROUTING_MATRIX_H
