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

//! The header's readouts, laid out in the strip: tempo, meter, position (in the time format, theme 7) and
//! time, the record mode with its scope (theme 7), master level and transposition, and the engine load.
//! Replaces the song editor's LCD boxes, sliders and scope (docs/UI-SHELL.md §1).
class LMMS_EXPORT TransportReadouts : public QWidget
{
	Q_OBJECT

public:
	explicit TransportReadouts(QWidget* parent = nullptr);
	//! Re-read what moves on its own: the position, the time and the load (60 Hz from the
	//! main window's periodic update).
	void tick();

	ReadoutField* tempo() const { return m_tempo; }
	ReadoutField* loudness() const { return m_loudness; }

private:
	void bindToSong(Song* song);
	//! The record mode's readout, tooltip (its scope) and checked choice, when the mode has changed.
	void syncRecordMode();

	ReadoutField* m_tempo;
	ReadoutField* m_meter;
	ReadoutField* m_position;
	ReadoutField* m_time;
	ReadoutField* m_recordMode;
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
