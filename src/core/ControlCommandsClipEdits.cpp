/*
 * ControlCommandsClipEdits.cpp - the clip fades / crossfade / clip-gain
 *                                 commands, added to the existing clip.* group.
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

// The clip.* group already exists (src/core/ControlCommandsClip.cpp); these
// three commands JOIN it rather than starting a second group for it, which is
// why they live in their own translation unit with their own register call and
// still declare group="clip".
//
// SCOPE, stated because it is a real limit rather than an accident: the engine
// applies fades and clip gain to AUDIO clips (`SampleClip`), through the play
// handle. A MIDI clip can carry the fields - they live on the base `Clip`, per
// docs/CLIP-CAPTURE-DESIGN.md §2.2 - but this release has nothing that applies
// them to note data, so these commands REFUSE a MIDI clip with a typed error
// instead of writing state that would silently do nothing. The same sentence is
// in docs/KNOWN-LIMITATIONS.md.

#include <algorithm>

#include <QJsonObject>

#include "Clip.h"
#include "ClipEdits.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "SampleClip.h"
#include "Song.h"
#include "Track.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

const QString ClauseClipJournalled = QStringLiteral("ProjectJournal (Clip checkpoint: "
	"Clip::restoreState re-loads the clip's serialized state, which carries the fades "
	"and the gain)");
const QString ClauseTrackJournalled = QStringLiteral("ProjectJournal (Track checkpoint: "
	"both clips of a crossfade are required to be on ONE track, so the track's own "
	"serialized clip list is the inverse)");

//! The widest clip gain this surface accepts, in dB. Beyond +24 dB a sample clip
//! is a distortion generator and below -60 dB it is inaudible, so both ends are
//! refusals rather than accepted nonsense (SPEC A11: typed, never silently clamped).
constexpr double kMinGainDb = -60.0;
constexpr double kMaxGainDb = 24.0;

/*! The block a caller reads back after a fade or gain edit: the same keys
 *  control::clipState() reports for every clip, so the command and the
 *  inspector cannot drift. \p edits is passed in rather than read from the clip
 *  so a handler can report a before-state without having written one. */
QJsonObject editsState(const ClipRef& ref, const ClipEdits& edits)
{
	QJsonObject out;
	out.insert(QStringLiteral("clip"), clipId(ref.ordinal));
	out.insert(QStringLiteral("gain_db"), static_cast<double>(gainLinearToDb(edits.gain)));
	out.insert(QStringLiteral("fade_in"), edits.fadeInTicks);
	out.insert(QStringLiteral("fade_out"), edits.fadeOutTicks);
	out.insert(QStringLiteral("fade_in_shape"), fadeShapeName(edits.fadeInShape));
	out.insert(QStringLiteral("fade_out_shape"), fadeShapeName(edits.fadeOutShape));
	return out;
}

//! The two keys every mutating handler here needs to name its clip again.
QJsonObject clipArg(const ClipRef& ref)
{
	return QJsonObject{{QStringLiteral("clip"), clipId(ref.ordinal)}};
}

/*! Resolves \p id to a clip whose edits this release can actually apply.
 *
 *  A MIDI clip is refused rather than accepted: the model can hold a fade, but
 *  nothing in this tree renders one for note data, and a command that reported
 *  success while changing no audio would be a lie the socket cannot catch.
 */
bool resolveAudioClip(const QString& id, ClipRef* ref, ControlResult* error)
{
	if (!resolveClip(id, ref, error)) { return false; }
	if (dynamic_cast<SampleClip*>(ref->clip) == nullptr)
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("fades and clip gain are applied to audio clips only in this "
				"release; %1 is a %2 clip (docs/KNOWN-LIMITATIONS.md)")
				.arg(clipId(ref->ordinal), ref->clip->nodeName()));
		return false;
	}
	return true;
}

//! Reads the optional shape argument \p key, leaving \p shape alone when absent.
bool shapeArg(const QJsonObject& args, const QString& key, FadeShape* shape, ControlResult* error)
{
	if (!args.contains(key)) { return true; }
	const QString name = args.value(key).toString();
	if (fadeShapeFromName(name, shape)) { return true; }
	*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
		QStringLiteral("'%1' is not a fade shape; use linear, exponential or equal_power").arg(name));
	return false;
}

