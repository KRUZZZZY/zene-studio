/*
 * SessionAudioCells.cpp - a launched AUDIO cell renders its clip (0.5.0 L02, WF-055 / WF-054)
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

#include "SessionAudioCells.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <vector>

#include <QFileInfo>
#include <QPointer>

#include "AudioBusHandle.h"
#include "AudioEngine.h"
#include "AudioResampler.h"
#include "InstrumentTrack.h"
#include "SessionAudioBake.h"
#include "SessionModel.h"
#include "Engine.h"
#include "PathUtil.h"
#include "SampleBuffer.h"
#include "SampleTrack.h"
#include "SessionScheduler.h"
#include "Song.h"

namespace lmms
{

SessionCellPlayHandle::SessionCellPlayHandle(Track* track, AudioBusHandle* bus) :
	PlayHandle(PlayHandle::Type::SessionCellHandle),
	m_track(track)
{
	setAudioBusHandle(bus);
}

SessionCellPlayHandle::SessionCellPlayHandle(SampleTrack* track) :
	SessionCellPlayHandle(track, track->audioBusHandle())
{
}

SessionCellPlayHandle::SessionCellPlayHandle(InstrumentTrack* track) :
	SessionCellPlayHandle(track, track->audioBusHandle())
{
}

bool SessionCellPlayHandle::isFromTrack(const Track* track) const
{
	return m_track == track;
}

namespace
{

//! @a track's column: its position in the song's track list, or -1. The list only changes under the
//! engine's model-change lock, which excludes this render.
int columnOf(const Track* track)
{
	const TrackContainer::TrackList& tracks = Engine::getSong()->tracks();
	const auto it = std::find(tracks.begin(), tracks.end(), track);
	return it == tracks.end() ? -1 : static_cast<int>(it - tracks.begin());
}

} // namespace

namespace
{

//! The loop region of @a audio inside a clip of @a size frames: [start, end), the whole clip when none is set.
std::pair<std::size_t, std::size_t> loopRegion(const SessionAudioShape& audio, std::size_t size)
{
	const auto start = static_cast<std::size_t>(std::clamp<std::int64_t>(audio.loopStartFrames, 0,
		static_cast<std::int64_t>(size) - 1));
	std::size_t end = size;
	if (audio.loopLengthFrames > 0) { end = std::min(size, start + static_cast<std::size_t>(audio.loopLengthFrames)); }
	return end > start ? std::make_pair(start, end) : std::make_pair(std::size_t{0}, size);
}

//! Where a launch starts in the clip, in frames: the scheduler's tick position, folded into the loop region.
std::size_t launchFrame(const SessionSlotPlayback& view, std::size_t size, std::size_t loopStart, std::size_t loopEnd)
{
	const double at = std::max(0.0, std::round(view.positionTicks * static_cast<double>(Engine::framesPerTick())));
	std::size_t position = static_cast<std::size_t>(at) % size;
	if (position >= loopEnd) { position = loopStart + (position - loopStart) % (loopEnd - loopStart); }
	return position;
}

} // namespace

void SessionCellPlayHandle::play(std::span<SampleFrame> buffer)
{
	std::fill(buffer.begin(), buffer.end(), SampleFrame(0.0f, 0.0f));
	const int column = columnOf(m_track);
	const SessionSlotPlayback view = column < 0 ? SessionSlotPlayback{}
		: Engine::getSong()->sessionScheduler().playbackForColumn(column);
	if (!view.playing || view.clip == nullptr || view.clip->size() == 0)
	{
		m_clip = nullptr;
		return;
	}
	const std::size_t size = view.clip->size();
	// The loop region (the cell's loop start / length) in frames of this clip; none means the whole clip.
	const auto [loopStart, loopEnd] = loopRegion(view.audio, size);
	if (view.clip != m_clip || view.scene != m_scene || view.startedTick != m_launchTick)
	{
		// A new launch: from where the scheduler says the cell is (its top, or its loop start, or the phase a
		// legato launch took over), in frames of this clip.
		m_clip = view.clip;
		m_scene = view.scene;
		m_launchTick = view.startedTick;
		m_position = launchFrame(view, size, loopStart, loopEnd);
	}
	const SampleFrame* clip = m_clip->data();
	const float gain = view.audio.gain;
	for (SampleFrame& frame : buffer)
	{
		frame = clip[m_position] * gain;
		m_position = m_position + 1 < loopEnd ? m_position + 1 : loopStart;
	}
	m_framesRendered.fetch_add(buffer.size(), std::memory_order_relaxed);
}

namespace sessionaudio
{

namespace
{

//! The clips of the open project by file, and the ones of the project closed last (retire()).
struct Clips
{
	std::map<QString, std::shared_ptr<const SampleBuffer>> held;  //!< the source files, by path
	std::map<QString, std::shared_ptr<const SampleBuffer>> variants;  //!< the settings-variants, by path and settings
	std::vector<std::shared_ptr<const SampleBuffer>> retired;
	std::vector<QPointer<Track>> handled;  //!< tracks that have a SessionCellPlayHandle
	int cachedRate = 0;  //!< the engine rate held clips were resampled for; 0 before first load
};

Clips& clips()
{
	static Clips instance;
	return instance;
}

//! @a buffer at the engine's rate: as it is when it already is, converted (once, here) when it is not.
std::shared_ptr<const SampleBuffer> atEngineRate(std::shared_ptr<const SampleBuffer> buffer)
{
	const auto rate = static_cast<int>(Engine::audioEngine()->outputSampleRate());
	if (buffer == nullptr || buffer->size() == 0 || static_cast<int>(buffer->sampleRate()) == rate) { return buffer; }
	AudioResampler resampler(AudioResampler::Mode::SincMedium);
	resampler.setRatio(buffer->sampleRate(), static_cast<sample_rate_t>(rate));
	std::vector<SampleFrame> out(static_cast<std::size_t>(std::ceil(buffer->size()
		* static_cast<double>(rate) / buffer->sampleRate())) + 1);
	const auto result = resampler.process({&buffer->data()[0][0], 2, buffer->size()}, {&out[0][0], 2, out.size()});
	out.resize(static_cast<std::size_t>(result.outputFramesGenerated));
	return std::make_shared<const SampleBuffer>(std::move(out), rate);
}

const SampleBuffer* load(const QString& source)
{
	Clips& all = clips();
	const auto rate = static_cast<int>(Engine::audioEngine()->outputSampleRate());
	if (all.cachedRate != 0 && all.cachedRate != rate)
	{
		for (auto& entry : all.held) { all.retired.push_back(std::move(entry.second)); }
		for (auto& entry : all.variants) { all.retired.push_back(std::move(entry.second)); }
		all.held.clear();
		all.variants.clear();
	}
	all.cachedRate = rate;
	const auto found = all.held.find(source);
	if (found != all.held.end()) { return found->second.get(); }
	if (!QFileInfo::exists(PathUtil::toAbsolute(source))) { return nullptr; }  // no warning box at a launch
	std::shared_ptr<const SampleBuffer> clip = atEngineRate(SampleBuffer::fromFile(source));
	if (clip == nullptr || clip->size() == 0) { return nullptr; }
	all.held.emplace(source, clip);
	return clip.get();
}

//! The most settings-variants of clips one project may hold (every tempo a warped cell was launched at is one);
//! past it the newest variant is not made and the cell plays its file as it is. The source files a project loads
//! are not settings-variants: they do not use this budget.
constexpr std::size_t MaxHeldVariants = 256;

//! @a source with @a params applied, made once per setting (the key holds the settings) and held like the file.
const SampleBuffer* variant(const QString& source, const SampleBuffer* original, const BakeParams& params)
{
	if (params.isIdentity()) { return original; }
	Clips& all = clips();
	const QString key = QStringLiteral("%1|w%2|p%3").arg(source).arg(params.warpSpeed, 0, 'g', 12).arg(params.pitchRatio, 0, 'g', 12);
	const auto found = all.variants.find(key);
	if (found != all.variants.end()) { return found->second.get(); }
	if (all.variants.size() >= MaxHeldVariants) { return original; }
	std::vector<SampleFrame> frames = bakeClip(original->data(), original->size(), params);
	if (frames.empty()) { return original; }
	auto clip = std::make_shared<const SampleBuffer>(std::move(frames), static_cast<int>(original->sampleRate()));
	all.variants.emplace(key, clip);
	return clip.get();
}

//! The renderer's home: a sample track's or an instrument track's bus.
bool canRenderCells(Track* track)
{
	return dynamic_cast<SampleTrack*>(track) != nullptr || dynamic_cast<InstrumentTrack*>(track) != nullptr;
}

void ensureHandle(Track* track)
{
	auto& handled = clips().handled;
	handled.erase(std::remove_if(handled.begin(), handled.end(), [](const QPointer<Track>& t) { return t.isNull(); }),
		handled.end());
	if (std::find(handled.begin(), handled.end(), track) != handled.end()) { return; }
	if (auto* sample = dynamic_cast<SampleTrack*>(track)) { Engine::audioEngine()->addPlayHandle(new SessionCellPlayHandle(sample)); }
	else { Engine::audioEngine()->addPlayHandle(new SessionCellPlayHandle(dynamic_cast<InstrumentTrack*>(track))); }
	handled.emplace_back(track);
}

} // namespace

Prepared prepare(Song& song, int column, const ClipSlot& slot, double tempo) noexcept
{
	try
	{
		const QString& source = slot.audioSource();
		Track* track = column >= 0 && column < static_cast<int>(song.tracks().size())
			? song.tracks()[static_cast<std::size_t>(column)] : nullptr;
		if (track == nullptr || !canRenderCells(track)) { return {}; }
		const SampleBuffer* original = load(source);
		if (original == nullptr) { return {}; }
		ensureHandle(track);
		Prepared prepared;
		prepared.clip = variant(source, original, bakeParamsFor(slot, tempo));
		prepared.shape.gain = std::pow(10.0f, slot.gainDb() / 20.0f);
		const double framesPerTick = tempo > 0.0 ? Engine::framesPerTick() : 0.0;
		prepared.shape.loopStartFrames = static_cast<std::int64_t>(std::llround(slot.loopStart() * framesPerTick));
		prepared.shape.loopLengthFrames = static_cast<std::int64_t>(std::llround(slot.loopLength() * framesPerTick));
		return prepared;
	}
	catch (...)
	{
		return {};  // an unreadable file leaves the cell silent, as every audio cell was before
	}
}

void retire() noexcept
{
	Clips& all = clips();
	all.retired.clear();
	for (auto& entry : all.held) { all.retired.push_back(std::move(entry.second)); }
	for (auto& entry : all.variants) { all.retired.push_back(std::move(entry.second)); }
	all.held.clear();
	all.variants.clear();
	all.cachedRate = 0;
}

int heldClips() noexcept
{
	return static_cast<int>(clips().held.size() + clips().variants.size());
}

} // namespace sessionaudio

} // namespace lmms
