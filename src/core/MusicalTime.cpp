/*
 * MusicalTime.cpp - how musician-facing positions and lengths read and are typed
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

#include "MusicalTime.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "ConfigManager.h"
#include "Engine.h"
#include "Song.h"
#include "TempoMap.h"
#include "TimePos.h"

namespace lmms::musicaltime
{

namespace
{

const QString kSection = QStringLiteral("ui");
const QString kKey = QStringLiteral("timeformat");
constexpr tick_t MaxTick = std::numeric_limits<tick_t>::max();
//! Half a millisecond: a time shown to the millisecond and typed back lands on the tick it was read from.
constexpr double HalfMs = 0.0005;

//! The song's meter in ticks: a bar, a beat, a quarter of the beat (the "sixteenth") and how many of
//! those a beat holds. A song-less run (a widget test) reads 4/4.
struct Meter
{
	tick_t perBar = 192;
	tick_t numerator = 4;
	tick_t perBeat = 48;
	tick_t perSixteenth = 12;
	tick_t sixteenths = 4;
};

Meter meter()
{
	Meter m;
	Song* song = Engine::getSong();
	if (song == nullptr) { return m; }
	m.perBar = std::max(1, song->ticksPerBar());
	m.numerator = std::max(1, song->getTimeSigModel().getNumerator());
	m.perBeat = std::max<tick_t>(1, m.perBar / m.numerator);
	m.perSixteenth = std::max<tick_t>(1, m.perBeat / 4);
	m.sixteenths = (m.perBeat + m.perSixteenth - 1) / m.perSixteenth;
	return m;
}

//! A non-negative tick count split into whole bars, beats and sixteenths (0-based) and the ticks left.
//! A denominator that does not divide 192 (5/7) leaves a bar a tick or two longer than its whole beats:
//! those ticks belong to the last beat, never to a beat past the meter (review r4 L19 #13).
struct Split
{
	tick_t bar = 0;
	tick_t beat = 0;
	tick_t sixteenth = 0;
	tick_t rest = 0;
};

Split split(tick_t ticks, const Meter& m)
{
	Split s;
	s.bar = ticks / m.perBar;
	const tick_t inBar = ticks % m.perBar;
	s.beat = std::min(m.numerator - 1, inBar / m.perBeat);
	const tick_t inBeat = inBar - s.beat * m.perBeat;
	s.sixteenth = std::min(m.sixteenths - 1, inBeat / m.perSixteenth);
	s.rest = inBeat - s.sixteenth * m.perSixteenth;
	return s;
}

//! "bar.beat.sixteenth" counted from @a base (1: a position, 0: a length), ".tick" when @a exact needs it.
QString barsBeatsText(tick_t ticks, tick_t base, bool exact)
{
	const Split s = split(ticks, meter());
	QString text = QStringLiteral("%1.%2.%3").arg(s.bar + base).arg(s.beat + base).arg(s.sixteenth + base);
	if (exact && s.rest != 0) { text += QStringLiteral(".%1").arg(s.rest); }
	return text;
}

QString ticksText(tick_t ticks) { return QObject::tr("%1 ticks").arg(ticks); }

double secondsAt(tick_t tick)
{
	const Song* song = Engine::getSong();
	return song != nullptr ? song->secondsAtTick(tick) : TimePos::ticksToMilliseconds(tick, 120) / 1000.0;
}

tick_t tickAtSeconds(double seconds)
{
	Song* song = Engine::getSong();
	if (song == nullptr) { return static_cast<tick_t>(seconds * 1000.0 * 120 / 1250); }
	return song->tempoMap().map().tickAtSeconds(seconds, song->getTempo());
}

//! "m:ss.mmm".
QString timeText(double seconds)
{
	const qint64 ms = std::llround(std::abs(seconds) * 1000.0);
	return QStringLiteral("%1:%2.%3").arg(ms / 60000).arg((ms / 1000) % 60, 2, 10, QLatin1Char('0'))
		.arg(ms % 1000, 3, 10, QLatin1Char('0'));
}

QString positionIn(tick_t tick, tick_t origin, Format as, bool exact)
{
	switch (as)
	{
	case Format::Ticks: return ticksText(tick);
	case Format::Time: return timeText(secondsAt(origin + tick) - secondsAt(origin));
	case Format::BarsBeats: break;
	}
	return barsBeatsText(tick, 1, exact);
}

// ---- typing -------------------------------------------------------------------------------------

enum class Unit
{
	BarsBeats,
	Ticks,
	Time,
	Bars  //!< "1.5 bars": a decimal number of bars
};

//! Takes the first of @a words that ends @a text off it; false when none does.
bool stripSuffix(QString* text, const QStringList& words)
{
	for (const QString& word : words)
	{
		if (word.isEmpty() || !text->endsWith(word, Qt::CaseInsensitive)) { continue; }
		text->chop(word.size());
		*text = text->trimmed();
		return true;
	}
	return false;
}

const QStringList& tickWords()
{
	static const QStringList words{QObject::tr("ticks"), QStringLiteral("ticks"), QStringLiteral("tick"), QStringLiteral("t")};
	return words;
}

const QStringList& barWords()
{
	static const QStringList words{QObject::tr("bars"), QStringLiteral("bars"), QObject::tr("bar"), QStringLiteral("bar")};
	return words;
}

const QStringList& secondWords()
{
	static const QStringList words{QStringLiteral("sec"), QStringLiteral("s")};
	return words;
}

//! The unit @a text is written in (its suffix taken off): a suffix names it, a colon is a time, two dots
//! are bars|beats, and anything else is read in the unit @a as shows.
Unit unitOf(QString* text, Format as)
{
	if (stripSuffix(text, tickWords())) { return Unit::Ticks; }
	if (stripSuffix(text, barWords())) { return Unit::Bars; }
	if (text->contains(QLatin1Char(':')) || stripSuffix(text, secondWords())) { return Unit::Time; }
	if (text->count(QLatin1Char('.')) >= 2) { return Unit::BarsBeats; }
	if (as == Format::Time) { return Unit::Time; }
	return as == Format::Ticks && !text->contains(QLatin1Char('.')) ? Unit::Ticks : Unit::BarsBeats;
}

bool ticksFrom(const QString& text, tick_t* out)
{
	bool ok = false;
	const qint64 value = text.toLongLong(&ok);
	if (!ok || value < 0 || value > MaxTick) { return false; }
	*out = static_cast<tick_t>(value);
	return true;
}

bool decimalBarsFrom(const QString& text, tick_t* out)
{
	bool ok = false;
	const double ticks = text.toDouble(&ok) * meter().perBar;
	if (!ok || ticks < 0 || ticks > MaxTick) { return false; }
	*out = static_cast<tick_t>(std::lround(ticks));
	return true;
}

//! "m:ss.mmm", "h:mm:ss" or plain seconds into @a seconds; false for anything else.
bool secondsFrom(const QString& text, double* seconds)
{
	const QStringList parts = text.split(QLatin1Char(':'));
	bool ok = parts.size() <= 3;
	double total = 0.0;
	for (int i = 0; ok && i < parts.size(); ++i)
	{
		const double value = i + 1 == parts.size() ? parts.at(i).toDouble(&ok) : parts.at(i).toInt(&ok);
		ok = ok && value >= 0 && (i == 0 || value < 60);
		total = total * 60 + value;
	}
	*seconds = total;
	return ok;
}

//! A time typed for a span that starts at @a origin: the ticks from there to that much later.
bool timeFrom(const QString& text, tick_t origin, tick_t* out)
{
	double seconds = 0.0;
	if (!secondsFrom(text, &seconds)) { return false; }
	const tick_t tick = tickAtSeconds(secondsAt(origin) + seconds + HalfMs) - origin;
	if (tick < 0) { return false; }
	*out = tick;
	return true;
}

//! Part @a index of @a parts as a number in [@a lo, @a hi] into @a out; true for an absent part (@a out kept).
bool partIn(const QStringList& parts, int index, tick_t lo, tick_t hi, tick_t* out)
{
	if (index >= parts.size()) { return true; }
	bool ok = false;
	const int value = parts.at(index).toInt(&ok);
	if (!ok || value < lo || value > hi) { return false; }
	*out = value;
	return true;
}

//! "bar[.beat[.sixteenth[.tick]]]" counted from @a base (1: a position, 0: a length). A beat inside the
//! bar, a sixteenth inside the beat, and a bar whose start still fits a tick count (review L19 #6, #7).
bool barsBeatsFrom(const QString& text, tick_t base, tick_t* out)
{
	const Meter m = meter();
	const QStringList parts = text.split(QLatin1Char('.'));
	tick_t bar = base;
	tick_t beat = base;
	tick_t sixteenth = base;
	tick_t rest = 0;
	if (parts.size() > 4 || !partIn(parts, 0, base, MaxTick / m.perBar - 1 + base, &bar)
		|| !partIn(parts, 1, base, m.numerator - 1 + base, &beat)
		|| !partIn(parts, 2, base, m.sixteenths - 1 + base, &sixteenth)
		// The tick remainder counts INSIDE the sixteenth above it, so it is bounded by that sixteenth
		// (what barsBeatsText's split() emits), never by the whole beat: "1.1.1.47" is not a position
		// and would not read back as itself.
		|| !partIn(parts, 3, 0, m.perSixteenth - 1, &rest))
	{
		return false;
	}
	*out = (bar - base) * m.perBar + (beat - base) * m.perBeat + (sixteenth - base) * m.perSixteenth + rest;
	return true;
}

//! Typed @a text (no sign) into ticks: a position when @a base is 1, a length when it is 0.
bool spanFrom(QString text, tick_t* out, tick_t origin, Format as, tick_t base)
{
	text = text.trimmed();
	switch (unitOf(&text, as))
	{
	case Unit::Ticks: return ticksFrom(text, out);
	case Unit::Time: return timeFrom(text, origin, out);
	case Unit::Bars:
		if (base == 0) { return decimalBarsFrom(text, out); }
		break;
	case Unit::BarsBeats: break;
	}
	return barsBeatsFrom(text, base, out);
}

} // namespace

const QStringList& formatNames()
{
	static const QStringList names{QStringLiteral("bars_beats"), QStringLiteral("ticks"), QStringLiteral("time")};
	return names;
}

QString formatLabel(Format format)
{
	switch (format)
	{
	case Format::Ticks: return QObject::tr("Ticks");
	case Format::Time: return QObject::tr("Time (min:sec)");
	case Format::BarsBeats: break;
	}
	return QObject::tr("Bars|Beats");
}

Format currentFormat()
{
	const auto index = formatNames().indexOf(ConfigManager::inst()->value(kSection, kKey, formatNames().first()));
	return index < 0 ? Format::BarsBeats : static_cast<Format>(index);
}

void setFormat(Format next)
{
	ConfigManager::inst()->setValue(kSection, kKey, formatNames().at(static_cast<int>(next)));
	emit formatSignal()->changed();
}

FormatSignal* formatSignal()
{
	static FormatSignal signal;
	return &signal;
}

QString positionText(tick_t tick, tick_t origin, Format as)
{
	return positionIn(tick, origin, as, true);
}

QString counterText(tick_t tick, Format as)
{
	return positionIn(tick, 0, as, false);
}

QString lengthText(tick_t length, tick_t at, Format as)
{
	const tick_t magnitude = length < 0 ? -length : length;
	QString text;
	switch (as)
	{
	case Format::Ticks: text = ticksText(magnitude); break;
	case Format::Time: text = timeText(secondsAt(at + magnitude) - secondsAt(at)); break;
	case Format::BarsBeats: text = barsBeatsText(magnitude, 0, true); break;
	}
	return length < 0 ? QLatin1Char('-') + text : text;
}

QString barBeatText(tick_t tick)
{
	const Split s = split(std::max<tick_t>(0, tick), meter());
	return QStringLiteral("%1.%2").arg(s.bar + 1).arg(s.beat + 1);
}

bool positionFromText(const QString& text, tick_t* tick, tick_t origin, Format as)
{
	return spanFrom(text, tick, origin, as, 1);
}

bool lengthFromText(const QString& text, tick_t* length, tick_t at, Format as)
{
	QString body = text.trimmed();
	const bool negative = body.startsWith(QLatin1Char('-'));
	if (negative) { body.remove(0, 1); }
	tick_t magnitude = 0;
	if (!spanFrom(body, &magnitude, at, as, 0)) { return false; }
	*length = negative ? -magnitude : magnitude;
	return true;
}

} // namespace lmms::musicaltime
