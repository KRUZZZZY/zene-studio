/*
 * SampleRangeFields.h - the sample editor's typed range: start, end and length fields
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

#ifndef LMMS_GUI_SAMPLE_RANGE_FIELDS_H
#define LMMS_GUI_SAMPLE_RANGE_FIELDS_H

#include <QSpinBox>
#include <QString>
#include <QStringList>

#include "LmmsTypes.h"
#include "MusicalTime.h"
#include "lmms_export.h"

namespace lmms::gui
{

/*! 0.5.0 theme 15: a sample editor range is in the clip's SOURCE frames, End exclusive - [start, end), as
 *  sample.edit_range takes it - and reads in one of three units: the time from the source's start, bars and
 *  beats at the song's tempo from where the clip starts (MusicalTime's text), or the frames themselves. */
namespace samplerange
{

enum class Unit
{
	Time = 0,
	BarsBeats = 1,
	Samples = 2
};

//! "time", "bars_beats", "samples" - window.sample_editor's `unit` enum, in Unit order.
LMMS_EXPORT const QStringList& unitNames();
//! "Time (min:sec)", "Bars|Beats", "Samples" - what the unit menu shows.
LMMS_EXPORT QString unitLabel(Unit unit);
//! The unit that follows the musical-time format: its time and bars|beats, and samples for ticks (a source
//! file has frames, not ticks).
LMMS_EXPORT Unit unitFor(musicaltime::Format format);

//! What a frame count needs to read as a time or as bars and beats.
struct Context
{
	int sampleRate = 44100;
	tick_t origin = 0;  //!< the song tick the clip starts at: where the bars and beats are measured from
};

//! Frame @a frame (a position; @a length: a length starting at frame @a at) as @a unit reads it.
LMMS_EXPORT QString frameText(qint64 frame, const Context& context, Unit unit, bool length = false, qint64 at = 0);
//! Typed @a text into @a frame: "1:02.500" or "62.5 s" as a time, "3.2.1" as bars|beats, "2756 smp" as frames -
//! a suffix names its unit, otherwise @a unit reads it. False for text that is none of them.
LMMS_EXPORT bool frameFromText(const QString& text, const Context& context, Unit unit, qint64* frame,
	bool length = false, qint64 at = 0);
//! The frames one arrow-key step moves a field: a frame, a millisecond, or a sixteenth at the clip's tempo.
LMMS_EXPORT int stepFrames(const Context& context, Unit unit);

} // namespace samplerange

//! One of the sample editor's range fields - Start, End or Length - holding source frames and showing them
//! in the chosen unit. Up and Down step it, typing replaces it (applied on Enter or when the focus leaves),
//! and a screen reader reads the text it shows.
class LMMS_EXPORT SampleFrameField : public QSpinBox
{
public:
	SampleFrameField(bool length, QWidget* parent = nullptr);
	void setFormat(const samplerange::Context& context, samplerange::Unit unit);
	//! A length field measures from the range's start.
	void setLengthStart(qint64 at) { m_at = at; }

protected:
	QString textFromValue(int value) const override;
	int valueFromText(const QString& text) const override;
	QValidator::State validate(QString& text, int& pos) const override;

private:
	samplerange::Context m_context;
	samplerange::Unit m_unit = samplerange::Unit::Time;
	bool m_length;
	qint64 m_at = 0;
};

} // namespace lmms::gui

#endif // LMMS_GUI_SAMPLE_RANGE_FIELDS_H
