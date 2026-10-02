/*
 * ControlCommandsClipArrange.cpp - clip.repeat and clip.time_scale
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

/*! 0.5.0 L04: ED-030 repeat a clip back to back (clip.repeat - Duplicate is a count of one) and
 *  ED-032 scale a clip's content in time (clip.time_scale): a MIDI clip's notes move and stretch
 *  with it, a sample clip plays at the matching rate (it leads with a source tempo, pitch kept
 *  when its stretch mode keeps it). */

#include <algorithm>
#include <cmath>

#include <QJsonArray>

#include "Clip.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "MidiClip.h"
#include "SampleClip.h"
#include "Song.h"
#include "Track.h"

namespace lmms
{

using namespace control;

namespace
{

constexpr int MaxRepeats = 64;
constexpr double MinFactor = 0.125;
constexpr double MaxFactor = 8.0;

ControlResult repeatClip(const QJsonObject& args)
{
	ClipRef ref;
	ControlResult error;
	if (!resolveClip(args.value(QStringLiteral("clip")).toString(), &ref, &error)) { return error; }
	const int count = args.value(QStringLiteral("count")).toInt(1);
	ref.track->addJournalCheckPoint();
	QJsonArray copies;
	tick_t next = ref.clip->endPosition().getTicks();
	const tick_t length = ref.clip->length().getTicks();
	for (int i = 0; i < count; ++i)
	{
		Clip* copy = ref.clip->clone();
		copy->movePosition(TimePos(next));
		copies.append(clipIdOf(copy));
		next += length;
	}
	Engine::getSong()->setModified();
	QJsonObject result{{QStringLiteral("clip"), clipId(ref.id)}, {QStringLiteral("copies"), copies}};
	result.insert(QStringLiteral("__transaction"), transactionPayload({{QStringLiteral("clip"), clipId(ref.id)}},
		QStringLiteral("clip.delete"), {{QStringLiteral("clips"), copies}}, true,
		QStringLiteral("Track journal checkpoint: undo removes every copy")));
	return ControlResult::success(result);
}

void scaleNotes(MidiClip* clip, double factor)
{
	for (Note* note : clip->notes())
	{
		note->setPos(TimePos(static_cast<tick_t>(std::lround(note->pos().getTicks() * factor))));
		note->setLength(TimePos(std::max<tick_t>(1, static_cast<tick_t>(std::lround(note->length().getTicks() * factor)))));
	}
	clip->rearrangeAllNotes();
}

ControlResult timeScale(const QJsonObject& args)
{
	ClipRef ref;
	ControlResult error;
	if (!resolveClip(args.value(QStringLiteral("clip")).toString(), &ref, &error)) { return error; }
	const double factor = args.value(QStringLiteral("factor")).toDouble();
	if (factor < MinFactor || factor > MaxFactor)
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'factor' must be %1..%2").arg(MinFactor).arg(MaxFactor));
	}
	const tick_t before = ref.clip->length().getTicks();
	ref.clip->addJournalCheckPoint();
	if (auto* midi = dynamic_cast<MidiClip*>(ref.clip)) { scaleNotes(midi, factor); }
	else if (auto* sample = dynamic_cast<SampleClip*>(ref.clip))
	{
		// A source tempo of (project tempo x factor) makes the clip play 1/factor as fast: it leads.
		const double sourceTempo = (sample->warpTempoMode() == WarpTempoMode::SourceTempo ? sample->sourceTempo()
			: static_cast<double>(Engine::getSong()->getTempo())) * factor;
		sample->setWarpTempoMode(WarpTempoMode::SourceTempo);
		sample->setSourceTempo(static_cast<float>(sourceTempo));
	}
	ref.clip->changeLength(TimePos(std::max<tick_t>(1, static_cast<tick_t>(std::lround(before * factor)))));
	ref.clip->setAutoResize(false);
	Engine::getSong()->setModified();
	QJsonObject result{{QStringLiteral("clip"), clipId(ref.id)}, {QStringLiteral("factor"), factor},
		{QStringLiteral("length"), ref.clip->length().getTicks()}};
	result.insert(QStringLiteral("__transaction"), transactionPayload({{QStringLiteral("length"), before}},
		QStringLiteral("clip.time_scale"), {{QStringLiteral("clip"), clipId(ref.id)}, {QStringLiteral("factor"), 1.0 / factor}}, true,
		QStringLiteral("Clip journal checkpoint: undo restores the clip's notes (or warp) and length")));
	return ControlResult::success(result);
}

} // namespace

void registerClipArrangeCommands(ControlRegistry& registry)
{
	ControlCommand repeat;
	repeat.id = QStringLiteral("clip.repeat");
	repeat.group = QStringLiteral("clip");
	repeat.verb = QStringLiteral("repeat");
	repeat.description = QStringLiteral("Put 'count' copies of a clip (1..64, default 1 - Duplicate) right after it, back "
		"to back on its track; answers the copies' ids. Reversible through the ProjectJournal (Track checkpoint).");
	repeat.argsSchema = objectSchema({{QStringLiteral("clip"), stringProperty()}, {QStringLiteral("count"), integerProperty(1, MaxRepeats)}},
		{QStringLiteral("clip")});
	repeat.resultSchema = objectSchema({{QStringLiteral("clip"), stringProperty()}, {QStringLiteral("copies"), arrayProperty()}});
	repeat.mutating = true;
	repeat.handler = repeatClip;
	registry.registerCommand(repeat);

	ControlCommand scale;
	scale.id = QStringLiteral("clip.time_scale");
	scale.group = QStringLiteral("clip");
	scale.verb = QStringLiteral("time_scale");
	scale.description = QStringLiteral("Scale a clip's content in time by 'factor' (0.125..8; 2 = twice as long): a MIDI "
		"clip's note positions and lengths scale with it; a sample clip leads with a source tempo of the project tempo "
		"times the factor, so it plays at 1/factor speed (pitch kept when its stretch mode keeps it). The clip's length "
		"scales too. Reversible through the ProjectJournal (Clip checkpoint).");
	scale.argsSchema = objectSchema({{QStringLiteral("clip"), stringProperty()}, {QStringLiteral("factor"), numberProperty()}},
		{QStringLiteral("clip"), QStringLiteral("factor")});
	scale.resultSchema = objectSchema({{QStringLiteral("clip"), stringProperty()}, {QStringLiteral("factor"), numberProperty()},
		{QStringLiteral("length"), integerProperty()}});
	scale.mutating = true;
	scale.handler = timeScale;
	registry.registerCommand(scale);
}

} // namespace lmms
