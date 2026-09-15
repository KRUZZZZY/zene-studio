/*
 * ControlCommandsWarpEdit.cpp - the mutating half of the warp.* command group
 *                               (SPEC A11-A16): warp.add / warp.move /
 *                               warp.remove / warp.set.
 *
 * Every verb here edits the marker map of ONE SampleClip and takes the clip's
 * own journal checkpoint before it writes, so one control.undo reverses one
 * command (SPEC A16 deliverable 3). The set is validated against the engine's
 * OWN value type before anything is written, so a refused set leaves the clip
 * untouched rather than half-edited.
 *
 * What this file does NOT do: it does not stretch anything. The mapping, the
 * resampler, the tempo-follower default and the render are #597's and are
 * unchanged (docs/WARP.md); this is the surface that authors markers.
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

#include <span>

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include "ControlEdit.h"       // ClipRef, resolveClip, trackTypeNameOf
#include "ControlRegistry.h"
#include "ControlWarpSupport.h"

namespace lmms
{

using namespace control; // the shared vocabulary lives in ControlVocabulary.h

namespace
{

//! Everything warp.set accepts, once the arguments have been read.
struct WarpSetArguments
{
	bool markers = false;
	bool mode = false;
	bool sourceTempo = false;
	int markerCount = 0;
	MarkerArray markerValues{};
	WarpTempoMode tempoMode = WarpTempoMode::FollowProject;
	float tempo = 0.0f;
};

bool readMarkerList(const QJsonArray& wanted, WarpSetArguments* out, ControlResult* error)
{
	if (wanted.size() > WarpMarkers::MaxMarkers)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("%1 markers is past the engine's maximum of %2")
				.arg(wanted.size()).arg(WarpMarkers::MaxMarkers));
		return false;
	}
	for (const QJsonValue& value : wanted)
	{
		if (!readMarker(value, &out->markerValues[static_cast<std::size_t>(out->markerCount)], error))
		{
			return false;
		}
		++out->markerCount;
	}
	return true;
}

bool readTempoModeArgument(const QJsonObject& args, WarpSetArguments* out, ControlResult* error)
{
	const QString mode = args.value(QStringLiteral("mode")).toString();
	if (mode == QLatin1String("source")) { out->tempoMode = WarpTempoMode::SourceTempo; return true; }
	if (mode == QLatin1String("follow")) { out->tempoMode = WarpTempoMode::FollowProject; return true; }
	*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
		QStringLiteral("'mode' is '%1'; it is either 'follow' (the clip follows the project tempo) "
			"or 'source' (the clip leads with the tempo it was recorded at)").arg(mode));
	return false;
}

bool readWarpSetArguments(const QJsonObject& args, WarpSetArguments* out, ControlResult* error)
{
	out->markers = args.contains(QStringLiteral("markers"));
	out->mode = args.contains(QStringLiteral("mode"));
	out->sourceTempo = args.contains(QStringLiteral("source_tempo"));
	if (!out->markers && !out->mode && !out->sourceTempo)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("warp.set needs at least one of 'markers', 'mode' or 'source_tempo'"));
		return false;
	}
	if (out->markers && !readMarkerList(args.value(QStringLiteral("markers")).toArray(), out, error))
	{
		return false;
	}
	if (out->mode && !readTempoModeArgument(args, out, error)) { return false; }
	if (out->sourceTempo)
	{
		const double bpm = args.value(QStringLiteral("source_tempo")).toDouble(-1.0);
		if (bpm < 0.0)
		{
			*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("'source_tempo' is %1; it is a tempo in BPM and cannot be negative")
					.arg(bpm));
			return false;
		}
		out->tempo = static_cast<float>(bpm);
	}
	return true;
}

//! Refuses a set the engine would reject, BEFORE the checkpoint, so a refused
//! call writes nothing at all - not even the tempo mode it also carried.
bool markerSetIsWritable(const WarpSetArguments& wanted, ControlResult* error)
{
	if (!wanted.markers || wanted.markerCount == 0) { return true; }
	WarpMarkers candidate;
	if (candidate.set(std::span<const WarpMarker>(wanted.markerValues.data(),
		static_cast<std::size_t>(wanted.markerCount))))
	{
		return true;
	}
	*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
		QStringLiteral("the marker set is not strictly increasing in source frame and timeline "
			"position: the engine refuses it and nothing was written"));
	return false;
}

//! The marker the arguments name, by the source frame it pins.
struct AddressedMarker
{
	int index = -1;
	f_cnt_t sourceFrame = 0;
};

bool addressMarker(const QJsonObject& args, const SampleClip& clip, AddressedMarker* out,
	ControlResult* error)
{
	const double sourceFrame = args.value(QStringLiteral("source_frame")).toDouble(-1.0);
	const int index = sourceFrame < 0.0 ? -1
		: indexOfSourceFrame(clip.warpMarkers(), static_cast<f_cnt_t>(sourceFrame));
	if (index < 0)
	{
		*error = ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("no warp marker pins source frame %1 (the clip has %2 marker(s))")
				.arg(static_cast<qlonglong>(sourceFrame)).arg(clip.warpMarkers().size()));
		return false;
	}
	out->index = index;
	out->sourceFrame = static_cast<f_cnt_t>(sourceFrame);
	return true;
}

ControlResult warpAdd(const QJsonObject& args)
{
	ClipRef ref;
	ControlResult error;
	SampleClip* clip = resolveSampleClip(args, &ref, &error);
	if (clip == nullptr) { return error; }

	WarpMarker marker;
	if (!readMarker(args, &marker, &error)) { return error; }

	int count = 0;
	MarkerArray next = markerArrayOf(clip->warpMarkers(), &count);
	if (count >= WarpMarkers::MaxMarkers)
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("the clip already holds the engine's maximum of %1 warp markers")
				.arg(WarpMarkers::MaxMarkers));
	}
	next[static_cast<std::size_t>(count)] = marker;

	WarpMarkers candidate;
	if (!candidate.set(std::span<const WarpMarker>(next.data(), static_cast<std::size_t>(count) + 1)))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("a marker for source frame %1 at offset %2 would reuse that source frame "
				"or put the set out of order: the engine refuses it and no marker was added")
				.arg(static_cast<qlonglong>(marker.sourceFrame))
				.arg(static_cast<qlonglong>(marker.offsetTicks)));
	}

	const QJsonObject before = warpBefore(ref, *clip);
	clip->addJournalCheckPoint();
	applyMarkers(clip, next, count + 1, &error);

	QJsonObject result = warpState(ref, *clip);
	result.insert(QStringLiteral("added"),
		markerJson(marker, indexOfSourceFrame(clip->warpMarkers(), marker.sourceFrame)));
	result.insert(QStringLiteral("__transaction"), warpInverse(before));
	return ControlResult::success(result);
}

ControlResult warpMove(const QJsonObject& args)
{
	ClipRef ref;
	ControlResult error;
	SampleClip* clip = resolveSampleClip(args, &ref, &error);
	if (clip == nullptr) { return error; }

	AddressedMarker addressed;
	if (!addressMarker(args, *clip, &addressed, &error)) { return error; }

	const tick_t wanted = static_cast<tick_t>(args.value(QStringLiteral("offset_ticks")).toDouble());
	tick_t low = 0;
	tick_t high = 0;
	offsetBounds(clip->warpMarkers(), addressed.index, &low, &high);
	if (wanted < low || wanted > high)
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("offset %1 for the marker at source frame %2 is outside %3..%4: a marker "
				"must stay strictly between its neighbours in timeline position")
				.arg(static_cast<qlonglong>(wanted))
				.arg(static_cast<qlonglong>(addressed.sourceFrame))
				.arg(static_cast<qlonglong>(low)).arg(static_cast<qlonglong>(high)));
	}

	int count = 0;
	MarkerArray next = markerArrayOf(clip->warpMarkers(), &count);
	const tick_t previous = next[static_cast<std::size_t>(addressed.index)].offsetTicks;
	next[static_cast<std::size_t>(addressed.index)].offsetTicks = wanted;

	const QJsonObject before = warpBefore(ref, *clip);
	clip->addJournalCheckPoint();
	if (!applyMarkers(clip, next, count, &error)) { return error; }

	QJsonObject moved;
	moved.insert(QStringLiteral("source_frame"), static_cast<qlonglong>(addressed.sourceFrame));
	moved.insert(QStringLiteral("offset_ticks"), static_cast<qlonglong>(wanted));
	moved.insert(QStringLiteral("previous_offset_ticks"), static_cast<qlonglong>(previous));

	QJsonObject result = warpState(ref, *clip);
	result.insert(QStringLiteral("moved"), moved);
	result.insert(QStringLiteral("__transaction"), warpInverse(before));
	return ControlResult::success(result);
}

ControlResult warpRemove(const QJsonObject& args)
{
	ClipRef ref;
	ControlResult error;
	SampleClip* clip = resolveSampleClip(args, &ref, &error);
	if (clip == nullptr) { return error; }

	AddressedMarker addressed;
	if (!addressMarker(args, *clip, &addressed, &error)) { return error; }

	int count = 0;
	MarkerArray next = markerArrayOf(clip->warpMarkers(), &count);
	const WarpMarker removed = next[static_cast<std::size_t>(addressed.index)];
	for (int i = addressed.index; i + 1 < count; ++i)
	{
		next[static_cast<std::size_t>(i)] = next[static_cast<std::size_t>(i) + 1];
	}

	const QJsonObject before = warpBefore(ref, *clip);
	clip->addJournalCheckPoint();
	if (!applyMarkers(clip, next, count - 1, &error)) { return error; }

	QJsonObject result = warpState(ref, *clip);
	result.insert(QStringLiteral("removed"), markerJson(removed, addressed.index));
	result.insert(QStringLiteral("was_last_marker"), count == 1);
	result.insert(QStringLiteral("__transaction"), warpInverse(before));
	return ControlResult::success(result);
}

ControlResult warpSet(const QJsonObject& args)
{
	ClipRef ref;
	ControlResult error;
	SampleClip* clip = resolveSampleClip(args, &ref, &error);
	if (clip == nullptr) { return error; }

	WarpSetArguments wanted;
	if (!readWarpSetArguments(args, &wanted, &error)) { return error; }
	if (!markerSetIsWritable(wanted, &error)) { return error; }

	const QJsonObject before = warpBefore(ref, *clip);
	// ONE checkpoint for the whole call: the marker list, the tempo mode and the
	// source tempo all live in the same serialized <warp> element, so one undo
	// takes back all three (SPEC A16 deliverable 3).
	clip->addJournalCheckPoint();
	if (wanted.mode) { clip->setWarpTempoMode(wanted.tempoMode); }
	if (wanted.sourceTempo) { clip->setSourceTempo(wanted.tempo); }
	if (wanted.markers && !applyMarkers(clip, wanted.markerValues, wanted.markerCount, &error))
	{
		return error;
	}

	QJsonObject result = warpState(ref, *clip);
	result.insert(QStringLiteral("__transaction"), warpInverse(before));
	return ControlResult::success(result);
}

/*! Row 30 of the 0.3.0 list: choose how a clip renders a rate change.
 *
 *  `resample` (the default) is what the engine has always done - the warped
 *  rate is handed to `AudioResampler` and the pitch moves with it.
 *  `preserve_pitch` routes the same rate through `AudioStretcher` (WSOLA), so
 *  the clip lasts as long as the mapping says and keeps its pitch.
 *
 *  A clip the mode means nothing for - one that renders linearly, i.e. with no
 *  marker and no source tempo - is REFUSED rather than silently accepted, because
 *  the render path deliberately bypasses the stretcher for it and a success
 *  here would promise a pitch that the next playback pass would not deliver.
 *  The clip's own state is the checkpoint (the `stretch` attribute of the same
 *  serialized <warp> element), so one control.undo takes the mode back. */
