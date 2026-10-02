/*
 * ControlCommandsClipEnvelope.cpp - clip.gain_envelope_*: a sample clip's gain envelope
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

/*! ED-042 (0.5.0 L04): a sample clip's gain envelope - up to 32 breakpoints (tick from the clip's
 *  start, gain in dB), applied by the play handle at block rate on top of the clip's gain and
 *  fades, saved with the clip. A replace is one Clip journal checkpoint. */

#include <QJsonArray>

#include "ClipEdits.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlVocabulary.h"
#include "SampleClip.h"

namespace lmms
{

using namespace control;

namespace
{

constexpr double MinDb = -60.0;
constexpr double MaxDb = 24.0;

QJsonArray pointsJson(const ClipEdits& edits)
{
	QJsonArray out;
	for (int i = 0; i < edits.envelopeCount; ++i)
	{
		out.append(QJsonObject{{QStringLiteral("tick"), edits.envelope[i].tick},
			{QStringLiteral("gain_db"), static_cast<double>(edits.envelope[i].gainDb)}});
	}
	return out;
}

bool resolveSample(const QJsonObject& args, ClipRef* ref, ControlResult* error)
{
	if (!resolveClip(args.value(QStringLiteral("clip")).toString(), ref, error)) { return false; }
	if (dynamic_cast<SampleClip*>(ref->clip) != nullptr) { return true; }
	*error = ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("a gain envelope applies to sample clips"));
	return false;
}

//! @a points as an envelope; false (with @a error) on a point outside the clip or the dB range.
bool readPoints(const QJsonArray& points, int clipTicks, ClipEdits* edits, ControlResult* error)
{
	if (points.size() > MaxGainPoints)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("at most %1 points").arg(MaxGainPoints));
		return false;
	}
	edits->envelopeCount = 0;
	for (const QJsonValue& value : points)
	{
		const QJsonObject point = value.toObject();
		const int tick = point.value(QStringLiteral("tick")).toInt();
		const double db = point.value(QStringLiteral("gain_db")).toDouble();
		if (tick < 0 || tick > clipTicks || db < MinDb || db > MaxDb)
		{
			*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("a point needs 0 <= tick <= %1 and %2 <= gain_db <= %3").arg(clipTicks).arg(MinDb).arg(MaxDb));
			return false;
		}
		edits->envelope[edits->envelopeCount++] = {tick, static_cast<float>(db)};
	}
	std::sort(edits->envelope.begin(), edits->envelope.begin() + edits->envelopeCount,
		[](const GainPoint& a, const GainPoint& b) { return a.tick < b.tick; });
	return true;
}

ControlResult setEnvelope(const QJsonObject& args)
{
	ClipRef ref;
	ControlResult error;
	if (!resolveSample(args, &ref, &error)) { return error; }
	const ClipEdits before = ref.clip->clipEdits();
	ClipEdits after = before;
	if (!readPoints(args.value(QStringLiteral("points")).toArray(), ref.clip->length().getTicks(), &after, &error)) { return error; }
	ref.clip->addJournalCheckPoint();
	ref.clip->setClipEdits(after);
	QJsonObject result{{QStringLiteral("clip"), clipId(ref.id)}, {QStringLiteral("points"), pointsJson(after)}};
	result.insert(QStringLiteral("__transaction"), transactionPayload({{QStringLiteral("points"), pointsJson(before)}},
		QStringLiteral("clip.gain_envelope_set"), {{QStringLiteral("clip"), clipId(ref.id)}, {QStringLiteral("points"), pointsJson(before)}},
		true, QStringLiteral("Clip journal checkpoint: undo restores the clip's previous edits")));
	return ControlResult::success(result);
}

ControlResult getEnvelope(const QJsonObject& args)
{
	ClipRef ref;
	ControlResult error;
	if (!resolveSample(args, &ref, &error)) { return error; }
	return ControlResult::success({{QStringLiteral("clip"), clipId(ref.id)}, {QStringLiteral("points"), pointsJson(ref.clip->clipEdits())},
		{QStringLiteral("max_points"), MaxGainPoints}});
}

} // namespace

void registerClipEnvelopeCommands(ControlRegistry& registry)
{
	ControlCommand set;
	set.id = QStringLiteral("clip.gain_envelope_set");
	set.group = QStringLiteral("clip");
	set.verb = QStringLiteral("gain_envelope_set");
	set.description = QStringLiteral("Replace a sample clip's gain envelope with 'points' ([{tick, gain_db}], tick from the "
		"clip's start, -60..+24 dB, at most 32; [] clears it). Between points the gain moves linearly in dB, before the "
		"first and after the last it holds; it multiplies the clip's own gain and fades. Saved with the clip. Reversible "
		"through the ProjectJournal (Clip checkpoint).");
	set.argsSchema = objectSchema({{QStringLiteral("clip"), stringProperty()}, {QStringLiteral("points"), arrayProperty()}},
		{QStringLiteral("clip"), QStringLiteral("points")});
	set.resultSchema = objectSchema({{QStringLiteral("clip"), stringProperty()}, {QStringLiteral("points"), arrayProperty()}});
	set.mutating = true;
	set.handler = setEnvelope;
	registry.registerCommand(set);

	ControlCommand get;
	get.id = QStringLiteral("clip.gain_envelope_get");
	get.group = QStringLiteral("clip");
	get.verb = QStringLiteral("gain_envelope_get");
	get.description = QStringLiteral("A sample clip's gain envelope points.");
	get.argsSchema = objectSchema({{QStringLiteral("clip"), stringProperty()}}, {QStringLiteral("clip")});
	get.resultSchema = objectSchema({{QStringLiteral("clip"), stringProperty()}, {QStringLiteral("points"), arrayProperty()},
		{QStringLiteral("max_points"), integerProperty()}});
	get.handler = getEnvelope;
	registry.registerCommand(get);
}

} // namespace lmms
