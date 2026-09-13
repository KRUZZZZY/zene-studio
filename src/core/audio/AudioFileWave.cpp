/*
 * AudioFileWave.cpp - audio-device which encodes wave-stream and writes it
 *                     into a WAVE-file. This is used for song-export.
 *
 * Copyright (c) 2004-2013 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#include "AudioFileWave.h"
#include "endian_handling.h"
#include "AudioEngine.h"

#include <algorithm>
#include <memory>


namespace lmms
{

namespace
{

/*! The bit depth the TPDF dither is drawn against, or 0 for "do not dither".
 *
 *  Two conditions, and both have to hold: the render asked for dither
 *  (OutputSettings::dither(), off by default), and the depth actually written
 *  is integer. 32-bit WAV is IEEE float - it has no quantisation step, so there
 *  is nothing to dither and adding noise to it would only degrade it.
 */
int ditherBitsFor(OutputSettings::BitDepth bitDepth, bool ditherEnabled)
{
	if (!ditherEnabled) { return 0; }
	switch (bitDepth)
	{
	case OutputSettings::BitDepth::Depth16Bit: return 16;
	case OutputSettings::BitDepth::Depth24Bit: return 24;
	case OutputSettings::BitDepth::Depth32Bit:
	default: return 0;
	}
}

} // namespace

AudioFileWave::AudioFileWave( OutputSettings const & outputSettings,
				const ch_cnt_t channels, bool & successful,
				const QString & file,
				AudioEngine* audioEngine ) :
	AudioFileDevice( outputSettings, channels, file, audioEngine ),
	m_sf( nullptr ),
	m_dither()
{
	successful = outputFileOpened() && startEncoding();
}



AudioFileWave::~AudioFileWave()
{
	finishEncoding();
}




bool AudioFileWave::startEncoding()
{
	m_si.samplerate = sampleRate();
	m_si.channels = channels();
	m_si.frames = audioEngine()->framesPerPeriod();
	m_si.sections = 1;
	m_si.seekable = 0;

	m_si.format = SF_FORMAT_WAV;

	switch( getOutputSettings().getBitDepth() )
	{
	case OutputSettings::BitDepth::Depth32Bit:
		m_si.format |= SF_FORMAT_FLOAT;
		break;
	case OutputSettings::BitDepth::Depth24Bit:
		m_si.format |= SF_FORMAT_PCM_24;
		break;
	case OutputSettings::BitDepth::Depth16Bit:
	default:
		m_si.format |= SF_FORMAT_PCM_16;
		break;
	}

	// Use file handle to handle unicode file name on Windows
	m_sf = sf_open_fd( outputFileHandle(), SFM_WRITE, &m_si, false );

	if (!m_sf)
	{
		qWarning("Error: AudioFileWave::startEncoding: %s", sf_strerror(nullptr));
		return false;
	}

	// Prevent fold overs when encountering clipped data
	sf_command(m_sf, SFC_SET_CLIPPING, nullptr, SF_TRUE);

	sf_set_string ( m_sf, SF_STR_SOFTWARE, "Zene Studio" );

	return true;
}

void AudioFileWave::writeBuffer(const SampleFrame* _ab, const f_cnt_t _frames)
{
	OutputSettings::BitDepth bitDepth = getOutputSettings().getBitDepth();
	const int ditherBits = ditherBitsFor(bitDepth, getOutputSettings().dither());

	if( bitDepth == OutputSettings::BitDepth::Depth32Bit || bitDepth == OutputSettings::BitDepth::Depth24Bit )
	{
		auto buf = new float[_frames * channels()];
		for( f_cnt_t frame = 0; frame < _frames; ++frame )
		{
			for( ch_cnt_t chnl = 0; chnl < channels(); ++chnl )
			{
				buf[frame * channels() + chnl] = _ab[frame][chnl];
			}
		}
		// The dither goes in BEFORE libsndfile quantises the floats to 24-bit
		// integer, which is the only order that removes the correlation between
		// the quantisation error and the signal. Off by default, so with no
		// dither request this call is absent and the bytes are what they were.
		if (ditherBits > 0)
		{
			m_dither.ditherInterleaved(buf, static_cast<std::size_t>(_frames) * channels(), ditherBits);
		}
		sf_writef_float( m_sf, buf, _frames );
		delete[] buf;
	}
	else
	{
		auto buf = new int_sample_t[_frames * channels()];
		if (ditherBits > 0)
		{
			// The 16-bit path converts a whole SampleFrame array, so the dither
			// is applied to a staged copy and the conversion then reads the
			// dithered value - the same order as the 24-bit path above.
			auto staged = std::make_unique<SampleFrame[]>(_frames);
			std::copy_n(_ab, _frames, staged.get());
			m_dither.ditherFrames(staged.get(), _frames, ditherBits);
			convertToS16(staged.get(), _frames, buf, !isLittleEndian());
		}
		else
		{
			convertToS16(_ab, _frames, buf, !isLittleEndian());
		}

		sf_writef_short( m_sf, buf, _frames );
		delete[] buf;
	}
}




void AudioFileWave::finishEncoding()
{
	if( m_sf )
	{
		sf_close( m_sf );
	}
}

} // namespace lmms