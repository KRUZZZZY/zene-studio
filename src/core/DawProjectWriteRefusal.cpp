/*
 * DawProjectWriteRefusal.cpp - the tempo and metre values the writer refuses.
 *
 * The two halves of the model the engine can say in a form it cannot read back:
 * a tempo outside the engine's own bounds, a metre whose numerator is outside the
 * reader's 1..32 or whose denominator is not a power of two. Declared in
 * DawProjectWriteRefusal.h; called from modelRefusal in DawProjectWrite.cpp.
 *
 * Split out of DawProjectWrite.cpp, which sat at gate 7's 500-code-line cap: these
 * refusals and their two predicates moved here unchanged, the numerator checks
 * aside, so the writer file keeps its room.
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
 */

#include "DawProjectWriteRefusal.h"

#include <cmath>

#include <QString>

#include "InstrumentMidiParts.h"
#include "Note.h"
#include "TempoMap.h"

namespace lmms
{
namespace interchange
{

namespace
{

bool isPowerOfTwo(int value)
{
	return value > 0 && (value & (value - 1)) == 0;
}

//! The tempo of one point or of the session, in bounds AND finite. A NaN
//! compares false against both bounds, so the range test alone lets it through.
bool isSupportedTempo(double bpm)
{
	return std::isfinite(bpm) && bpm >= DawProjectMinTempo && bpm <= DawProjectMaxTempo;
}

bool inIntRange(int value, int minimum, int maximum)
{
	return value >= minimum && value <= maximum;
}

bool inDoubleRange(double value, double minimum, double maximum)
{
	return std::isfinite(value) && value >= minimum && value <= maximum;
}

} // namespace

QString tempoRefusal(const DawProjectModel& model)
{
	if (!isSupportedTempo(model.tempo))
	{
		return QStringLiteral("the session tempo %1 is outside the engine's own %2..%3, so "
			"the file would say something this engine cannot read back")
			.arg(model.tempo).arg(DawProjectMinTempo).arg(DawProjectMaxTempo);
	}
	for (const DawProjectPoint& point : model.tempoPoints)
	{
		if (!isSupportedTempo(point.value))
		{
			return QStringLiteral("a tempo-map point at beat %1 carries %2 bpm, outside the "
				"engine's own %3..%4").arg(point.time).arg(point.value)
				.arg(DawProjectMinTempo).arg(DawProjectMaxTempo);
		}
	}
	return QString();
}

QString meterRefusal(const DawProjectModel& model)
{
	if (model.numerator < TempoMapMinNumerator || model.numerator > TempoMapMaxNumerator)
	{
		return QStringLiteral("the time signature's numerator %1 is outside the engine's own %2..%3")
			.arg(model.numerator).arg(TempoMapMinNumerator).arg(TempoMapMaxNumerator);
	}
	if (!isPowerOfTwo(model.denominator))
	{
		return QStringLiteral("the time signature's denominator %1 is not a power of two")
			.arg(model.denominator);
	}
	for (const DawProjectPoint& point : model.meterPoints)
	{
		if (point.numerator < TempoMapMinNumerator || point.numerator > TempoMapMaxNumerator)
		{
			return QStringLiteral("a metre point at beat %1 has numerator %2, outside the engine's "
				"own %3..%4").arg(point.time).arg(point.numerator)
				.arg(TempoMapMinNumerator).arg(TempoMapMaxNumerator);
		}
		if (!isPowerOfTwo(point.denominator))
		{
			return QStringLiteral("a metre point at beat %1 has denominator %2, which is not a "
				"power of two").arg(point.time).arg(point.denominator);
		}
	}
	return QString();
}

QString noteFieldsRefusal(const DawProjectNote& note)
{
	if (!inIntRange(note.channel, 0, InstrumentMidiParts::Channels - 1))
	{
		return QStringLiteral("the note channel %1 is outside the supported range 0..%2")
			.arg(note.channel).arg(InstrumentMidiParts::Channels - 1);
	}
	if (!inIntRange(note.key, 0, NumKeys - 1))
	{
		return QStringLiteral("the note key %1 is outside the supported range 0..%2")
			.arg(note.key).arg(NumKeys - 1);
	}
	if (!inDoubleRange(note.vel, 0.0, 1.0))
	{
		return QStringLiteral("the note vel %1 is outside the supported range 0..1").arg(note.vel);
	}
	if (!inDoubleRange(note.rel, 0.0, 1.0))
	{
		return QStringLiteral("the note rel %1 is outside the supported range 0..1").arg(note.rel);
	}
	return QString();
}

QString scalarFieldsRefusal(const DawProjectModel& model)
{
	for (const DawProjectMixerChannel& channel : model.mixerChannels)
	{
		if (!inDoubleRange(channel.volume, 0.0, 2.0))
		{
			return QStringLiteral("the mixer channel volume %1 is outside the supported range 0..2")
				.arg(channel.volume);
		}
	}
	for (const DawProjectTrack& track : model.tracks)
	{
		if (track.hasVolume && !inDoubleRange(track.volume, 0.0, 2.0))
		{
			return QStringLiteral("the track volume %1 is outside the supported range 0..2")
				.arg(track.volume);
		}
		if (track.hasPan && !inDoubleRange(track.pan, 0.0, 1.0))
		{
			return QStringLiteral("the track pan %1 is outside the supported range 0..1")
				.arg(track.pan);
		}
	}
	return QString();
}

} // namespace interchange
} // namespace lmms
