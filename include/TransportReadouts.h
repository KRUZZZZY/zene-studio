/*
 * TransportReadouts.h - the shell header's tempo, position, meter, level and load readouts
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

#ifndef LMMS_GUI_TRANSPORT_READOUTS_H
#define LMMS_GUI_TRANSPORT_READOUTS_H

#include <functional>

#include <QPointer>
#include <QWidget>

#include "AutomatableModel.h"
#include "ControlRegistry.h"
#include "lmms_export.h"

class QActionGroup;
class QLineEdit;

namespace lmms
{
class Song;
}

namespace lmms::gui
{

//! A header readout: a small caption over a value, painted from the design tokens - the
//! modern replacement for the LCD digits. A bound integer model is edited the way every pro
//! app edits a number: drag up/down (Shift: fine), the wheel, or double-click to type. The
//! drag is ONE journal step, exactly as the old LCD spin box made it.
class LMMS_EXPORT ReadoutField : public QWidget
{
	Q_OBJECT

public:
	ReadoutField(const QString& caption, QWidget* parent = nullptr);

	//! Show and edit @a model; @a format turns its value into the text shown.
	void bind(IntModel* model, std::function<QString(int)> format, int pixelsPerStep = 4);
	//! A readout whose text comes from @a text, refreshed by refresh(); with @a typed it can be
	//! typed into (double-click), the handler returning false for text it refuses.
	void bindText(std::function<QString()> text, std::function<bool(const QString&)> typed = {});
	//! Like bindText(), but preserves a registered command's refusal message for the field's error display.
	void bindTextResult(std::function<QString()> text, std::function<ControlResult(const QString&)> typed);
	void refresh();

	QString caption() const { return m_caption; }
	QString valueText() const { return m_value; }
	QSize sizeHint() const override;

protected:
	void paintEvent(QPaintEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	void mouseDoubleClickEvent(QMouseEvent* event) override;
	void wheelEvent(QWheelEvent* event) override;
	void keyPressEvent(QKeyEvent* event) override;

private:
	void step(int delta);
	void commitTyped();

	QString m_caption;
	QString m_value;
	QPointer<IntModel> m_model;
	std::function<QString(int)> m_format;
	std::function<QString()> m_text;
	std::function<bool(const QString&)> m_typed;
	std::function<ControlResult(const QString&)> m_typedResult;
	int m_pixelsPerStep = 4;
	int m_pressY = 0;
	int m_pressValue = 0;
	bool m_dragging = false;
	QLineEdit* m_editor = nullptr;
};

//! "bar.beat" (both 1-based) of a tick position under the song's meter (musicaltime::barBeatText).
LMMS_EXPORT QString barBeatText(int ticks);
//! The tick position of "bar", "bar.beat", "bar.beat.sixteenth" or "bar.beat.sixteenth.tick" (or ticks or a
//! time with their unit - musicaltime::positionFromText, read as bars|beats); false for anything else, a beat
//! past the meter's or a sixteenth past the beat's included.
LMMS_EXPORT bool ticksFromBarBeat(const QString& text, int* ticks);
//! Theme 7: right-click choices on @a widget - Bars|Beats, Ticks, Time - each running transport.time_format,
//! the one in use checked. For every field that shows a musical position or length.
LMMS_EXPORT void addTimeFormatChoices(QWidget* widget);

//! WF-085: the words the header's Rec left readout shows for @a state - the transcription of
//! record.remaining's seconds: "2h 3m" (rounded down to the minute), "0m" when the budget is
//! spent (a zero estimate), "-" when the command refused (a volume it could not measure).
//! Exported for the readout's tests; not a JSON value, whatever the estimate is:
QString recordRemainingLabel(const ControlResult& state);

//! The header's transport readouts, laid out in the strip: tempo, meter, position (in the time format, theme 7) and
//! time, the record mode with its scope (theme 7), the recording budget (record.remaining, WF-085), master level and
//! transposition, and the engine load.
//! Replaces the song editor's LCD boxes, sliders and scope (docs/UI-SHELL.md §1).
class LMMS_EXPORT TransportReadouts : public QWidget
{
	Q_OBJECT

public:
	explicit TransportReadouts(QWidget* parent = nullptr);
	//! Re-read what moves on its own: the position, the time, the load and the recording budget (60 Hz from the
	//! main window's periodic update; the budget once a second, the disk's free space does not move faster).
	void tick();

	//! WF-085 test seam: while @a source is set, the Rec left readout takes its ControlResult instead of
	//! asking ControlRegistry for record.remaining, so a test can present a value that changed since the
	//! bind. A null function restores the live command (the default; UI thread only, like the readout).
	using RemainingTestSource = std::function<ControlResult()>;
	static void setRemainingTestSource(RemainingTestSource source);

	ReadoutField* tempo() const { return m_tempo; }
	ReadoutField* loudness() const { return m_loudness; }
	ReadoutField* recordRemaining() const { return m_recordRemaining; }
	//! Q050-094: the Rec mode readout (overdub, replace or keep history; right-click chooses).
	ReadoutField* recordMode() const { return m_recordMode; }

private:
	void bindToSong(Song* song);
	//! The record mode's readout, tooltip (its scope) and checked choice, when the mode has changed.
	void syncRecordMode();

	ReadoutField* m_tempo;
	ReadoutField* m_meter;
	ReadoutField* m_position;
	ReadoutField* m_time;
	ReadoutField* m_recordMode;
	ReadoutField* m_recordRemaining;
	QActionGroup* m_recordModes = nullptr;
	int m_shownRecordMode = -1;
	ReadoutField* m_volume;
	ReadoutField* m_pitch;
	ReadoutField* m_load;
	ReadoutField* m_loudness;
	int m_ticks = 0;
};

} // namespace lmms::gui

#endif // LMMS_GUI_TRANSPORT_READOUTS_H
