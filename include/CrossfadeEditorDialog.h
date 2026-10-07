/*
 * CrossfadeEditorDialog.h - the crossfade editor: both sides of two overlapping clips' crossfade
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

#ifndef LMMS_GUI_CROSSFADE_EDITOR_DIALOG_H
#define LMMS_GUI_CROSSFADE_EDITOR_DIALOG_H

#include <QDialog>
#include <QPointer>
#include <QString>
#include <QWidget>

#include "ClipEdits.h"
#include "lmms_export.h"

class QComboBox;
class QLabel;
class QSpinBox;

namespace lmms
{
class Clip;
class SampleClip;
}

namespace lmms::gui
{

//! One side of a crossfade as the editor shows it: the ramp's length in ticks and its shape.
struct CrossfadeSide
{
	int ticks = 0;
	FadeShape shape = FadeShape::EqualPower;
};

/*! The two ramps of a crossfade drawn over the span they cover: the outgoing clip's fade-out ending at its end,
 *  the incoming clip's fade-in starting at its start, each with its shape (fadeShapeGain). */
class LMMS_EXPORT CrossfadeCurveView : public QWidget
{
	Q_OBJECT
public:
	explicit CrossfadeCurveView(QWidget* parent = nullptr);
	//! The outgoing clip ends at tick @a outEnd and the incoming one starts at @a inStart (song ticks).
	void setRamps(int outEnd, const CrossfadeSide& out, int inStart, const CrossfadeSide& in);
	QSize sizeHint() const override { return {420, 140}; }

protected:
	void paintEvent(QPaintEvent* event) override;

private:
	int m_outEnd = 0;
	int m_inStart = 0;
	CrossfadeSide m_out;
	CrossfadeSide m_in;
};

/*! 0.5.0 L05 (ED-176): the crossfade editor. Two audio clips of one track that overlap: the outgoing clip's
 *  fade-out and the incoming clip's fade-in, each a length in ticks and a shape, read from the clips (the overlap
 *  and equal power where a side has no ramp yet) and drawn over the span they cover. Apply runs clip.crossfade
 *  with both sides - the agent's command, one undo step - and the status line says what happened; a refusal
 *  leaves the clips as they were. Opened from an audio clip's context menu while another clip overlaps it. */
class LMMS_EXPORT CrossfadeEditorDialog : public QDialog
{
	Q_OBJECT
public:
	//! @a out starts first; the two must overlap on one track.
	CrossfadeEditorDialog(SampleClip* out, SampleClip* in, QWidget* parent = nullptr);
	//! The open editor of this pair, or a new one (shown either way); the pair in start order.
	static CrossfadeEditorDialog* openFor(SampleClip* first, SampleClip* second, QWidget* parent);
	//! The audio clip on @a clip's track that overlaps it - the nearest one after it, else the nearest before -
	//! or nullptr.
	static SampleClip* overlapping(const Clip* clip);

	SampleClip* outClip() const;  // out of line: QPointer<SampleClip>::data() needs the complete type
	SampleClip* inClip() const;
	//! The ticks the two clips share.
	int overlap() const;
	CrossfadeSide outSide() const;
	CrossfadeSide inSide() const;
	void setOutSide(const CrossfadeSide& side);
	void setInSide(const CrossfadeSide& side);
	//! Reads both sides back from the clips (what Apply wrote, or an undo restored).
	void reload();
	//! Runs clip.crossfade with the fields' values; true when it was applied.
	bool apply();
	QString status() const;

private:
	QWidget* buildSide(const QString& title, QSpinBox** length, QComboBox** shape);
	void sync();
	void report(const QString& message, bool failed);
	QPointer<SampleClip> m_out;
	QPointer<SampleClip> m_in;
	CrossfadeCurveView* m_view;
	QSpinBox* m_outLength = nullptr;
	QSpinBox* m_inLength = nullptr;
	QComboBox* m_outShape = nullptr;
	QComboBox* m_inShape = nullptr;
	QLabel* m_summary = nullptr;
	QLabel* m_status = nullptr;
};

} // namespace lmms::gui

#endif // LMMS_GUI_CROSSFADE_EDITOR_DIALOG_H
