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
#include "ContainedCrossfade.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "Engine.h"
#include "SampleClip.h"
#include "Song.h"
#include "Track.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

void registerClipSetRecord(ControlRegistry& registry);

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
	out.insert(QStringLiteral("clip"), clipId(ref.id));
	out.insert(QStringLiteral("gain_db"), static_cast<double>(gainLinearToDb(edits.gain)));
	out.insert(QStringLiteral("fade_in"), edits.fadeInTicks);
	out.insert(QStringLiteral("fade_out"), edits.fadeOutTicks);
	out.insert(QStringLiteral("fade_in_shape"), fadeShapeName(edits.fadeInShape));
	out.insert(QStringLiteral("fade_out_shape"), fadeShapeName(edits.fadeOutShape));
	// Q050-125: the clip's polarity travels with every edits read-back, so a caller sees it without a
	// second call and a before-state carries it for the inverse.
	out.insert(QStringLiteral("inverted"), edits.hasPolarity());
	if (edits.fadeInOffsetTicks != 0) { out.insert(QStringLiteral("fade_in_offset"), edits.fadeInOffsetTicks); }
	if (edits.fadeOutOffsetTicks != 0) { out.insert(QStringLiteral("fade_out_offset"), edits.fadeOutOffsetTicks); }
	return out;
}

//! The two keys every mutating handler here needs to name its clip again.
QJsonObject clipArg(const ClipRef& ref)
{
	return QJsonObject{{QStringLiteral("clip"), clipId(ref.id)}};
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
				.arg(clipId(ref->id), ref->clip->nodeName()));
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
		QStringLiteral("'%1' is not a fade shape; use linear, exponential, logarithmic, s_curve or equal_power").arg(name));
	return false;
}

/*! 0.5.0 L05 (ED-176): the crossfade's shape ('shape', equal power when left out) and its two sides -
 *  'out_length' / 'in_length' (the overlap when left out) and 'out_shape' / 'in_shape' (the shape). */
bool crossfadeSides(const QJsonObject& args, int overlap, FadeShape* shape, ClipEdits* out, ClipEdits* in, ControlResult* error)
{
	if (!shapeArg(args, QStringLiteral("shape"), shape, error)) { return false; }
	out->fadeOutShape = *shape;
	in->fadeInShape = *shape;
	out->fadeOutTicks = args.value(QStringLiteral("out_length")).toInt(overlap);
	in->fadeInTicks = args.value(QStringLiteral("in_length")).toInt(overlap);
	out->fadeInOffsetTicks = 0;
	out->fadeOutOffsetTicks = 0;
	out->fadeOffsetGap = false;
	in->fadeOutOffsetTicks = 0;
	in->fadeInOffsetTicks = 0;
	in->fadeOffsetGap = false;
	if (out->fadeOutTicks < 1 || in->fadeInTicks < 1)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("a crossfade side is at least one tick"));
		return false;
	}
	return shapeArg(args, QStringLiteral("out_shape"), &out->fadeOutShape, error) && shapeArg(args, QStringLiteral("in_shape"), &in->fadeInShape, error);
}

/*! Explicit container mode uses the contained clip's authored fade-in and fade-out lengths for the entry and exit.
 *  When supplied, out_length/in_length override the one shared edge named by the clip order; both must agree.
 *  The ramps that go down take out_shape, the ones that go up in_shape. */
