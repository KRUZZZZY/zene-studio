/*
 * SessionTransportBar.cpp - the clip launcher's Follow Actions / arrangement-record row
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

#include "SessionTransportBar.h"

#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QSignalBlocker>
#include <QToolButton>

#include "ControlRegistry.h"

namespace lmms::gui
{

namespace
{

QJsonObject run(const char* command, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(QString::fromLatin1(command), args).result;
}

QToolButton* button(const QString& text, const char* command, bool checkable, QWidget* parent)
{
	auto* b = new QToolButton(parent);
	b->setText(text);
	b->setAccessibleName(text);
	b->setCheckable(checkable);
	b->setProperty("controlCommand", QString::fromLatin1(command));
	return b;
}

} // namespace

SessionTransportBar::SessionTransportBar(QWidget* parent) :
	QWidget(parent)
{
	setObjectName(QStringLiteral("SessionTransportBar"));
	m_follow = button(tr("Follow Actions"), "session.set_follow_actions", true, this);
	m_record = button(tr("Record to arrangement"), "session.arrangement_record_arm", true, this);
	m_land = button(tr("Land the recording"), "session.arrangement_record_land", false, this);
	m_back = button(tr("Back to arrangement"), "session.back_to_arrangement", false, this);
	m_recorded = new QLabel(this);
	m_recorded->setAccessibleName(tr("Arrangement recording status"));
	connect(m_follow, &QToolButton::toggled, this, [this](bool on) {
		run("session.set_follow_actions", {{QStringLiteral("enabled"), on}});
		refresh();
	});
	connect(m_record, &QToolButton::toggled, this, [this](bool on) {
		run("session.arrangement_record_arm", {{QStringLiteral("armed"), on}});
		refresh();
	});
	connect(m_land, &QToolButton::clicked, this, [this] { run("session.arrangement_record_land"); refresh(); });
	connect(m_back, &QToolButton::clicked, this, [this] { run("session.back_to_arrangement"); refresh(); });
	auto* row = new QHBoxLayout(this);
	row->setContentsMargins(0, 0, 0, 0);
	for (QWidget* w : {static_cast<QWidget*>(m_follow), static_cast<QWidget*>(m_record),
			static_cast<QWidget*>(m_land), static_cast<QWidget*>(m_back)}) { row->addWidget(w); }
	row->addWidget(m_recorded, 1);
	connect(&m_timer, &QTimer::timeout, this, &SessionTransportBar::refresh);
	m_timer.start(1000);
	refresh();
}

void SessionTransportBar::refresh()
{
	const QJsonObject follow = run("session.follow_get_state");
	const QJsonObject record = run("session.arrangement_record_status");
	const QSignalBlocker a(m_follow), b(m_record);
	m_follow->setChecked(follow.value(QStringLiteral("follow_actions_enabled")).toBool());
	m_record->setChecked(record.value(QStringLiteral("armed")).toBool());
	const int recorded = record.value(QStringLiteral("recorded")).toInt();
	m_land->setEnabled(recorded > 0);
	m_recorded->setText(recorded > 0 ? tr("%n event(s) recorded", nullptr, recorded) : QString());
}

} // namespace lmms::gui
