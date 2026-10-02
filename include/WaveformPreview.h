/*
 * WaveformPreview.h - the browser's waveform of the selected sample (browser.peaks)
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

#ifndef LMMS_GUI_WAVEFORM_PREVIEW_H
#define LMMS_GUI_WAVEFORM_PREVIEW_H

#include <QString>
#include <QVector>
#include <QWidget>

#include "lmms_export.h"

namespace lmms::gui
{

/*! WF-010: the waveform of the sample selected in the browser, drawn from browser.peaks - the
 *  same bounded peak cache an agent reads, so the picture and the socket agree. Nothing is
 *  decoded here: an unreadable file, or a path that is not audio, shows its reason as text. */
class LMMS_EXPORT WaveformPreview : public QWidget
{
	Q_OBJECT
public:
	explicit WaveformPreview(QWidget* parent = nullptr);

	//! Show @a path's waveform (empty clears it). Returns whether peaks were read.
	bool setPath(const QString& path);
	QString path() const { return m_path; }
	//! The peaks drawn, as (min, max) pairs; empty when none.
	const QVector<QPair<float, float>>& peaks() const { return m_peaks; }
	QString message() const { return m_message; }

	QSize sizeHint() const override;

protected:
	void paintEvent(QPaintEvent* event) override;

private:
	QString m_path;
	QString m_message;
	QVector<QPair<float, float>> m_peaks;
	double m_seconds = 0;
};

} // namespace lmms::gui

#endif // LMMS_GUI_WAVEFORM_PREVIEW_H
