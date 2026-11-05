/*
 * MixerMaskingReport.cpp - the Masking Report window over mixer.masking_report
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

#include "MixerMaskingReport.h"

#include <QComboBox>
#include <QDialog>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "ControlRegistry.h"
#include "GuiApplication.h"
#include "MainWindow.h"

namespace lmms::gui
{

namespace
{

QString hz(double value)
{
	return value >= 1000.0 ? QObject::tr("%1 kHz").arg(value / 1000.0, 0, 'f', 2) : QObject::tr("%1 Hz").arg(value, 0, 'f', 0);
}

class MaskingReport : public QDialog
{
public:
	explicit MaskingReport(QWidget* parent) :
		QDialog(parent),
		m_channels(new QTableWidget(0, 2, this)),
		m_clashes(new QTableWidget(0, 4, this)),
		m_summary(new QLabel(QObject::tr("Tick two or more channels and press Report."), this))
	{
		setWindowTitle(QObject::tr("Masking Report"));
		setAttribute(Qt::WA_DeleteOnClose);
		setProperty("controlCommand", QStringLiteral("mixer.masking_report"));
		m_channels->setAccessibleName(QObject::tr("Channels"));
		m_channels->setHorizontalHeaderLabels({QObject::tr("Channel"), QObject::tr("Priority")});
		m_clashes->setAccessibleName(QObject::tr("Clashes"));
		m_clashes->setHorizontalHeaderLabels({QObject::tr("Channels"), QObject::tr("Range"), QObject::tr("Proposed cut"), QObject::tr("Reason")});
		m_clashes->setEditTriggers(QAbstractItemView::NoEditTriggers);
		for (QTableWidget* table : {m_channels, m_clashes}) { table->horizontalHeader()->setStretchLastSection(true); }
		fillChannels();
		auto* report = new QPushButton(QObject::tr("Report"), this);
		report->setAccessibleName(QObject::tr("Report"));
		report->setToolTip(QObject::tr("Compare the ticked channels' audio; changes no EQ"));
		connect(report, &QPushButton::clicked, this, [this] { runReport(); });
		auto* layout = new QVBoxLayout(this);
		layout->addWidget(m_channels, 1);
		layout->addWidget(report);
		layout->addWidget(m_clashes, 2);
		layout->addWidget(m_summary);
		resize(620, 480);
	}

private:
	void fillChannels()
	{
		const QJsonArray channels = ControlRegistry::instance()->invoke(QStringLiteral("routing.matrix")).result.value(QStringLiteral("channels")).toArray();
		for (const QJsonValue& value : channels)
		{
			const QJsonObject channel = value.toObject();
			const int row = m_channels->rowCount();
			m_channels->insertRow(row);
			auto* name = new QTableWidgetItem(channel.value(QStringLiteral("name")).toString());
			name->setData(Qt::UserRole, channel.value(QStringLiteral("channel")).toString());
			name->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
			name->setCheckState(Qt::Unchecked);
			m_channels->setItem(row, 0, name);
			auto* priority = new QComboBox(m_channels);
			priority->addItem(QObject::tr("Front"), QStringLiteral("front"));
			priority->addItem(QObject::tr("Middle"), QStringLiteral("middle"));
			priority->addItem(QObject::tr("Back"), QStringLiteral("back"));
			priority->setCurrentIndex(1);
			priority->setAccessibleName(QObject::tr("Priority of %1").arg(name->text()));
			m_channels->setCellWidget(row, 1, priority);
		}
	}

	QString nameOf(const QString& id) const
	{
		for (int row = 0; row < m_channels->rowCount(); ++row)
		{
			if (m_channels->item(row, 0)->data(Qt::UserRole).toString() == id) { return m_channels->item(row, 0)->text(); }
		}
		return id;
	}

	void runReport()
	{
		QJsonArray ids;
		QJsonObject priorities;
		for (int row = 0; row < m_channels->rowCount(); ++row)
		{
			if (m_channels->item(row, 0)->checkState() != Qt::Checked) { continue; }
			const QString id = m_channels->item(row, 0)->data(Qt::UserRole).toString();
			ids.append(id);
			priorities.insert(id, static_cast<QComboBox*>(m_channels->cellWidget(row, 1))->currentData().toString());
		}
		const ControlResult result = ControlRegistry::instance()->invoke(QStringLiteral("mixer.masking_report"),
			{{QStringLiteral("channels"), ids}, {QStringLiteral("priorities"), priorities}});
		m_clashes->setRowCount(0);
		if (!result.ok)
		{
			m_summary->setText(QObject::tr("Could not report: %1").arg(result.errorMessage));
			return;
		}
		const QJsonArray clashes = result.result.value(QStringLiteral("clashes")).toArray();
		for (const QJsonValue& value : clashes) { addClash(value.toObject()); }
		m_summary->setText(clashes.isEmpty() ? QObject::tr("No clashes between these channels.")
			: QObject::tr("%n clash(es). Nothing was changed: apply a cut with the channel's EQ.", nullptr, static_cast<int>(clashes.size())));
	}

	void addClash(const QJsonObject& clash)
	{
		const int row = m_clashes->rowCount();
		m_clashes->insertRow(row);
		const QString a = nameOf(clash.value(QStringLiteral("a")).toString());
		const QString b = nameOf(clash.value(QStringLiteral("b")).toString());
		m_clashes->setItem(row, 0, new QTableWidgetItem(QObject::tr("%1 / %2").arg(a, b)));
		m_clashes->setItem(row, 1, new QTableWidgetItem(QObject::tr("%1 - %2").arg(hz(clash.value(QStringLiteral("low_hz")).toDouble()),
			hz(clash.value(QStringLiteral("high_hz")).toDouble()))));
		m_clashes->setItem(row, 2, new QTableWidgetItem(QObject::tr("%1 dB on %2").arg(clash.value(QStringLiteral("cut_db")).toDouble(), 0, 'f', 1)
			.arg(nameOf(clash.value(QStringLiteral("target")).toString()))));
		m_clashes->setItem(row, 3, new QTableWidgetItem(clash.value(QStringLiteral("reason")).toString()));
		m_clashes->item(row, 0)->setData(Qt::UserRole, clash);
	}

	QTableWidget* m_channels;
	QTableWidget* m_clashes;
	QLabel* m_summary;
};

QPointer<QDialog> s_open;

} // namespace

void showMaskingReport()
{
	if (s_open.isNull()) { s_open = new MaskingReport(getGUI() != nullptr ? getGUI()->mainWindow() : nullptr); }
	s_open->show();
	s_open->raise();
	s_open->activateWindow();
}

} // namespace lmms::gui