ControlResult warpStretch(const QJsonObject& args)
{
	ClipRef ref;
	ControlResult error;
	SampleClip* clip = resolveSampleClip(args, &ref, &error);
	if (clip == nullptr) { return error; }

	const QString mode = args.value(QStringLiteral("mode")).toString();
	if (mode != QLatin1String("resample") && mode != QLatin1String("preserve_pitch"))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'mode' is '%1'; it is either 'resample' (the rate is rendered by plain "
				"resampling and the pitch moves with it - the default) or 'preserve_pitch' (the "
				"rate is rendered by the WSOLA stretcher and the pitch stays where it is)").arg(mode));
	}

	// Honest refusal: with no rate change there is nothing to preserve the
	// pitch across, and SamplePlayHandle does not route such a clip through
	// the stretcher at all (see renderPreservingPitch).
	if (mode == QLatin1String("preserve_pitch") && clip->rendersLinearly())
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("this clip has no warp markers and follows the project tempo, so it "
				"renders linearly: there is no rate change for a pitch-preserving stretch to "
				"render. Author the warp first (warp.add / warp.set), or declare the clip's own "
				"source tempo, and then set the stretch mode."));
	}

	const WarpStretchMode wanted = mode == QLatin1String("preserve_pitch")
		? WarpStretchMode::PreservePitch : WarpStretchMode::Resample;
	const WarpStretchMode previous = clip->warpStretchMode();

	const QJsonObject before = warpBefore(ref, *clip);
	// The same one-checkpoint shape warp.set uses: the mode lives in the clip's
	// own <warp> element, so the clip's checkpoint is the exact inverse.
	clip->addJournalCheckPoint();
	clip->setWarpStretchMode(wanted);

	QJsonObject result = warpState(ref, *clip);
	result.insert(QStringLiteral("previous_mode"), stretchModeName(previous));
	result.insert(QStringLiteral("changed"), previous != wanted);

	// The inverse is this command with the previous mode, and it travels as
	// the same "warp.stretch" id the caller would re-issue by hand.
	QJsonObject inverseArgs;
	inverseArgs.insert(QStringLiteral("clip"), clipId(ref.id));
	inverseArgs.insert(QStringLiteral("mode"), stretchModeName(previous));
	result.insert(QStringLiteral("__transaction"), transactionPayload(before,
		QStringLiteral("warp.stretch"), inverseArgs, true,
		QStringLiteral("ProjectJournal (Clip checkpoint: the stretch mode is the 'stretch' "
			"attribute of the clip's own <warp> element, which SampleClip::saveSettings writes "
			"and SampleClip::loadSettings re-reads, so one undo restores the previous mode)")));
	return ControlResult::success(result);
}