/*! Rejects a fade pair that cannot both fit inside the clip it ramps over. The
 *  rule is the rejected-edit rule and not a clamp (invariant I4's shape): a pair
 *  that overlaps would make the clip's own envelope ambiguous, and the caller
 *  asked for something else. */
bool checkFadesFit(const ClipRef& ref, const ClipEdits& edits, ControlResult* error)
{
	const int length = ref.clip->length().getTicks();
	if (edits.fadeInTicks + edits.fadeOutTicks <= length) { return true; }
	*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
		QStringLiteral("fade_in (%1) + fade_out (%2) exceed the clip's length (%3 ticks): "
			"the two ramps would overlap inside one clip. Shorten them, or use "
			"clip.crossfade to ramp two neighbouring clips into each other.")
			.arg(edits.fadeInTicks).arg(edits.fadeOutTicks).arg(length));
	return false;
}

void registerClipSetGain(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("clip.set_gain");
	cmd.group = QStringLiteral("clip");
	cmd.verb = QStringLiteral("set_gain");
	cmd.description = QStringLiteral("Set a clip's gain in dB (-60..+24, 0 is unity). The gain "
		"multiplies every frame the clip renders, on top of its fades; a clip's own gain is "
		"independent of any other clip's. Reversible through the ProjectJournal (Clip "
		"checkpoint).");
	cmd.argsSchema = control::objectSchema({
		{QStringLiteral("clip"), control::stringProperty()},
		{QStringLiteral("gain_db"), control::numberProperty()},
	}, {QStringLiteral("clip"), QStringLiteral("gain_db")});
	cmd.resultSchema = control::objectSchema({
		{QStringLiteral("clip"), control::stringProperty()},
		{QStringLiteral("gain_db"), control::numberProperty()},
		{QStringLiteral("fade_in"), control::integerProperty(0, MaxSongLength)},
		{QStringLiteral("fade_out"), control::integerProperty(0, MaxSongLength)},
		{QStringLiteral("fade_in_shape"), control::stringProperty()},
		{QStringLiteral("fade_out_shape"), control::stringProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ControlResult error;
		ClipRef ref;
		if (!resolveAudioClip(args.value(QStringLiteral("clip")).toString(), &ref, &error))
		{
			return error;
		}
		const double db = args.value(QStringLiteral("gain_db")).toDouble();
		if (db < kMinGainDb || db > kMaxGainDb)
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("gain_db %1 is outside %2..%3 dB")
					.arg(db).arg(kMinGainDb).arg(kMaxGainDb));
		}

		const ClipEdits before = ref.clip->clipEdits();
		ClipEdits after = before;
		after.gain = gainDbToLinear(static_cast<float>(db));

		ref.clip->addJournalCheckPoint();
		ref.clip->setClipEdits(after);

		QJsonObject result = editsState(ref, after);
		QJsonObject inverseArgs = clipArg(ref);
		inverseArgs.insert(QStringLiteral("gain_db"),
			static_cast<double>(gainLinearToDb(before.gain)));
		result.insert(QStringLiteral("__transaction"),
			control::transactionPayload(editsState(ref, before),
				QStringLiteral("clip.set_gain"), inverseArgs, true, ClauseClipJournalled));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerClipSetFade(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("clip.set_fade");
	cmd.group = QStringLiteral("clip");
	cmd.verb = QStringLiteral("set_fade");
	cmd.description = QStringLiteral("Set a clip's fade-in and/or fade-out in ticks, with a shape "
		"each (linear, exponential, equal_power). An omitted argument keeps its current value; "
		"fade_in + fade_out must fit inside the clip. Reversible through the ProjectJournal "
		"(Clip checkpoint).");
	cmd.argsSchema = control::objectSchema({
		{QStringLiteral("clip"), control::stringProperty()},
		{QStringLiteral("fade_in"), control::integerProperty(0, MaxSongLength)},
		{QStringLiteral("fade_out"), control::integerProperty(0, MaxSongLength)},
		{QStringLiteral("fade_in_shape"), control::stringProperty()},
		{QStringLiteral("fade_out_shape"), control::stringProperty()},
	}, {QStringLiteral("clip")});
	cmd.resultSchema = control::objectSchema({
		{QStringLiteral("clip"), control::stringProperty()},
		{QStringLiteral("gain_db"), control::numberProperty()},
		{QStringLiteral("fade_in"), control::integerProperty(0, MaxSongLength)},
		{QStringLiteral("fade_out"), control::integerProperty(0, MaxSongLength)},
		{QStringLiteral("fade_in_shape"), control::stringProperty()},
		{QStringLiteral("fade_out_shape"), control::stringProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ControlResult error;
		ClipRef ref;
		if (!resolveAudioClip(args.value(QStringLiteral("clip")).toString(), &ref, &error))
		{
			return error;
		}

		const ClipEdits before = ref.clip->clipEdits();
		ClipEdits after = before;
		if (args.contains(QStringLiteral("fade_in")))
		{
			after.fadeInTicks = static_cast<int>(args.value(QStringLiteral("fade_in")).toDouble());
		}
		if (args.contains(QStringLiteral("fade_out")))
		{
			after.fadeOutTicks = static_cast<int>(args.value(QStringLiteral("fade_out")).toDouble());
		}
		if (!shapeArg(args, QStringLiteral("fade_in_shape"), &after.fadeInShape, &error)
			|| !shapeArg(args, QStringLiteral("fade_out_shape"), &after.fadeOutShape, &error))
		{
			return error;
		}
		if (!checkFadesFit(ref, after, &error)) { return error; }

		ref.clip->addJournalCheckPoint();
		ref.clip->setClipEdits(after);

		QJsonObject result = editsState(ref, after);
		QJsonObject inverseArgs = clipArg(ref);
		inverseArgs.insert(QStringLiteral("fade_in"), before.fadeInTicks);
		inverseArgs.insert(QStringLiteral("fade_out"), before.fadeOutTicks);
		inverseArgs.insert(QStringLiteral("fade_in_shape"), fadeShapeName(before.fadeInShape));
		inverseArgs.insert(QStringLiteral("fade_out_shape"), fadeShapeName(before.fadeOutShape));
		result.insert(QStringLiteral("__transaction"),
			control::transactionPayload(editsState(ref, before), QStringLiteral("clip.set_fade"),
				inverseArgs, true, ClauseClipJournalled));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerClipCrossfade(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("clip.crossfade");
	cmd.group = QStringLiteral("clip");
	cmd.verb = QStringLiteral("crossfade");
	cmd.description = QStringLiteral("Ramp two overlapping clips on one track into each other: the "
		"outgoing clip gets a fade-out and the incoming clip a fade-in, both exactly as long as "
		"their overlap, with the given shape (default equal_power, which sums the pair to unity "
		"POWER). Reversible through the ProjectJournal (Track checkpoint).");
	cmd.argsSchema = control::objectSchema({
		{QStringLiteral("out"), control::stringProperty()},
		{QStringLiteral("in"), control::stringProperty()},
		{QStringLiteral("shape"), control::stringProperty()},
	}, {QStringLiteral("out"), QStringLiteral("in")});
	cmd.resultSchema = control::objectSchema({
		{QStringLiteral("out"), control::stringProperty()},
		{QStringLiteral("in"), control::stringProperty()},
		{QStringLiteral("overlap"), control::integerProperty(0, MaxSongLength)},
		{QStringLiteral("shape"), control::stringProperty()},
		{QStringLiteral("out_fade_out"), control::integerProperty(0, MaxSongLength)},
		{QStringLiteral("in_fade_in"), control::integerProperty(0, MaxSongLength)},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ControlResult error;
		ClipRef outRef;
		ClipRef inRef;
		if (!resolveAudioClip(args.value(QStringLiteral("out")).toString(), &outRef, &error))
		{
			return error;
		}
		if (!resolveAudioClip(args.value(QStringLiteral("in")).toString(), &inRef, &error))
		{
			return error;
		}
		if (outRef.clip == inRef.clip)
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("'out' and 'in' are the same clip (%1)").arg(clipId(outRef.ordinal)));
		}
		if (outRef.track != inRef.track)
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("'out' (%1) and 'in' (%2) are on different tracks; a crossfade "
					"pairs two clips of ONE track's lane")
					.arg(clipId(outRef.ordinal), clipId(inRef.ordinal)));
		}

		const tick_t outStart = outRef.clip->startPosition().getTicks();
		const tick_t outEnd = outRef.clip->endPosition().getTicks();
		const tick_t inStart = inRef.clip->startPosition().getTicks();
		const tick_t inEnd = inRef.clip->endPosition().getTicks();
		const tick_t overlapStart = std::max(outStart, inStart);
		const tick_t overlapEnd = std::min(outEnd, inEnd);
		if (overlapEnd <= overlapStart)
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("clips %1 and %2 do not overlap (%3..%4 vs %5..%6), so there is "
					"no range to crossfade")
					.arg(clipId(outRef.ordinal), clipId(inRef.ordinal))
					.arg(outStart).arg(outEnd).arg(inStart).arg(inEnd));
		}
		const int overlap = static_cast<int>(overlapEnd - overlapStart);

		FadeShape shape = FadeShape::EqualPower;
		if (!shapeArg(args, QStringLiteral("shape"), &shape, &error)) { return error; }

		const ClipEdits outBefore = outRef.clip->clipEdits();
		const ClipEdits inBefore = inRef.clip->clipEdits();
		ClipEdits outAfter = outBefore;
		ClipEdits inAfter = inBefore;
		outAfter.fadeOutTicks = overlap;
		outAfter.fadeOutShape = shape;
		inAfter.fadeInTicks = overlap;
		inAfter.fadeInShape = shape;
		// Each ramp is bounded by ITS OWN clip, and a clip whose OTHER fade no
		// longer fits is a refusal naming the fix rather than a silent overwrite
		// of an edit the caller made deliberately.
		if (!checkFadesFit(outRef, outAfter, &error)) { return error; }
		if (!checkFadesFit(inRef, inAfter, &error)) { return error; }

		// The overlap is what the two clips share, so the outgoing clip's ramp is
		// exactly the incoming clip's ramp: the pair meets over one range, each
		// starting at unity and ending at silence (or the reverse), which is the
		// only arrangement in which a crossfade cannot double-count gain.
		Track* track = outRef.track;
		track->addJournalCheckPoint();
		outRef.clip->setClipEdits(outAfter);
		inRef.clip->setClipEdits(inAfter);

		QJsonObject before;
		before.insert(QStringLiteral("out"), clipId(outRef.ordinal));
		before.insert(QStringLiteral("in"), clipId(inRef.ordinal));
		before.insert(QStringLiteral("overlap"), overlap);
		before.insert(QStringLiteral("out_fade_out"), outBefore.fadeOutTicks);
		before.insert(QStringLiteral("out_fade_out_shape"), fadeShapeName(outBefore.fadeOutShape));
		before.insert(QStringLiteral("in_fade_in"), inBefore.fadeInTicks);
		before.insert(QStringLiteral("in_fade_in_shape"), fadeShapeName(inBefore.fadeInShape));

		QJsonObject result;
		result.insert(QStringLiteral("out"), clipId(outRef.ordinal));
		result.insert(QStringLiteral("in"), clipId(inRef.ordinal));
		result.insert(QStringLiteral("overlap"), overlap);
		result.insert(QStringLiteral("shape"), fadeShapeName(shape));
		result.insert(QStringLiteral("out_fade_out"), outAfter.fadeOutTicks);
		result.insert(QStringLiteral("in_fade_in"), inAfter.fadeInTicks);
		QJsonObject inverseArgs;
		inverseArgs.insert(QStringLiteral("out"), clipId(outRef.ordinal));
		inverseArgs.insert(QStringLiteral("in"), clipId(inRef.ordinal));
		inverseArgs.insert(QStringLiteral("out_fade_out"), outBefore.fadeOutTicks);
		inverseArgs.insert(QStringLiteral("out_fade_out_shape"),
			fadeShapeName(outBefore.fadeOutShape));
		inverseArgs.insert(QStringLiteral("in_fade_in"), inBefore.fadeInTicks);
		inverseArgs.insert(QStringLiteral("in_fade_in_shape"), fadeShapeName(inBefore.fadeInShape));
		// The automatic inverse is the TRACK checkpoint (both clips live on it,
		// which the guard above enforces). There is no single command that
		// restores two clips' fades, so the op is named as the manual one - the
		// same shape clip.split uses - while `mechanism` names what control.undo
		// actually replays.
		result.insert(QStringLiteral("__transaction"),
			control::transactionPayload(before,
				QStringLiteral("UNIMPLEMENTED: re-open both clips' previous fades"),
				inverseArgs, true, ClauseTrackJournalled));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

} // namespace

void registerClipEditsCommands(ControlRegistry& registry)
{
	registerClipSetGain(registry);
	registerClipSetFade(registry);
	registerClipCrossfade(registry);
}

} // namespace lmms
