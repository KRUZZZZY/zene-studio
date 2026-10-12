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
#include "Engine.h"
#include "PathUtil.h"
#include "SampleBuffer.h"
#include "SampleTrack.h"
#include "SessionScheduler.h"
#include "Song.h"

namespace lmms
{

SessionCellPlayHandle::SessionCellPlayHandle(SampleTrack* track) :
	PlayHandle(PlayHandle::Type::SessionCellHandle),
	m_track(track)
{
	setAudioBusHandle(track->audioBusHandle());
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
	if (view.clip != m_clip || view.scene != m_scene || view.startedTick != m_launchTick)
	{
		// A new launch: from where the scheduler says the cell is (its top, or its loop start, or the phase a
		// legato launch took over), in frames of this clip.
		m_clip = view.clip;
		m_scene = view.scene;
		m_launchTick = view.startedTick;
		const double at = std::max(0.0, std::round(view.positionTicks * static_cast<double>(Engine::framesPerTick())));
		m_position = static_cast<std::size_t>(at) % size;
	}
	const SampleFrame* clip = m_clip->data();
	for (SampleFrame& frame : buffer)
	{
		frame = clip[m_position];
		m_position = m_position + 1 < size ? m_position + 1 : 0;
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
	std::map<QString, std::shared_ptr<const SampleBuffer>> held;
	std::vector<std::shared_ptr<const SampleBuffer>> retired;
	std::vector<QPointer<SampleTrack>> handled;  //!< tracks that have a SessionCellPlayHandle
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
	const auto found = all.held.find(source);
	if (found != all.held.end()) { return found->second.get(); }
	if (!QFileInfo::exists(PathUtil::toAbsolute(source))) { return nullptr; }  // no warning box at a launch
	std::shared_ptr<const SampleBuffer> clip = atEngineRate(SampleBuffer::fromFile(source));
	if (clip == nullptr || clip->size() == 0) { return nullptr; }
	all.held.emplace(source, clip);
	return clip.get();
}

void ensureHandle(SampleTrack* track)
{
	auto& handled = clips().handled;
	handled.erase(std::remove_if(handled.begin(), handled.end(), [](const QPointer<SampleTrack>& t) { return t.isNull(); }),
		handled.end());
	if (std::find(handled.begin(), handled.end(), track) != handled.end()) { return; }
	Engine::audioEngine()->addPlayHandle(new SessionCellPlayHandle(track));
	handled.emplace_back(track);
}

} // namespace

const SampleBuffer* prepare(Song& song, int column, const QString& source) noexcept
{
	try
	{
		const SampleBuffer* clip = load(source);
		auto* track = column >= 0 && column < static_cast<int>(song.tracks().size())
			? dynamic_cast<SampleTrack*>(song.tracks()[static_cast<std::size_t>(column)]) : nullptr;
		if (clip != nullptr && track != nullptr) { ensureHandle(track); }
		return clip;
	}
	catch (...)
	{
		return nullptr;  // an unreadable file leaves the cell silent, as every audio cell was before
	}
}

void retire() noexcept
{
	Clips& all = clips();
	all.retired.clear();
	for (auto& entry : all.held) { all.retired.push_back(std::move(entry.second)); }
	all.held.clear();
}

int heldClips() noexcept
{
	return static_cast<int>(clips().held.size());
}

} // namespace sessionaudio

} // namespace lmms