bool containedSides(const QJsonObject& args, const ClipRef& outRef, const ClipRef& inRef, FadeShape* shape,
	ClipEdits* out, ClipEdits* in, ControlResult* error)
{
	const Clip* container = nullptr;
	const Clip* contained = nullptr;
	containedcrossfade::containment(outRef.clip, inRef.clip, &container, &contained);
	const bool containerIsOut = container == outRef.clip;
	const int outLength = args.value(QStringLiteral("out_length")).toInt(0);
	const int inLength = args.value(QStringLiteral("in_length")).toInt(0);
	if (outLength > 0 && inLength > 0 && outLength != inLength)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("a crossfade between a clip and "
			"one inside it has one shared edge length: 'out_length' (%1) and 'in_length' (%2) differ").arg(outLength).arg(inLength));
		return false;
	}
	FadeShape down = FadeShape::EqualPower;
	FadeShape up = FadeShape::EqualPower;
	if (!shapeArg(args, QStringLiteral("shape"), shape, error)) { return false; }
	down = up = *shape;
	if (!shapeArg(args, QStringLiteral("out_shape"), &down, error) || !shapeArg(args, QStringLiteral("in_shape"), &up, error)) { return false; }
	int entry = 0;
	int exit = 0;
	const QString why = containedcrossfade::ramps(*container, *contained, std::max(outLength, inLength), containerIsOut, &entry, &exit);
	if (!why.isEmpty())
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs, why);
		return false;
	}
	ClipEdits& containerEdits = containerIsOut ? *out : *in;
	ClipEdits& containedEdits = containerIsOut ? *in : *out;
	containedcrossfade::apply(*container, *contained, entry, exit, down, up, &containerEdits, &containedEdits);
	return true;
}

//! A crossfade defaults to edge fades; only the explicit container mode uses offset fades.
bool crossfadeSidesFor(const QJsonObject& args, const ClipRef& outRef, const ClipRef& inRef, int overlap, FadeShape* shape,
	ClipEdits* out, ClipEdits* in, ControlResult* error)
{
	const QString mode = args.contains(QStringLiteral("mode"))
		? args.value(QStringLiteral("mode")).toString() : QStringLiteral("clamp");
	if (mode != QStringLiteral("clamp") && mode != QStringLiteral("container"))
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("crossfade mode must be 'clamp' or 'container'"));
		return false;
	}
	const Clip* container = nullptr;
	const Clip* contained = nullptr;
	if (mode == QStringLiteral("container"))
	{
		if (!containedcrossfade::containment(outRef.clip, inRef.clip, &container, &contained))
		{
			*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("mode 'container' requires one clip to lie wholly inside the other"));
			return false;
		}
		return containedSides(args, outRef, inRef, shape, out, in, error);
	}
	if (!crossfadeSides(args, overlap, shape, out, in, error)) { return false; }
	return true;
}

//! The offsets a crossfade wrote, in the reply, only when there are some.
void insertOffsets(QJsonObject* result, const ClipEdits& out, const ClipEdits& in)
{
	if (out.fadeOutOffsetTicks != 0) { result->insert(QStringLiteral("out_fade_out_offset"), out.fadeOutOffsetTicks); }
	if (in.fadeInOffsetTicks != 0) { result->insert(QStringLiteral("in_fade_in_offset"), in.fadeInOffsetTicks); }
}

/*! An offset fade is a pair (withValidOffsetFades): a fade-out that ends early needs a fade-in that starts where it
 *  ends, or the gain would step there. Refused when \p edits has a lone one or a mismatched one. */
bool offsetFadesArePaired(const ClipRef& ref, const ClipEdits& before, const ClipEdits& edits, ControlResult* error)
{
	// Offsets the caller did not touch are not its to answer for (a clip resized after a crossfade is repaired at render).
	if (edits.fadeInOffsetTicks == before.fadeInOffsetTicks && edits.fadeOutOffsetTicks == before.fadeOutOffsetTicks) { return true; }
	if (withValidOffsetFades(edits, ref.clip->length().getTicks()) == edits) { return true; }
	*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
		QStringLiteral("an offset fade is a pair: set fade_in_offset and fade_out_offset together, both ramps above 0, with "
			"the fade-in starting where the fade-out ends (fade_in_offset = the clip's length %1 - fade_out_offset); a fade-out "
			"that ends early with nothing to bring the clip back would step the gain. clip.crossfade makes the pair for a clip "
			"inside another one, and both offsets 0 clear it.").arg(ref.clip->length().getTicks()));
	return false;
}

/*! Rejects a fade pair that cannot both fit inside the clip it ramps over. The
 *  rule is the rejected-edit rule and not a clamp (invariant I4's shape): a pair
 *  that overlaps would make the clip's own envelope ambiguous, and the caller
 *  asked for something else. */
