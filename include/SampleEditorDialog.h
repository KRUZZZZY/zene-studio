/*
 * SampleEditorDialog.h - a sample clip's source edited over a selection
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
#ifndef LMMS_GUI_SAMPLE_EDITOR_DIALOG_H
#define LMMS_GUI_SAMPLE_EDITOR_DIALOG_H

#include <algorithm>

#include <QDialog>
#include <QPointer>
#include <QString>
#include <QWidget>

#include "SampleThumbnail.h"
#include "lmms_export.h"

namespace lmms
{
class SampleClip;
}

namespace lmms::gui
{

//! The waveform of a sample clip's whole source, with a selection dragged across it (source frames).
class LMMS_EXPORT SampleSelectionView : public QWidget
{
	Q_OBJECT
public:
	explicit SampleSelectionView(QWidget* parent = nullptr);
	void setClip(SampleClip* clip);
	int from() const { return std::min(m_anchor, m_end); }
	int to() const { return std::max(m_anchor, m_end); }
	QSize sizeHint() const override { return {640, 180}; }

protected:
	void paintEvent(QPaintEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;

private:
	int frameAt(int x) const;
	QPointer<SampleClip> m_clip;
	SampleThumbnail m_thumbnail;
	int m_frames = 0;
	int m_anchor = 0;
	int m_end = 0;
};

/*! 0.5.0 L05 (ED-156): the sample editor - drag a selection over the clip's source, then gain,
 *  silence, delete, reverse, fade or crop it. Every button runs sample.edit_range, so an agent's edit
 *  and the editor's are one path and one undo step each. */
class LMMS_EXPORT SampleEditorDialog : public QDialog
{
	Q_OBJECT
public:
	SampleEditorDialog(SampleClip* clip, QWidget* parent = nullptr);

private:
	void apply(const QString& op, double gainDb = 0.0);
	QPointer<SampleClip> m_clip;
	SampleSelectionView* m_view;
};

} // namespace lmms::gui

#endif // LMMS_GUI_SAMPLE_EDITOR_DIALOG_H
