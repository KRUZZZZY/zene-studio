/*
 * MixerWindows.cpp - the large meters and the routing matrix
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
#include "MixerWindows.h"

#include <algorithm>
#include <cmath>

#include <QDialog>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QPainter>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "CommandForm.h"
#include "ControlRegistry.h"
#include "DesignTokens.h"
#include "Engine.h"
#include "Mixer.h"

namespace lmms::gui
{

namespace
{

//! MX-131: the master's RMS as two tall bars (-60..0 dB), the correlation and the loudness under them.
class LargeMeters : public QDialog
{
public:
	explicit LargeMeters(QWidget* parent) :
		QDialog(parent)
	{
		setWindowTitle(QObject::tr("Large Meters"));
		setAttribute(Qt::WA_DeleteOnClose);
		setAccessibleName(QObject::tr("Large Meters"));
		resize(260, 520);
		auto* timer = new QTimer(this);
		connect(timer, &QTimer::timeout, this, qOverload<>(&QWidget::update));
		timer->start(40);
		auto* slow = new QTimer(this);  // the loudness reading changes slowly; asked twice a second
		connect(slow, &QTimer::timeout, this, [this] { readLoudness(); });
		slow->start(500);
		readLoudness();
	}

protected:
	void paintEvent(QPaintEvent*) override
	{
		const tokens::Palette& pal = tokens::palette();
		QPainter p(this);
		p.fillRect(rect(), pal.panel);
		const MixerChannel* master = Engine::mixer()->mixerChannel(0);
		const float levels[2] = {master->m_rmsLeft.get(), master->m_rmsRight.get()};
		const int bottom = height() - 60;
		for (int side = 0; side < 2; ++side)
		{
			const double db = levels[side] > 0.f ? 20.0 * std::log10(levels[side]) : -60.0;
			const double share = std::clamp((db + 60.0) / 60.0, 0.0, 1.0);
			const QRect bar(20 + side * (width() / 2 - 10), 20, width() / 2 - 40, bottom - 20);
			p.fillRect(bar, pal.control);
			const int top = bar.bottom() - static_cast<int>(share * bar.height());
			p.fillRect(QRect(bar.left(), top, bar.width(), bar.bottom() - top), db > -6.0 ? pal.meterHigh : db > -18.0 ? pal.meterMid : pal.meterLow);
		}
		p.setPen(pal.text);
		p.setFont(tokens::font(tokens::TextRole::Title));
		p.drawText(QRect(0, bottom + 4, width(), 26), Qt::AlignCenter, QObject::tr("Correlation %1").arg(master->m_correlation.get(), 0, 'f', 2));
		p.drawText(QRect(0, bottom + 30, width(), 26), Qt::AlignCenter, m_loudness);
	}

private:
	void readLoudness()
	{
		const QJsonValue lufs = ControlRegistry::instance()->invoke(QStringLiteral("meter.get_state")).result.value(QStringLiteral("short_term_lufs"));
		m_loudness = lufs.isDouble() ? QObject::tr("%1 LUFS (short term)").arg(lufs.toDouble(), 0, 'f', 1)
			: QObject::tr("Loudness meter off (Strip > Loudness Meter)");
	}

	QString m_loudness;
};

//! MX-055: channels against channels; a cell shows the send's amount, double-click sends there.
class RoutingMatrix : public QDialog
{
public:
	explicit RoutingMatrix(QWidget* parent) :
		QDialog(parent),
		m_table(new QTableWidget(this))
	{
		setWindowTitle(QObject::tr("Routing Matrix"));
		setAttribute(Qt::WA_DeleteOnClose);
		m_table->setAccessibleName(QObject::tr("Routing matrix: rows send to columns"));
		m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
		m_table->setProperty("controlCommand", QStringLiteral("routing.matrix"));
		auto* layout = new QVBoxLayout(this);
		layout->addWidget(m_table);
		connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int column) { send(row, column); });
		fill();
		resize(640, 420);
	}

private:
	void fill()
	{
		const QJsonArray channels = ControlRegistry::instance()->invoke(QStringLiteral("routing.matrix")).result.value(QStringLiteral("channels")).toArray();
		m_ids.clear();
		QStringList names;
		for (const QJsonValue& value : channels)
		{
			m_ids << value.toObject().value(QStringLiteral("channel")).toString();
			names << value.toObject().value(QStringLiteral("name")).toString();
		}
		m_table->setRowCount(static_cast<int>(channels.size()));
		m_table->setColumnCount(static_cast<int>(channels.size()));
		m_table->setVerticalHeaderLabels(names);
		m_table->setHorizontalHeaderLabels(names);
		for (int row = 0; row < channels.size(); ++row)
		{
			for (const QJsonValue& send : channels.at(row).toObject().value(QStringLiteral("sends")).toArray())
			{
				const QJsonObject s = send.toObject();
				const int column = static_cast<int>(m_ids.indexOf(s.value(QStringLiteral("to")).toString()));
				if (column < 0) { continue; }
				QString text = QString::number(s.value(QStringLiteral("amount")).toDouble(), 'f', 2);
				if (!s.value(QStringLiteral("enabled")).toBool(true)) { text += QObject::tr(" (off)"); }
				m_table->setItem(row, column, new QTableWidgetItem(text));
			}
		}
	}

	void send(int row, int column)
	{
		if (row == column || row >= m_ids.size() || column >= m_ids.size()) { return; }
		runCommand(this, CommandCall{QStringLiteral("mixer.send_to"), {{QStringLiteral("channel"), m_ids.at(row)}, {QStringLiteral("to"), m_ids.at(column)}},
			{{QStringLiteral("amount"), 1.0}}, QObject::tr("Send"), true});
		QTimer::singleShot(200, this, [this] { fill(); });
	}

	QTableWidget* m_table;
	QStringList m_ids;
};

} // namespace

void addMixerWindows(QMenu* menu)
{
	QAction* meters = menu->addAction(QObject::tr("Large Meters..."));
	meters->setProperty("controlCommand", QStringLiteral("meter.get_state"));
	QObject::connect(meters, &QAction::triggered, menu, [menu] { (new LargeMeters(menu->parentWidget()))->show(); });
	QAction* matrix = menu->addAction(QObject::tr("Routing Matrix..."));
	matrix->setProperty("controlCommand", QStringLiteral("routing.matrix"));
	QObject::connect(matrix, &QAction::triggered, menu, [menu] { (new RoutingMatrix(menu->parentWidget()))->show(); });
}

} // namespace lmms::gui
