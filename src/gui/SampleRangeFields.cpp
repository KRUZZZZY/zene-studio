/*
 * SampleRangeFields.cpp - the sample editor's typed range: start, end and length fields
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

#include "SampleRangeFields.h"

#include <algorithm>
#include <cmath>

#include <QCoreApplication>
#include <QLineEdit>
#include <QRegularExpression>

#include "Engine.h"
#include "Song.h"
#include "TempoMap.h"

namespace lmms::gui
{

namespace samplerange
{

namespace
{

QString tr(const char* text) { return QCoreApplication::translate("SampleRange", text); }

//! A song-less run (a widget test) reads 120 BPM: 48 ticks a beat, two beats a second.
constexpr double TicksPerSecondWithoutSong = 96.0;
//! A sixteenth note at 192 ticks a bar: the bars|beats fields' arrow step.
constexpr tick_t SixteenthTicks = 12;

double secondsOfFrame(qint64 frame, const Context& context)
{
	return static_cast<double>(frame) / std::max(1, context.sampleRate);
}

qint64 frameOfSeconds(double seconds, const Context& context)
{
	return std::llround(seconds * std::max(1, context.sampleRate));
}

//! The seconds the song takes from the clip's start to @a tick ticks after it.
double secondsAfterOrigin(tick_t tick, const Context& context)
{
	const Song* song = Engine::getSong();
	if (song == nullptr) { return tick / TicksPerSecondWithoutSong; }
	return song->secondsAtTick(context.origin + tick) - song->secondsAtTick(context.origin);
}

tick_t tickOfFrame(qint64 frame, const Context& context)
{
	const double seconds = secondsOfFrame(frame, context);
	Song* song = Engine::getSong();
	if (song == nullptr) { return static_cast<tick_t>(seconds * TicksPerSecondWithoutSong); }
	return song->tempoMap().map().tickAtSeconds(song->secondsAtTick(context.origin) + seconds, song->getTempo()) - context.origin;
}

qint64 frameOfTick(tick_t tick, const Context& context)
{
	return frameOfSeconds(secondsAfterOrigin(tick, context), context);
}

//! "m:ss.mmm", to the millisecond.
QString clockText(double seconds)
{
	const qint64 ms = std::llround(seconds * 1000.0);
	return QStringLiteral("%1:%2.%3").arg(ms / 60000).arg((ms / 1000) % 60, 2, 10, QLatin1Char('0'))
		.arg(ms % 1000, 3, 10, QLatin1Char('0'));
}

//! Takes the first of @a words that ends @a text off it.
bool stripWord(QString* text, const QStringList& words)
{
	for (const QString& word : words)
	{
		if (!text->endsWith(word, Qt::CaseInsensitive)) { continue; }
		text->chop(word.size());
		*text = text->trimmed();
		return true;
	}
	return false;
}

bool framesFrom(const QString& text, qint64* frame)
{
	bool ok = false;
	const qint64 value = text.toLongLong(&ok);
	if (!ok || value < 0) { return false; }
	*frame = value;
	return true;
}

//! "[[h:]m:]s[.fff]" into frames; minutes and seconds under 60 once a larger part leads them.
bool timeFrom(const QString& text, const Context& context, qint64* frame)
{
	static const QRegularExpression clock(QStringLiteral("^(?:(\\d+):)?(?:(\\d+):)?(\\d+(?:\\.\\d*)?)$"));
	const QRegularExpressionMatch match = clock.match(text);
	if (!match.hasMatch()) { return false; }
	const bool hours = (match.capturedStart(2) >= 0);
	const double h = hours ? match.captured(1).toDouble() : 0.0;
	const double m = hours ? match.captured(2).toDouble() : match.captured(1).toDouble();
	const double s = match.captured(3).toDouble();
	if (((match.capturedStart(1) >= 0) && s >= 60.0) || (hours && m >= 60.0)) { return false; }
	*frame = frameOfSeconds(h * 3600.0 + m * 60.0 + s, context);
	return true;
}

//! Bars|beats (MusicalTime's reading) at the song's tempo from the clip's start into frames.
bool barsFrom(const QString& text, const Context& context, bool length, qint64 at, qint64* frame)
{
	tick_t ticks = 0;
	if (!length)
	{
		if (!musicaltime::positionFromText(text, &ticks, context.origin, musicaltime::Format::BarsBeats)) { return false; }
		*frame = frameOfTick(ticks, context);
		return true;
	}
	const tick_t start = tickOfFrame(at, context);
	if (!musicaltime::lengthFromText(text, &ticks, context.origin + start, musicaltime::Format::BarsBeats) || ticks < 0)
	{
		return false;
	}
	*frame = frameOfTick(start + ticks, context) - frameOfTick(start, context);
	return true;
}

} // namespace

const QStringList& unitNames()
{
	static const QStringList names{QStringLiteral("time"), QStringLiteral("bars_beats"), QStringLiteral("samples")};
	return names;
}

QString unitLabel(Unit unit)
{
	switch (unit)
	{
	case Unit::BarsBeats: return musicaltime::formatLabel(musicaltime::Format::BarsBeats);
	case Unit::Samples: return tr("Samples");
	case Unit::Time: break;
	}
	return musicaltime::formatLabel(musicaltime::Format::Time);
}

Unit unitFor(musicaltime::Format format)
{
	switch (format)
	{
	case musicaltime::Format::BarsBeats: return Unit::BarsBeats;
	case musicaltime::Format::Ticks: return Unit::Samples;
	case musicaltime::Format::Time: break;
	}
	return Unit::Time;
}

QString frameText(qint64 frame, const Context& context, Unit unit, bool length, qint64 at)
{
	switch (unit)
	{
	case Unit::Samples: return tr("%1 smp").arg(frame);
	case Unit::Time: return clockText(secondsOfFrame(frame, context));
	case Unit::BarsBeats: break;
	}
	if (!length) { return musicaltime::positionText(tickOfFrame(frame, context), context.origin, musicaltime::Format::BarsBeats); }
	const tick_t start = tickOfFrame(at, context);
	return musicaltime::lengthText(tickOfFrame(at + frame, context) - start, context.origin + start, musicaltime::Format::BarsBeats);
}

bool frameFromText(const QString& text, const Context& context, Unit unit, qint64* frame, bool length, qint64 at)
{
	QString body = text.trimmed();
	static const QStringList sampleWords{tr("samples"), QStringLiteral("samples"), tr("smp"), QStringLiteral("smp")};
	static const QStringList barWords{QStringLiteral("bars"), QStringLiteral("bar")};
	static const QStringList secondWords{QStringLiteral("sec"), QStringLiteral("s")};
	if (stripWord(&body, sampleWords)) { return framesFrom(body, frame); }
	if (body.endsWith(barWords.front(), Qt::CaseInsensitive) || body.endsWith(barWords.back(), Qt::CaseInsensitive))
	{
		return barsFrom(body, context, length, at, frame);
	}
	// The word off first: a clock keeps its colon and its "s" word, and one names the unit just as well.
	if (stripWord(&body, secondWords) || body.contains(QLatin1Char(':'))) { return timeFrom(body, context, frame); }
	switch (unit)
	{
	case Unit::Samples: return framesFrom(body, frame);
	case Unit::Time: return timeFrom(body, context, frame);
	case Unit::BarsBeats: break;
	}
	return barsFrom(body, context, length, at, frame);
}

int stepFrames(const Context& context, Unit unit)
{
	switch (unit)
	{
	case Unit::Samples: return 1;
	case Unit::Time: return std::max(1, context.sampleRate / 1000);
	case Unit::BarsBeats: break;
	}
	return static_cast<int>(std::max<qint64>(1, frameOfTick(SixteenthTicks, context) - frameOfTick(0, context)));
}

} // namespace samplerange

SampleFrameField::SampleFrameField(bool length, QWidget* parent) :
	QSpinBox(parent),
	m_length(length)
{
	setKeyboardTracking(false);  // a typed value applies on Enter or when the focus leaves, not per keystroke
	setAccelerated(true);
	setCorrectionMode(QAbstractSpinBox::CorrectToPreviousValue);
	setRange(0, 0);
}

void SampleFrameField::setFormat(const samplerange::Context& context, samplerange::Unit unit)
{
	m_context = context;
	m_unit = unit;
	setSingleStep(samplerange::stepFrames(context, unit));
	lineEdit()->setText(textFromValue(value()));  // the same value in the new unit
}

QString SampleFrameField::textFromValue(int value) const
{
	return samplerange::frameText(value, m_context, m_unit, m_length, m_at);
}

int SampleFrameField::valueFromText(const QString& text) const
{
	qint64 frame = 0;
	if (!samplerange::frameFromText(text, m_context, m_unit, &frame, m_length, m_at)) { return value(); }
	return static_cast<int>(std::clamp<qint64>(frame, minimum(), maximum()));
}

QValidator::State SampleFrameField::validate(QString& text, int&) const
{
	qint64 frame = 0;
	if (!samplerange::frameFromText(text, m_context, m_unit, &frame, m_length, m_at)) { return QValidator::Intermediate; }
	return frame >= minimum() && frame <= maximum() ? QValidator::Acceptable : QValidator::Intermediate;
}

} // namespace lmms::gui