//! One registration step per verb, because a lambda cannot carry a description
//! this long without burying the schema beside it.
void registerWarpAdd(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("warp.add");
	cmd.group = QStringLiteral("warp");
	cmd.verb = QStringLiteral("add");
	cmd.description = QStringLiteral("Pin one frame of a sample clip's audio to a timeline "
		"position (a warp marker). The clip keeps its markers in source-frame order and the engine "
		"refuses a marker that would reuse a source frame or put the set out of order - nothing is "
		"written in that case. Reversible through the ProjectJournal (Clip checkpoint).");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("clip"), stringProperty()},
		{QStringLiteral("source_frame"), integerProperty(0, MaxSchemaInteger)},
		{QStringLiteral("offset_ticks"), offsetTicksProperty()},
	}, {QStringLiteral("clip"), QStringLiteral("source_frame"), QStringLiteral("offset_ticks")});
	cmd.resultSchema = warpStateSchema({{QStringLiteral("added"), markerProperty()}});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) { return warpAdd(args); };
	registry.registerCommand(cmd);
}

void registerWarpMove(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("warp.move");
	cmd.group = QStringLiteral("warp");
	cmd.verb = QStringLiteral("move");
	cmd.description = QStringLiteral("Move the warp marker that pins a given source frame to a new "
		"timeline offset, keeping it pinned to that same frame of the audio. The new offset must "
		"stay strictly between the marker's neighbours. Reversible through the ProjectJournal "
		"(Clip checkpoint).");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("clip"), stringProperty()},
		{QStringLiteral("source_frame"), integerProperty(0, MaxSchemaInteger)},
		{QStringLiteral("offset_ticks"), offsetTicksProperty()},
	}, {QStringLiteral("clip"), QStringLiteral("source_frame"), QStringLiteral("offset_ticks")});
	cmd.resultSchema = warpStateSchema({{QStringLiteral("moved"), markerProperty()}});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) { return warpMove(args); };
	registry.registerCommand(cmd);
}

