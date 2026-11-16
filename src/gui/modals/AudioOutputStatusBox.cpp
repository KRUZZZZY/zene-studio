/*
 * AudioOutputStatusBox.cpp - Settings > Audio: the running output, the configured one, and a test tone
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

#include "AudioOutputStatusBox.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStringList>
#include <QVBoxLayout>

#include "ControlRegistry.h"

namespace lmms::gui
{

AudioOutputStatusBox::AudioOutputStatusBox(QWidget* parent, const QString& unavailableChoice)
	: QGroupBox(tr("Running output"), parent)
	, m_unavailableChoice(unavailableChoice)
{
	auto* layout = new QVBoxLayout(this);

	m_status = new QLabel(this);
	m_status->setObjectName(QStringLiteral("audioRunningStatus"));
	m_status->setWordWrap(true);
	m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
	layout->addWidget(m_status);

	auto* buttons = new QHBoxLayout();
	m_play = new QPushButton(tr("Play test tone"), this);
	m_play->setObjectName(QStringLiteral("audioTestTonePlay"));
	m_stop = new QPushButton(tr("Stop"), this);
	m_stop->setObjectName(QStringLiteral("audioTestToneStop"));
	buttons->addWidget(m_play);
	buttons->addWidget(m_stop);
	buttons->addStretch();
	layout->addLayout(buttons);

	connect(m_play, &QPushButton::clicked, this, [this] { runCommand(QStringLiteral("audio.test_tone")); });
	connect(m_stop, &QPushButton::clicked, this, [this] { runCommand(QStringLiteral("audio.test_tone_stop")); });

	refresh();
}


void AudioOutputStatusBox::refresh()
{
	QString text = describe(ControlRegistry::instance()->invoke(QStringLiteral("audio.device_list"), {}).result);
	if (!m_unavailableChoice.isEmpty())
	{
		text += QLatin1Char('\n') + tr("Saved choice %1 is not available here, so the running output replaced it.")
			.arg(m_unavailableChoice);
	}
	if (!m_error.isEmpty()) { text += QLatin1Char('\n') + m_error; }
	m_status->setText(text);
}


QString AudioOutputStatusBox::describe(const QJsonObject& deviceList)
{
	const QString current = deviceList.value(QStringLiteral("current")).toString();
	const QString configured = deviceList.value(QStringLiteral("configured")).toString();

	QStringList lines;
	lines << tr("Running: %1").arg(current);
	lines << tr("Configured: %1").arg(configured.isEmpty() ? tr("(none)") : configured);

	if (deviceList.value(QStringLiteral("fallback")).toBool())
	{
		const QString requested = deviceList.value(QStringLiteral("requested")).toString();
		const QString reason = deviceList.value(QStringLiteral("failure_reason")).toString();
		lines << tr("Fallback: %1 could not start - %2")
			.arg(requested.isEmpty() ? configured : requested,
				reason.isEmpty() ? tr("no reason given") : reason);
	}
	else if (!configured.isEmpty() && configured != current)
	{
		lines << tr("Restart to use %1.").arg(configured);
	}
	return lines.join(QLatin1Char('\n'));
}


void AudioOutputStatusBox::runCommand(const QString& command)
{
	const ControlResult result = ControlRegistry::instance()->invoke(command, {});
	m_error = result.ok ? QString() : result.errorMessage;
	m_status->setToolTip(m_error);
	refresh();
}

} // namespace lmms::gui
