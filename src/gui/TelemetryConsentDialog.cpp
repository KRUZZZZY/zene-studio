/*
 * TelemetryConsentDialog.cpp - the "what we send" consent and preview screen
 *
 * Copyright (c) 2026 LMMS Developers
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

#include "TelemetryConsentDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QJsonDocument>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace lmms::gui
{

TelemetryConsentDialog::TelemetryConsentDialog(QWidget * parent) :
	QDialog(parent)
{
	setWindowTitle(tr("Telemetry - what we send"));
	resize(560, 460);

	const TelemetryConsent consent = Telemetry::loadConsent();

	auto * layout = new QVBoxLayout(this);

	auto * notice = new QLabel(tr(
		"Anonymous platform statistics are off by default. Nothing is sent "
		"until you turn it on below, and you can turn it off again at any "
		"time. Only the fields shown in the preview are ever sent: coarse "
		"hardware buckets and usage counts, no project or file names, no "
		"paths, no IP address and no identifier that follows you between "
		"sessions."), this);
	notice->setWordWrap(true);
	layout->addWidget(notice);

	m_enabledBox = new QCheckBox(tr("Send anonymous platform statistics"), this);
	m_enabledBox->setChecked(consent.enabled);
	layout->addWidget(m_enabledBox);

	m_hardwareBox = new QCheckBox(tr("Hardware and platform (OS, CPU, GPU, RAM, display, audio backend)"), this);
	m_hardwareBox->setChecked(consent.hardware);
	layout->addWidget(m_hardwareBox);

	m_featureBox = new QCheckBox(tr("Feature usage (coarse plugin-format counts and feature counters)"), this);
	m_featureBox->setChecked(consent.featureUsage);
	layout->addWidget(m_featureBox);

	m_crashBox = new QCheckBox(tr("Crash counts (a count and a coarse stage label)"), this);
	m_crashBox->setChecked(consent.crashCounts);
	layout->addWidget(m_crashBox);

	layout->addWidget(new QLabel(tr("This is the exact payload that would be sent:"), this));

	m_preview = new QPlainTextEdit(this);
	m_preview->setReadOnly(true);
	m_preview->setLineWrapMode(QPlainTextEdit::NoWrap);
	layout->addWidget(m_preview, 1);

	m_status = new QLabel(this);
	m_status->setWordWrap(true);
	layout->addWidget(m_status);

	auto * buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Close, this);
	layout->addWidget(buttons);

	connect(buttons, &QDialogButtonBox::accepted, this, &TelemetryConsentDialog::save);
	connect(buttons, &QDialogButtonBox::rejected, this, &TelemetryConsentDialog::reject);
	connect(m_enabledBox, &QCheckBox::toggled, this, &TelemetryConsentDialog::refreshPreview);
	connect(m_hardwareBox, &QCheckBox::toggled, this, &TelemetryConsentDialog::refreshPreview);
	connect(m_featureBox, &QCheckBox::toggled, this, &TelemetryConsentDialog::refreshPreview);
	connect(m_crashBox, &QCheckBox::toggled, this, &TelemetryConsentDialog::refreshPreview);

	refreshPreview();
}

TelemetryConsent TelemetryConsentDialog::currentConsent() const
{
	TelemetryConsent consent;
	consent.enabled = m_enabledBox->isChecked();
	consent.hardware = m_hardwareBox->isChecked();
	consent.featureUsage = m_featureBox->isChecked();
	consent.crashCounts = m_crashBox->isChecked();
	consent.consentVersion = m_enabledBox->isChecked() ? Telemetry::ConsentVersion : 0;
	return consent;
}

void TelemetryConsentDialog::refreshPreview()
{
	Telemetry client(currentConsent(), nullptr, TelemetryHardware::collect());
	const QByteArray preview = client.previewJson();
	m_preview->setPlainText(QString::fromUtf8(
			QJsonDocument::fromJson(preview).toJson(QJsonDocument::Indented)));

	if(!Telemetry::isCompiledIn())
	{
		m_status->setText(tr("This build was packaged without telemetry (ZENE_TELEMETRY=OFF); "
			"nothing can be sent, now or later."));
		return;
	}
	m_status->setText(m_enabledBox->isChecked() && (m_hardwareBox->isChecked()
			|| m_featureBox->isChecked() || m_crashBox->isChecked())
		? tr("Telemetry is on. Turn it off to stop sending, immediately.")
		: tr("Telemetry is off. Nothing is sent."));
}

void TelemetryConsentDialog::save()
{
	Telemetry::saveConsent(currentConsent());
	refreshPreview();
}

} // namespace lmms::gui
