/*
 * TakeLanesPanel.cpp - M3 item 6: take lanes with audition and consolidate
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

#include "TakeLanesPanel.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include "ControlRegistry.h"
#include "Song.h"

namespace lmms::gui
{

namespace
{

constexpr int RefreshMs = 300;

} // namespace


TakeLanesPanel::TakeLanesPanel(const QString& trackId, QWidget* parent) :
	QWidget(parent),
	m_trackId(trackId)
{
	setWindowTitle(tr("Take lanes - %1").arg(trackId));
	setObjectName(QStringLiteral("TakeLanesPanel"));
	auto* outer = new QVBoxLayout(this);
	m_lanes = new QVBoxLayout;
	outer->addLayout(m_lanes);
	m_addLane = makeButton(QStringLiteral("comp.lane_add"), tr("Add lane"));
	connect(m_addLane, &QToolButton::clicked, this, [this] { run(QStringLiteral("comp.lane_add"), {{QStringLiteral("track"), m_trackId}}); });
	outer->addWidget(m_addLane);

	m_composite = new QLabel(this);
	m_composite->setAccessibleName(tr("Composite"));
	m_composite->setWordWrap(true);
	outer->addWidget(m_composite);

	auto* comp = new QHBoxLayout;
	m_begin = new QSpinBox(this);
	m_end = new QSpinBox(this);
	for (QSpinBox* box : {m_begin, m_end}) { box->setRange(0, MaxSongLength); box->setSuffix(tr(" ticks")); }
	m_begin->setAccessibleName(tr("Comp from tick"));
	m_end->setAccessibleName(tr("Comp to tick"));
	m_lane = new QComboBox(this);
	m_lane->setAccessibleName(tr("Lane to comp in"));
	m_comp = makeButton(QStringLiteral("comp.select"), tr("Comp"));
	connect(m_comp, &QToolButton::clicked, this, [this] {
		run(QStringLiteral("comp.select"), {{QStringLiteral("track"), m_trackId},
			{QStringLiteral("begin"), m_begin->value()}, {QStringLiteral("end"), m_end->value()},
			{QStringLiteral("lane"), m_lane->currentData().toInt()}});
	});
	comp->addWidget(m_begin);
	comp->addWidget(m_end);
	comp->addWidget(m_lane);
	comp->addWidget(m_comp);
	outer->addLayout(comp);

	m_consolidate = makeButton(QStringLiteral("clip.consolidate"), tr("Consolidate the comp"));
	connect(m_consolidate, &QToolButton::clicked, this, [this] {
		QJsonObject args{{QStringLiteral("track"), m_trackId}};
		if (m_spanEnd > m_spanBegin)
		{
			args.insert(QStringLiteral("start"), m_spanBegin);
			args.insert(QStringLiteral("end"), m_spanEnd);
		}
		run(QStringLiteral("clip.consolidate"), args);
	});
	outer->addWidget(m_consolidate);
	m_status = new QLabel(this);
	m_status->setAccessibleName(tr("Take lanes status"));
	outer->addWidget(m_status);
	outer->addStretch();

	connect(&m_timer, &QTimer::timeout, this, &TakeLanesPanel::refresh);
	m_timer.start(RefreshMs);
	refresh();
}


QToolButton* TakeLanesPanel::makeButton(const QString& command, const QString& text)
{
	auto* button = new QToolButton(this);
	button->setText(text);
	button->setAccessibleName(text);
	button->setProperty("controlCommand", command);
	button->setFocusPolicy(Qt::StrongFocus);
	return button;
}


void TakeLanesPanel::run(const QString& command, const QJsonObject& args)
{
	const ControlResult result = ControlRegistry::instance()->invoke(command, args);
	m_status->setText(result.ok ? tr("%1: done").arg(command) : result.errorMessage);
	refresh();
}


void TakeLanesPanel::audition(int lane)
{
	const int next = m_auditioning == lane ? -1 : lane;
	const ControlResult result = ControlRegistry::instance()->invoke(QStringLiteral("comp.audition"),
		{{QStringLiteral("track"), m_trackId}, {QStringLiteral("lane"), next}});
	if (result.ok) { m_auditioning = next; }
	m_status->setText(result.ok ? (next < 0 ? tr("Playing the comp") : tr("Auditioning lane %1").arg(next))
		: result.errorMessage);
	refresh();
}


QToolButton* TakeLanesPanel::auditionButton(int lane) const
{
	for (std::size_t i = 0; i < m_laneIndices.size(); ++i)
	{
		if (m_laneIndices[i] == lane) { return m_auditions[i]; }
	}
	return nullptr;
}


QString TakeLanesPanel::compositeText() const
{
	return m_composite->text();
}


QString TakeLanesPanel::statusText() const
{
	return m_status->text();
}


void TakeLanesPanel::refresh()
{
	const ControlResult state = ControlRegistry::instance()->invoke(QStringLiteral("comp.get_state"),
		{{QStringLiteral("track"), m_trackId}});
	if (!state.ok)
	{
		m_status->setText(state.errorMessage);
		return;
	}
	const QJsonArray lanes = state.result.value(QStringLiteral("lanes")).toArray();
	showLanes(lanes);
	showComposite(state.result.value(QStringLiteral("composite")).toObject(), !lanes.isEmpty());
}

//! The lane buttons (rebuilt when the lane set changes) and their labels.
void TakeLanesPanel::showLanes(const QJsonArray& lanes)
{
	std::vector<int> indices;
	for (const QJsonValue& value : lanes) { indices.push_back(value.toObject().value(QStringLiteral("lane")).toInt()); }
	if (indices != m_laneIndices)
	{
		for (QToolButton* button : m_auditions) { button->deleteLater(); }
		m_auditions.clear();
		m_lane->clear();
		m_laneIndices = indices;
		for (int lane : indices)
		{
			QToolButton* button = makeButton(QStringLiteral("comp.audition"), QString());
			button->setCheckable(true);
			connect(button, &QToolButton::clicked, this, [this, lane] { audition(lane); });
			m_lanes->addWidget(button);
			m_auditions.push_back(button);
			m_lane->addItem(tr("Lane %1").arg(lane), lane);
		}
	}
	for (std::size_t i = 0; i < m_auditions.size(); ++i)
	{
		const QJsonObject lane = lanes.at(static_cast<int>(i)).toObject();
		const QString name = lane.value(QStringLiteral("name")).toString();
		const int takes = lane.value(QStringLiteral("takes")).toArray().size();
		const QString label = tr("Lane %1%2 - %n take(s)", nullptr, takes).arg(m_laneIndices[i])
			.arg(name.isEmpty() ? QString() : QStringLiteral(" (") + name + QStringLiteral(")"));
		m_auditions[i]->setText(tr("Audition: %1").arg(label));
		m_auditions[i]->setAccessibleName(tr("Audition %1").arg(label));
		m_auditions[i]->setChecked(m_laneIndices[i] == m_auditioning);
	}
}


void TakeLanesPanel::showComposite(const QJsonObject& composite, bool hasLanes)
{
	m_spanBegin = composite.value(QStringLiteral("begin")).toInt();
	m_spanEnd = composite.value(QStringLiteral("end")).toInt();
	QStringList segments;
	for (const QJsonValue& value : composite.value(QStringLiteral("segments")).toArray())
	{
		const QJsonObject segment = value.toObject();
		segments << tr("%1-%2: lane %3").arg(segment.value(QStringLiteral("begin")).toInt())
			.arg(segment.value(QStringLiteral("end")).toInt()).arg(segment.value(QStringLiteral("lane")).toInt());
	}
	m_composite->setText(segments.isEmpty() ? tr("No comp yet: pick a range and a lane, then Comp.")
		: tr("Comp: %1").arg(segments.join(QStringLiteral(", "))));
	m_consolidate->setEnabled(!segments.isEmpty() || hasLanes);
}

} // namespace lmms::gui
