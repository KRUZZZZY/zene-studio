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
#include "RubberBandStretch.h"
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

ControlResult setTranspose(const QJsonObject& args)
{
	ClipRef ref;
	ControlResult error;
	if (!resolveClip(args.value(QStringLiteral("clip")).toString(), &ref, &error)) { return error; }
	const int semitones = args.value(QStringLiteral("semitones")).toInt();
	auto* sample = dynamic_cast<SampleClip*>(ref.clip);
	if (sample != nullptr && semitones != 0 && !RubberBandPool::available())
	{
		return ControlResult::failure(ControlErrorKind::Requires,
			QStringLiteral("transposing a sample clip keeps its timing through Rubber Band, which this build does not have"));
	}
	const ClipEdits before = ref.clip->clipEdits();
	ClipEdits after = before;
	after.transpose = semitones;
	// One checkpoint covers both: the edits and the warp element (the stretch mode) are the clip's.
	ref.clip->addJournalCheckPoint();
	ref.clip->setClipEdits(after);
	if (sample != nullptr && semitones != 0) { sample->setWarpStretchMode(WarpStretchMode::RubberBand); }
	QJsonObject result{{QStringLiteral("clip"), clipId(ref.id)}, {QStringLiteral("semitones"), semitones},
		{QStringLiteral("previous"), before.transpose}};
	result.insert(QStringLiteral("__transaction"), transactionPayload({{QStringLiteral("semitones"), before.transpose}},
		QStringLiteral("clip.set_transpose"), {{QStringLiteral("clip"), clipId(ref.id)}, {QStringLiteral("semitones"), before.transpose}},
		true, QStringLiteral("Clip journal checkpoint: undo restores the clip's previous edits and stretch mode")));
	return ControlResult::success(result);
}

//! 0.5.0 ED-029: a MIDI clip's content repeating every 'length' ticks across the clip (0 stops it).
ControlResult setLoop(const QJsonObject& args)
{
	ClipRef ref;
	ControlResult error;
	if (!resolveClip(args.value(QStringLiteral("clip")).toString(), &ref, &error)) { return error; }
	if (dynamic_cast<SampleClip*>(ref.clip) != nullptr) { return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("looped content belongs to MIDI clips")); }
	ClipEdits after = ref.clip->clipEdits();
	after.loopTicks = args.value(QStringLiteral("length")).toInt();
	ref.clip->addJournalCheckPoint();
	ref.clip->setClipEdits(after);
	return ControlResult::success({{QStringLiteral("clip"), clipId(ref.id)}, {QStringLiteral("length"), after.loopTicks}});
}

//! 0.5.0 ED-153: a sample clip's formants - kept while it is transposed, and shifted on their own.
ControlResult setFormant(const QJsonObject& args)
{
	ClipRef ref;
	ControlResult error;
	if (!resolveSample(args, &ref, &error)) { return error; }
	if (!RubberBandPool::available())
	{
		return ControlResult::failure(ControlErrorKind::Requires, QStringLiteral("formants are handled by Rubber Band, which this build does not have"));
	}
	ClipEdits after = ref.clip->clipEdits();
	after.keepFormants = args.value(QStringLiteral("keep")).toBool(after.keepFormants);
	after.formant = args.value(QStringLiteral("semitones")).toInt(after.formant);
	ref.clip->addJournalCheckPoint();
	ref.clip->setClipEdits(after);
	if (after.keepFormants || after.formant != 0) { static_cast<SampleClip*>(ref.clip)->setWarpStretchMode(WarpStretchMode::RubberBand); }
	return ControlResult::success({{QStringLiteral("clip"), clipId(ref.id)}, {QStringLiteral("keep"), after.keepFormants},
		{QStringLiteral("semitones"), after.formant}});
}

} // namespace

void registerClipEnvelopeCommands(ControlRegistry& registry)
{
	ControlCommand loop;
	loop.id = QStringLiteral("clip.set_loop");
	loop.group = QStringLiteral("clip");
	loop.verb = QStringLiteral("set_loop");
	loop.description = QStringLiteral("Loop a MIDI clip's content: its notes from the start to 'length' ticks repeat across the whole "
		"clip, however long it is drawn (0 stops the loop). Saved with the clip. Reversible through the ProjectJournal (Clip checkpoint).");
	loop.argsSchema = objectSchema({{QStringLiteral("clip"), stringProperty()}, {QStringLiteral("length"), integerProperty(0, 192 * 256)}},
		{QStringLiteral("clip"), QStringLiteral("length")});
	loop.resultSchema = objectProperty();
	loop.mutating = true;
	loop.handler = setLoop;
	registry.registerCommand(loop);

	ControlCommand formant;
	formant.id = QStringLiteral("clip.set_formant");
	formant.group = QStringLiteral("clip");
	formant.verb = QStringLiteral("set_formant");
	formant.description = QStringLiteral("A sample clip's formants (Rubber Band): 'keep' holds them where they were while the clip "
		"is transposed (a voice stays a voice), 'semitones' (-24..24) shifts them on their own. Both off is the plain pitch "
		"shift. The clip's stretch mode becomes Rubber Band; refused, typed, in a build without it. Saved with the clip. "
		"Reversible through the ProjectJournal (Clip checkpoint).");
	formant.argsSchema = objectSchema({{QStringLiteral("clip"), stringProperty()}, {QStringLiteral("keep"), booleanProperty()},
		{QStringLiteral("semitones"), integerProperty(-24, 24)}}, {QStringLiteral("clip")});
	formant.resultSchema = objectProperty();
	formant.mutating = true;
	formant.handler = setFormant;
	registry.registerCommand(formant);

	ControlCommand transpose;
	transpose.id = QStringLiteral("clip.set_transpose");
	transpose.group = QStringLiteral("clip");
	transpose.verb = QStringLiteral("set_transpose");
	transpose.description = QStringLiteral("Transpose a clip by 'semitones' (-48..+48, 0 resets), non-destructively: a "
		"MIDI clip's notes play transposed and keep their keys; a sample clip is pitch-shifted with its timing kept "
		"(its stretch mode becomes Rubber Band; refused, typed, in a build without it). Saved with the clip. "
		"Reversible through the ProjectJournal (Clip checkpoint).");
	transpose.argsSchema = objectSchema({{QStringLiteral("clip"), stringProperty()}, {QStringLiteral("semitones"), integerProperty(-48, 48)}},
		{QStringLiteral("clip"), QStringLiteral("semitones")});
	transpose.resultSchema = objectSchema({{QStringLiteral("clip"), stringProperty()}, {QStringLiteral("semitones"), integerProperty()},
		{QStringLiteral("previous"), integerProperty()}});
	transpose.mutating = true;
	transpose.handler = setTranspose;
	registry.registerCommand(transpose);

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
