/*
 * AafImportBuild.cpp - the AAF import's builder: an AafModel made into sample
 *                      tracks, sample clips and markers (include/AafImport.h).
 *
 * The builder needs a song and nothing from LibAAF, so its test builds a
 * hand-made model on any machine. It records no undo step: the command wraps
 * it in one (src/core/ControlCommandsAafImport.cpp).
 *
 * PLACEMENT. Every model time is frames at the model's sample rate; a position
 * becomes ticks at the project tempo after `shiftFrames` is taken off it. The
 * clip's SOURCE OFFSET is authored as the clip's sample window (its first
 * source frame, at the file's own rate), not as a tick offset: a tick at 120
 * BPM is about 10 ms, and an AAF's offsets are sample-accurate. Fades, clip
 * gain and the gain curve go into the clip's ClipEdits, the same values
 * clip.set_fade / clip.set_gain / clip.set_envelope write.
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

#include "AafImport.h"

#include <algorithm>
#include <cmath>

#include "ProjectMarkers.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "SampleWindow.h"
#include "Song.h"
#include "TimePos.h"
#include "Track.h"

namespace lmms::interchange
{

namespace
{

//! The model's frames as song ticks.
struct Placement
{
	const AafModel& model;
	const AafBuildOptions& options;

	//! A POSITION: shifted, then converted.
	qint64 at(qint64 frames) const { return span(frames - options.shiftFrames); }
	//! A LENGTH: converted only.
	qint64 span(qint64 frames) const { return aafTicksFromFrames(frames, model.sampleRate, options.tempo); }
};

ClipEdits editsFor(const AafClip& clip, const Placement& place, qint64 lengthTicks)
{
	ClipEdits edits;
	edits.gain = static_cast<float>(std::max(clip.gain, 0.0));
	edits.fadeInTicks = static_cast<int>(std::min(place.span(clip.fadeIn.frames), lengthTicks));
	edits.fadeOutTicks = static_cast<int>(std::min(place.span(clip.fadeOut.frames), lengthTicks));
	edits.fadeInShape = clip.fadeIn.shape;
	edits.fadeOutShape = clip.fadeOut.shape;
	const int count = std::min(static_cast<int>(clip.envelope.size()), MaxGainPoints);
	for (int index = 0; index < count; ++index)
	{
		const AafGainPoint& point = clip.envelope[index];
		edits.envelope[index] = GainPoint{static_cast<int>(std::lround(point.at * lengthTicks)),
			std::clamp(point.gainDb, MinGainPointDb, MaxGainPointDb)};
	}
	edits.envelopeCount = count;
	return edits;
}

//! The clip starts @a offsetFrames (model rate) into its source: authored as the sample window's first
//! frame at the file's own rate. An offset past the source's end is authored as a tick offset instead,
//! which plays nothing - the clip is as silent as it was in the AAF.
void placeSource(SampleClip* clip, qint64 offsetFrames, const Placement& place)
{
	if (offsetFrames <= 0) { return; }
	const auto bufferFrames = static_cast<f_cnt_t>(clip->sample().sampleSize());
	const double scale = static_cast<double>(clip->sample().sampleRate()) / place.model.sampleRate;
	const auto in = static_cast<f_cnt_t>(std::llround(static_cast<double>(offsetFrames) * scale));
	if (in < bufferFrames)
	{
		// The window is authored by its owner (the one-writer rule, WriteRefusalGateTest): the loaded
		// source and the window to play of it, in one SampleClip call.
		clip->replaceSample(clip->sample().buffer(), SampleWindow(in, bufferFrames));
		return;
	}
	clip->setStartTimeOffset(TimePos(static_cast<tick_t>(-place.span(offsetFrames))));
}

void buildClip(Track* track, const AafClip& clip, const Placement& place, AafBuildReport* report)
{
	if (clip.sourcePath.isEmpty())
	{
		report->missingMediaClips++;
		return;
	}
	const qint64 start = place.at(clip.positionFrames);
	const qint64 length = std::max<qint64>(1, place.span(clip.lengthFrames));
	if (start < 0 || start + length > MaxSongLength)
	{
		report->beyondSongLength++;
		return;
	}
	auto* made = dynamic_cast<SampleClip*>(track->createClip(TimePos(static_cast<tick_t>(start))));
	if (made == nullptr) { return; }
	made->setSampleFile(clip.sourcePath);
	placeSource(made, clip.sourceOffsetFrames, place);
	made->changeLength(TimePos(static_cast<tick_t>(length)));
	made->setAutoResize(false);
	made->setName(clip.name);
	made->setMuted(clip.muted);
	made->setClipEdits(editsFor(clip, place, length));
	report->clips++;
}

Track* buildTrack(Song* song, const AafTrack& source, const Placement& place, AafBuildReport* report)
{
	auto* track = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, song));
	if (track == nullptr) { return nullptr; }
	track->setName(source.name.isEmpty() ? QStringLiteral("AAF track %1").arg(source.number) : source.name);
	// The track's fader is LMMS' 0..200 % volume (unity 100); count gains that exceed its range.
	const float requestedVolume = static_cast<float>(source.gain * 100.0);
	const auto* volume = track->volumeModel();
	if (requestedVolume < volume->minValue() || requestedVolume > volume->maxValue()) { report->trackGainClamps++; }
	track->volumeModel()->setValue(requestedVolume);
	// LibAAF's pan is 0 (left) .. 0.5 (centre) .. 1 (right); LMMS' is -100 .. 100.
	if (source.hasPan) { track->panningModel()->setValue(static_cast<float>((source.pan - 0.5) * 200.0)); }
	track->setMuted(source.muted);
	for (const AafClip& clip : source.clips) { buildClip(track, clip, place, report); }
	report->tracks++;
	return track;
}

void buildMarkers(const AafModel& model, const Placement& place, AafBuildReport* report)
{
	ProjectMarkers& markers = ProjectMarkers::instance();
	for (const AafMarker& marker : model.markers)
	{
		const qint64 tick = place.at(marker.positionFrames);
		if (tick < 0 || tick > MaxSongLength) { continue; }
		const QString name = marker.name.isEmpty() ? QStringLiteral("Marker") : marker.name;
		if (markers.add(static_cast<tick_t>(tick), name, marker.color) >= 0) { report->markers++; }
	}
}

} // namespace

AafBuildReport buildAafSession(Song* song, const AafModel& model, const AafBuildOptions& options,
	std::vector<int>* madeTrackIds)
{
	AafBuildReport report;
	const Placement place{model, options};
	madeTrackIds->clear();
	for (const AafTrack& track : model.tracks)
	{
		if (Track* made = buildTrack(song, track, place, &report)) { madeTrackIds->push_back(made->id()); }
	}
	if (options.markers) { buildMarkers(model, place, &report); }
	return report;
}

} // namespace lmms::interchange
