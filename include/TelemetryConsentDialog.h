/*
 * TelemetryConsentDialog.h - the "what we send" consent and preview screen
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

#ifndef LMMS_TELEMETRY_CONSENT_DIALOG_H
#define LMMS_TELEMETRY_CONSENT_DIALOG_H

#include "Telemetry.h"

#include <QDialog>

class QCheckBox;
class QLabel;
class QPlainTextEdit;

namespace lmms::gui
{

//! Renders the exact payload the client would send, live, from the same
//! builder submit() uses. There is no second list here that could drift: the
//! text in the preview is Telemetry::previewJson().
class TelemetryConsentDialog : public QDialog
{
	Q_OBJECT

public:
	explicit TelemetryConsentDialog(QWidget * parent = nullptr);

private slots:
	void refreshPreview();
	void save();

private:
	TelemetryConsent currentConsent() const;

	QCheckBox * m_enabledBox = nullptr;
	QCheckBox * m_hardwareBox = nullptr;
	QCheckBox * m_featureBox = nullptr;
	QCheckBox * m_crashBox = nullptr;
	QPlainTextEdit * m_preview = nullptr;
	QLabel * m_status = nullptr;
};

} // namespace lmms::gui

#endif // LMMS_TELEMETRY_CONSENT_DIALOG_H
