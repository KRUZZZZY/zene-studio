/*
 * AudioFileWave.h - AudioDevice which encodes wave-stream and writes it
 *                   into a WAVE-file. This is used for song-export.
 *
 * Copyright (c) 2004-2009 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#ifndef LMMS_AUDIO_FILE_WAVE_H
#define LMMS_AUDIO_FILE_WAVE_H

#include "lmmsconfig.h"
#include "AudioFileDevice.h"
#include "ExportDither.h"
#include "ExportNoiseShaper.h"

#include <sndfile.h>

#include <vector>

namespace lmms
{

class AudioFileWave : public AudioFileDevice
{
public:
	AudioFileWave( OutputSettings const & outputSettings,
			const ch_cnt_t channels,
			bool & successful,
			const QString & file,
			AudioEngine* audioEngine );
	~AudioFileWave() override;

	static AudioFileDevice * getInst( const QString & outputFilename,
					  OutputSettings const & outputSettings,
					  const ch_cnt_t channels,
					  AudioEngine* audioEngine,
					  bool & successful )
	{
		return new AudioFileWave( outputSettings, channels, successful,
					  outputFilename, audioEngine );
	}


private:
	bool writeBuffer(const SampleFrame* _ab, const f_cnt_t _frames) override;
	bool writeInterleaved(float* samples, const f_cnt_t frames) override;

	bool startEncoding();
	//! 0.5.0 F7: a file of more than two channels is WAVE_FORMAT_EXTENSIBLE with the speakers' mask.
	bool setChannelMap();

public:
	/*! Finalises the file: closes the libsndfile handle, patches a discrete layout's channel mask
	 *  and reports whether the file was written completely. A file that was not is REMOVED here,
	 *  so a failed export leaves nothing behind to be mistaken for a render. Idempotent: a render
	 *  already called it, the destructor calls it again. */
	bool finishEncoding() override;

private:
	//! The bytes of sample data the frames written so far must occupy in the file.
	sf_count_t expectedDataBytes() const;
	/*! True when the finished WAV's own header accounts for every frame the writes reported.
	 *  sf_close() rewrites the RIFF and data-chunk sizes in place, and it reports success even
	 *  when that rewrite only partly landed: the file's LENGTH is unchanged then (the old bytes
	 *  are still there), so a size-only check cannot see it. The header fields themselves can. */
	bool headerHoldsEveryFrame(sf_count_t expectedData) const;
	//! A discrete layout has no speaker positions; libsndfile writes its count default mask, so zero it
	//! after the samples are flushed (WAVEX's dwChannelMask 0 is what "no speaker positions" means).
	//! False when the field could not be written: that is a broken header, not a finished export.
	bool writeZeroChannelMask();

private:
	SF_INFO m_si;
	SNDFILE * m_sf;
	//! Frames libsndfile reported as written: what the close has to leave in the file.
	sf_count_t m_framesWritten = 0;
	bool m_zeroChannelMask = false;
	//! TPDF dither for the integer depths (include/ExportDither.h). Off unless
	//! OutputSettings::dither() is on; seeded from a constant so a dithered
	//! render is still reproducible.
	ExportDither m_dither;
	//! DitherMode::NoiseShaped at 16-bit (include/ExportNoiseShaper.h): the encoder does the
	//! requantisation itself so the error it feeds back is known.
	ExportNoiseShaper m_noiseShaper;
	//! The 16-bit staging of writeInterleaved(), sized on its first call (the render thread is offline).
	std::vector<int_sample_t> m_interleaved16;
} ;


} // namespace lmms

#endif // LMMS_AUDIO_FILE_WAVE_H