bool checkFadesFit(const ClipRef& ref, const ClipEdits& edits, ControlResult* error)
{
	const int length = ref.clip->length().getTicks();
	// With offsets the two ramps sit anywhere inside the clip, in either order (a container's V is out, then in):
	// each must fit on its own. Without, the in-ramp comes first and the pair must fit end to end, as ever.
	const bool offset = edits.fadeInOffsetTicks != 0 || edits.fadeOutOffsetTicks != 0;
	const int used = offset ? std::max(edits.fadeInOffsetTicks + edits.fadeInTicks, edits.fadeOutOffsetTicks + edits.fadeOutTicks)
		: edits.fadeInTicks + edits.fadeOutTicks;
	if (used <= length) { return true; }
	*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
		QStringLiteral("fade_in (%1) + fade_out (%2) (offsets %4, %5) do not fit the clip's length (%3 ticks): "
			"the two ramps would overlap inside one clip. Shorten them, or use "
			"clip.crossfade to ramp two neighbouring clips into each other.")
			.arg(edits.fadeInTicks).arg(edits.fadeOutTicks).arg(length)
			.arg(edits.fadeInOffsetTicks).arg(edits.fadeOutOffsetTicks));
	return false;
}

void assignTicks(const QJsonObject& args, const QString& key, int* value)
{
	if (args.contains(key)) { *value = static_cast<int>(args.value(key).toDouble()); }
}

bool applyFadeArguments(const QJsonObject& args, ClipEdits* after, ControlResult* error)
{
	assignTicks(args, QStringLiteral("fade_in"), &after->fadeInTicks);
	assignTicks(args, QStringLiteral("fade_out"), &after->fadeOutTicks);
	assignTicks(args, QStringLiteral("fade_in_offset"), &after->fadeInOffsetTicks);
	assignTicks(args, QStringLiteral("fade_out_offset"), &after->fadeOutOffsetTicks);
	if (after->fadeInOffsetTicks == 0 && after->fadeOutOffsetTicks == 0) { after->fadeOffsetGap = false; }
	return shapeArg(args, QStringLiteral("fade_in_shape"), &after->fadeInShape, error)
		&& shapeArg(args, QStringLiteral("fade_out_shape"), &after->fadeOutShape, error);
}

bool validateFadeArguments(const ClipRef& ref, const ClipEdits& before, const ClipEdits& after,
	ControlResult* error)
{
	return offsetFadesArePaired(ref, before, after, error) && checkFadesFit(ref, after, error);
}