void registerWarpRemove(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("warp.remove");
	cmd.group = QStringLiteral("warp");
	cmd.verb = QStringLiteral("remove");
	cmd.description = QStringLiteral("Delete the warp marker that pins a given source frame, "
		"leaving the rest of the map in place. Removing the last marker leaves the clip unwarped, "
		"so its mapping is the linear one again. Reversible through the ProjectJournal (Clip "
		"checkpoint).");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("clip"), stringProperty()},
		{QStringLiteral("source_frame"), integerProperty(0, MaxSchemaInteger)},
	}, {QStringLiteral("clip"), QStringLiteral("source_frame")});
	cmd.resultSchema = warpStateSchema({
		{QStringLiteral("removed"), markerProperty()},
		{QStringLiteral("was_last_marker"), booleanProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) { return warpRemove(args); };
	registry.registerCommand(cmd);
}

void registerWarpSet(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("warp.set");
	cmd.group = QStringLiteral("warp");
	cmd.verb = QStringLiteral("set");
	cmd.description = QStringLiteral("Set a sample clip's warp map: replace the whole marker list "
		"('markers'; an empty array clears it), choose the tempo mode ('follow' the project tempo, "
		"or 'source' - the clip leads with the tempo it was recorded at) and/or declare that source "
		"tempo in BPM. One call is one undoable step. Reversible through the ProjectJournal (Clip "
		"checkpoint).");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("clip"), stringProperty()},
		{QStringLiteral("markers"), markersProperty()},
		{QStringLiteral("mode"), tempoModeProperty()},
		{QStringLiteral("source_tempo"), numberProperty()},
	}, {QStringLiteral("clip")});
	cmd.resultSchema = warpStateSchema();
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) { return warpSet(args); };
	registry.registerCommand(cmd);
}

