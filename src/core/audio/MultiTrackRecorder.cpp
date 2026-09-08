/*
 * MultiTrackRecorder.cpp - owns the hardcoded two-track capture streams of the
 *                          two-track recording prototype
 *
 * Copyright (c) 2026 LMMS developers
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

#include "MultiTrackRecorder.h"

#include "SampleFrame.h"

namespace lmms
{


MultiTrackRecorder::MultiTrackRecorder()
{
	// Hardcoded prototype mapping: track 0 <- input channel 0,
	// track 1 <- input channel 1.
	for (int i = 0; i < NumTracks; ++i)
	{
		m_tracks[i].setInputChannel(i);
	}
}




MultiTrackRecorder::~MultiTrackRecorder()
{
	disarmAll();
}




void MultiTrackRecorder::processInput(const SampleFrame* input, f_cnt_t frames) noexcept
{
	for (auto& track : m_tracks)
	{
		track.processInput(input, frames);
	}
}




bool MultiTrackRecorder::armTrack(int index, const std::string& filePath,
	int sampleRate, int inputChannel)
{
	if (index < 0 || index >= NumTracks)
	{
		return false;
	}
	return m_tracks[index].arm(filePath, sampleRate, inputChannel);
}




void MultiTrackRecorder::disarmAll()
{
	for (auto& track : m_tracks)
	{
		track.disarm();
	}
}




std::uint64_t MultiTrackRecorder::totalOverflowCount() const noexcept
{
	std::uint64_t total = 0;
	for (const auto& track : m_tracks)
	{
		total += track.overflowCount();
	}
	return total;
}


} // namespace lmms