ControlResult setClipFade(const QJsonObject& args)
{
	ControlResult error;
	ClipRef ref;
	if (!resolveAudioClip(args.value(QStringLiteral("clip")).toString(), &ref, &error)) { return error; }
	const ClipEdits before = ref.clip->clipEdits();
	ClipEdits after = before;
	if (!applyFadeArguments(args, &after, &error)) { return error; }
	if (!validateFadeArguments(ref, before, after, &error)) { return error; }

	ref.clip->addJournalCheckPoint();
	ref.clip->setClipEdits(after);
	QJsonObject result = editsState(ref, after);
	QJsonObject inverseArgs = clipArg(ref);
	inverseArgs.insert(QStringLiteral("fade_in"), before.fadeInTicks);
	inverseArgs.insert(QStringLiteral("fade_out"), before.fadeOutTicks);
	inverseArgs.insert(QStringLiteral("fade_in_shape"), fadeShapeName(before.fadeInShape));
	inverseArgs.insert(QStringLiteral("fade_out_shape"), fadeShapeName(before.fadeOutShape));
	inverseArgs.insert(QStringLiteral("fade_in_offset"), before.fadeInOffsetTicks);
	inverseArgs.insert(QStringLiteral("fade_out_offset"), before.fadeOutOffsetTicks);
	result.insert(QStringLiteral("__transaction"), control::transactionPayload(
		editsState(ref, before), QStringLiteral("clip.set_fade"), inverseArgs, true, ClauseClipJournalled));
	return ControlResult::success(result);
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
		{QStringLiteral("inverted"), control::booleanProperty()},
		{QStringLiteral("fade_in"), control::integerProperty(0, MaxSongLength)},
		{QStringLiteral("fade_out"), control::integerProperty(0, MaxSongLength)},
		{QStringLiteral("fade_in_shape"), control::stringProperty()},
		{QStringLiteral("fade_out_shape"), control::stringProperty()},
		{QStringLiteral("fade_in_offset"), control::integerProperty(0, MaxSongLength)},
		{QStringLiteral("fade_out_offset"), control::integerProperty(0, MaxSongLength)},
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
		if (after == before) { return ControlResult::success(editsState(ref, after)); }

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

void registerClipSetPolarity(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("clip.set_polarity");
	cmd.group = QStringLiteral("clip");
	cmd.verb = QStringLiteral("set_polarity");
	cmd.description = QStringLiteral("Set whether a sample clip renders with its polarity inverted: every frame it "
		"plays changes sign, and its drawn waveform mirrors around zero. The SOURCE sample is never modified - the "
		"inversion is a non-destructive stage on top of the clip's gain, like its fades - and an old project loads "
		"with polarity off. Reversible through the ProjectJournal (Clip checkpoint).");
	cmd.argsSchema = control::objectSchema({
		{QStringLiteral("clip"), control::stringProperty()},
		{QStringLiteral("inverted"), control::booleanProperty()},
	}, {QStringLiteral("clip"), QStringLiteral("inverted")});
	cmd.resultSchema = control::objectSchema({
		{QStringLiteral("clip"), control::stringProperty()},
		{QStringLiteral("inverted"), control::booleanProperty()},
		{QStringLiteral("gain_db"), control::numberProperty()},
		{QStringLiteral("fade_in"), control::integerProperty(0, MaxSongLength)},
		{QStringLiteral("fade_in_offset"), control::integerProperty(0, MaxSongLength)},
		{QStringLiteral("fade_out"), control::integerProperty(0, MaxSongLength)},
		{QStringLiteral("fade_out_offset"), control::integerProperty(0, MaxSongLength)},
		{QStringLiteral("fade_in_shape"), control::stringProperty()},
		{QStringLiteral("fade_out_shape"), control::stringProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ControlResult error;
		ClipRef ref;
		if (!resolveAudioClip(args.value(QStringLiteral("clip")).toString(), &ref, &error)) { return error; }
		const bool inverted = args.value(QStringLiteral("inverted")).toBool();
		const ClipEdits before = ref.clip->clipEdits();
		ClipEdits after = before;
		after.polarityInverted = inverted;
		// An unchanged request records nothing: `setClipEdits` would not emit, and a step that changes
		// nothing would shadow the edit before it (the registry's no-op rule, BUG-DRYRUN-UNDO's shape).
		if (after != before)
		{
			ref.clip->addJournalCheckPoint();
			ref.clip->setClipEdits(after);
			Engine::getSong()->setModified();
		}
		QJsonObject result = editsState(ref, after);
		if (after != before)
		{
			QJsonObject inverseArgs = clipArg(ref);
			inverseArgs.insert(QStringLiteral("inverted"), before.hasPolarity());
			result.insert(QStringLiteral("__transaction"),
				control::transactionPayload(editsState(ref, before),
					QStringLiteral("clip.set_polarity"), inverseArgs, true, ClauseClipJournalled));
		}
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
		"each (linear, exponential, logarithmic, s_curve, equal_power). An omitted argument keeps its current value; "
		"fade_in + fade_out must fit inside the clip. 'fade_in_offset' / 'fade_out_offset' (ticks, default 0) "
		"start the fade-in that far after the clip's start / end the fade-out that far before its end; set as a pair, the fade-in starting where the fade-out ends. "
		"Reversible through the ProjectJournal (Clip checkpoint).");
	cmd.argsSchema = control::objectSchema({
		{QStringLiteral("clip"), control::stringProperty()},
		{QStringLiteral("fade_in"), control::integerProperty(0, MaxSongLength)},
		{QStringLiteral("fade_out"), control::integerProperty(0, MaxSongLength)},
		{QStringLiteral("fade_in_shape"), control::stringProperty()},
		{QStringLiteral("fade_out_shape"), control::stringProperty()},
		{QStringLiteral("fade_in_offset"), control::integerProperty(0, MaxSongLength)},
		{QStringLiteral("fade_out_offset"), control::integerProperty(0, MaxSongLength)},
	}, {QStringLiteral("clip")});
	cmd.resultSchema = control::objectSchema({
		{QStringLiteral("clip"), control::stringProperty()},
		{QStringLiteral("gain_db"), control::numberProperty()},
		{QStringLiteral("inverted"), control::booleanProperty()},
		{QStringLiteral("fade_in"), control::integerProperty(0, MaxSongLength)},
		{QStringLiteral("fade_out"), control::integerProperty(0, MaxSongLength)},
		{QStringLiteral("fade_in_shape"), control::stringProperty()},
		{QStringLiteral("fade_out_shape"), control::stringProperty()},
		{QStringLiteral("fade_in_offset"), control::integerProperty(0, MaxSongLength)},
		{QStringLiteral("fade_out_offset"), control::integerProperty(0, MaxSongLength)},
	});
	cmd.mutating = true;
	cmd.handler = setClipFade;
	registry.registerCommand(cmd);
}

void registerClipCrossfade(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("clip.crossfade");
	cmd.group = QStringLiteral("clip");
	cmd.verb = QStringLiteral("crossfade");
	cmd.description = QStringLiteral("Ramp two overlapping audio clips on one track into each other. "
		"mode 'clamp' (default) uses edge fades over the overlap and creates no offset fades. "
		"mode 'container' opts into the V-shaped fade for a clip lying inside another; it uses the inner clip's "
		"authored fade-in and fade-out lengths and refuses missing or non-fitting ramps. Holding Alt while dropping "
		"a clip onto another on the same track invokes that container gesture. "
		"outgoing clip gets a fade-out and the incoming clip a fade-in, both exactly as long as "
		"their overlap, with the given shape (default equal_power, which sums the pair to unity "
		"POWER) - or each side its own: 'out_length' / 'in_length' (ticks) and 'out_shape' / 'in_shape'. "
		"Reversible through the ProjectJournal (Track checkpoint).");
	cmd.argsSchema = control::objectSchema({
		{QStringLiteral("out"), control::stringProperty()},
		{QStringLiteral("in"), control::stringProperty()},
		{QStringLiteral("mode"), control::stringProperty()},
		{QStringLiteral("shape"), control::stringProperty()},
		{QStringLiteral("out_length"), control::integerProperty(1, MaxSongLength)},
		{QStringLiteral("in_length"), control::integerProperty(1, MaxSongLength)},
		{QStringLiteral("out_shape"), control::stringProperty()},
		{QStringLiteral("in_shape"), control::stringProperty()},
	}, {QStringLiteral("out"), QStringLiteral("in")});
	cmd.resultSchema = control::objectSchema({
		{QStringLiteral("out"), control::stringProperty()},
		{QStringLiteral("in"), control::stringProperty()},
		{QStringLiteral("overlap"), control::integerProperty(0, MaxSongLength)},
		{QStringLiteral("mode"), control::stringProperty()},
		{QStringLiteral("shape"), control::stringProperty()},
		{QStringLiteral("out_fade_out"), control::integerProperty(0, MaxSongLength)},
		{QStringLiteral("in_fade_in"), control::integerProperty(0, MaxSongLength)},
		{QStringLiteral("out_shape"), control::stringProperty()},
		{QStringLiteral("in_shape"), control::stringProperty()},
		{QStringLiteral("out_fade_out_offset"), control::integerProperty(0, MaxSongLength)},
		{QStringLiteral("in_fade_in_offset"), control::integerProperty(0, MaxSongLength)},
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
				QStringLiteral("'out' and 'in' are the same clip (%1)").arg(clipId(outRef.id)));
		}
		if (outRef.track != inRef.track)
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("'out' (%1) and 'in' (%2) are on different tracks; a crossfade "
					"pairs two clips of ONE track's lane")
					.arg(clipId(outRef.id), clipId(inRef.id)));
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
					.arg(clipId(outRef.id), clipId(inRef.id))
					.arg(outStart).arg(outEnd).arg(inStart).arg(inEnd));
		}
		const int overlap = static_cast<int>(overlapEnd - overlapStart);

		const ClipEdits outBefore = outRef.clip->clipEdits();
		const ClipEdits inBefore = inRef.clip->clipEdits();
		ClipEdits outAfter = outBefore;
		ClipEdits inAfter = inBefore;
		FadeShape shape = FadeShape::EqualPower;
		if (!crossfadeSidesFor(args, outRef, inRef, overlap, &shape, &outAfter, &inAfter, &error)) { return error; }
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
		before.insert(QStringLiteral("out"), clipId(outRef.id));
		before.insert(QStringLiteral("in"), clipId(inRef.id));
		before.insert(QStringLiteral("overlap"), overlap);
		before.insert(QStringLiteral("out_fade_out"), outBefore.fadeOutTicks);
		before.insert(QStringLiteral("out_fade_out_shape"), fadeShapeName(outBefore.fadeOutShape));
		before.insert(QStringLiteral("in_fade_in"), inBefore.fadeInTicks);
		before.insert(QStringLiteral("in_fade_in_shape"), fadeShapeName(inBefore.fadeInShape));

		QJsonObject result;
		result.insert(QStringLiteral("out"), clipId(outRef.id));
		result.insert(QStringLiteral("in"), clipId(inRef.id));
		result.insert(QStringLiteral("overlap"), overlap);
		result.insert(QStringLiteral("mode"), args.contains(QStringLiteral("mode"))
			? args.value(QStringLiteral("mode")).toString() : QStringLiteral("clamp"));
		result.insert(QStringLiteral("shape"), fadeShapeName(shape));
		result.insert(QStringLiteral("out_fade_out"), outAfter.fadeOutTicks);
		result.insert(QStringLiteral("in_fade_in"), inAfter.fadeInTicks);
		// 'shape' is the shared one; the two sides say what each clip now holds (out_shape / in_shape may differ).
		result.insert(QStringLiteral("out_shape"), fadeShapeName(outAfter.fadeOutShape));
		result.insert(QStringLiteral("in_shape"), fadeShapeName(inAfter.fadeInShape));
		insertOffsets(&result, outAfter, inAfter);
		QJsonObject inverseArgs;
		inverseArgs.insert(QStringLiteral("out"), clipId(outRef.id));
		inverseArgs.insert(QStringLiteral("in"), clipId(inRef.id));
		inverseArgs.insert(QStringLiteral("out_fade_out"), outBefore.fadeOutTicks);
		inverseArgs.insert(QStringLiteral("out_fade_out_shape"),
			fadeShapeName(outBefore.fadeOutShape));
		inverseArgs.insert(QStringLiteral("in_fade_in"), inBefore.fadeInTicks);
		inverseArgs.insert(QStringLiteral("in_fade_in_shape"), fadeShapeName(inBefore.fadeInShape));
		// The inverse is the TRACK checkpoint (both clips live on it, which the
		// guard above enforces), so the op names what replays it (E19); the
		// args are the previous fades, for audit.
		result.insert(QStringLiteral("__transaction"),
			control::transactionPayload(before,
				QStringLiteral("control.undo"),
				inverseArgs, true, ClauseTrackJournalled));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

} // namespace

void registerClipEditsCommands(ControlRegistry& registry)
{
	registerClipSetGain(registry);
	registerClipSetPolarity(registry);
	registerClipSetRecord(registry);
	registerClipSetFade(registry);
	registerClipCrossfade(registry);
}

} // namespace lmms