void registerWarpStretch(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("warp.stretch");
	cmd.group = QStringLiteral("warp");
	cmd.verb = QStringLiteral("stretch");
	cmd.description = QStringLiteral("Choose how a warped sample clip renders its rate change: "
		"'resample' (the default, and what the engine has always done - the rate goes to the "
		"resampler and the pitch moves with it, so a 2x warp is an octave up) or 'preserve_pitch' "
		"(the same rate is rendered by the WSOLA stretcher, AudioStretcher: the clip lasts as long "
		"as the mapping says and keeps its own pitch). Refused for a clip that renders linearly - "
		"with no marker and no source tempo there is no rate change to render. Reversible through "
		"the ProjectJournal (Clip checkpoint).");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("clip"), stringProperty()},
		{QStringLiteral("mode"), stretchModeProperty()},
	}, {QStringLiteral("clip"), QStringLiteral("mode")});
	cmd.resultSchema = warpStateSchema({
		{QStringLiteral("previous_mode"), stringProperty()},
		{QStringLiteral("changed"), booleanProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) { return warpStretch(args); };
	registry.registerCommand(cmd);
}

} // namespace

void registerWarpEditCommands(ControlRegistry& registry)
{
	// add / move / remove edit ONE marker; set replaces the map and carries the
	// clip-level warp tempo with it; stretch picks how a rate change is
	// rendered (row 30 - resampling or the pitch-preserving WSOLA stretch).
	registerWarpAdd(registry);
	registerWarpMove(registry);
	registerWarpRemove(registry);
	registerWarpSet(registry);
	registerWarpStretch(registry);
}

} // namespace lmms
